"""Build-time guard: data/config.json must equal the compiled defaults.

data/config.json spells out every setting so it documents the file's shape,
which puts each default in two places: the C++ (struct initializers in the
headers, overridden by assignments in defaults.cpp's fillDefaults) and the
JSON. The two have drifted before. This script fails the build when they
disagree, when the JSON is missing a known field, or when it carries a field
this script does not know.

Like check_formats.py it parses the C++ rather than compiling it, so it needs
only Python. A default is resolved the way the firmware resolves it: an
assignment in defaults.cpp wins, otherwise the struct's member initializer.

Three groups are deliberately device-specific and are not compared:
time.timezone, location, and sunset.

Adding a config field: add it to data/config.json and to EXPECTED below.
"""

import json
import os
import re
import sys

# JSON paths whose values are this device's own, not compiled defaults.
DEVICE_SPECIFIC_PREFIXES = ("time.timezone.", "location.", "sunset.")


def fail(message):
    sys.stderr.write("check_config_defaults: %s\n" % message)
    sys.exit(1)


def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


class CppSources:
    """The handful of source files defaults are read from, with lookups that
    turn C++ declarations into Python values."""

    def __init__(self, src_dir):
        self.defaults = read(os.path.join(src_dir, "defaults.cpp"))
        self.headers = "\n".join(
            read(os.path.join(src_dir, name))
            for name in ("config.h", "beep_pattern.h", "schedule.h", "config_serializer.h")
        )

    def constant(self, name):
        """Value of a named constant in defaults.cpp or the headers."""
        for text in (self.defaults, self.headers):
            match = re.search(r"\b%s(?:\[\])?\s*=\s*([^;]+);" % re.escape(name), text)
            if match:
                return self.literal(match.group(1))
        fail("could not find constant %s" % name)

    def literal(self, expression):
        """Converts one C++ initializer expression to a Python value."""
        expression = expression.strip()
        if expression.startswith('"'):
            return bytes(expression[1:-1], "utf-8").decode("unicode_escape")
        if expression in ("true", "false"):
            return expression == "true"
        if expression == "kSameFormat":
            return "same"  # kSameFormatKey, the JSON spelling of "no separate format".
        if expression.startswith("kMode"):
            return expression[len("kMode"):].lower()  # kModeClock -> "clock"
        if re.fullmatch(r"[0-9xXa-fA-F\s+\-*/()UL]+", expression):
            return int(eval(re.sub(r"(?<=[0-9])[UL]+", "", expression), {"__builtins__": {}}))
        if re.fullmatch(r"k[A-Za-z0-9]+", expression):
            return self.constant(expression)
        fail("cannot interpret C++ expression %r" % expression)

    def struct_body(self, struct_name):
        start = self.headers.find("struct %s {" % struct_name)
        if start < 0:
            fail("could not find struct %s" % struct_name)
        depth, index = 0, self.headers.index("{", start)
        for index in range(index, len(self.headers)):
            depth += {"{": 1, "}": -1}.get(self.headers[index], 0)
            if depth == 0:
                return self.headers[start:index]
        fail("unterminated struct %s" % struct_name)

    def initializer(self, struct_name, field):
        match = re.search(r"\b%s\s*=\s*([^;]+);" % re.escape(field), self.struct_body(struct_name))
        if not match:
            fail("struct %s has no initializer for %s" % (struct_name, field))
        return self.literal(match.group(1))

    def assignment(self, cpp_path):
        """What fillDefaults() assigns to config.<cpp_path>, or None."""
        pattern = r"\bconfig\.%s\s*=\s*([^;]+);" % re.escape(cpp_path)
        match = re.search(pattern, self.defaults)
        if match:
            return self.resolve(match.group(1))
        # String fields are filled with snprintf(config.<path>, ..., "%s", kName).
        match = re.search(
            r'snprintf\(config\.%s,[^;]*"%%s",\s*(\w+)\)' % re.escape(cpp_path),
            self.defaults, re.S)
        return self.constant(match.group(1)) if match else None

    def resolve(self, expression):
        expression = expression.strip()
        format_key = re.fullmatch(r"formatIndexOrFirst\(\w+,\s*(\w+)\)", expression)
        if format_key:
            return self.constant(format_key.group(1))  # Stored as its key.
        other_field = re.fullmatch(r"config\.([\w.]+)", expression)
        if other_field:
            value = self.assignment(other_field.group(1))
            if value is None:
                fail("defaults.cpp copies config.%s, which it never assigns" % other_field.group(1))
            return value
        return self.literal(expression)

    def default(self, cpp_path, struct_name, field):
        """fillDefaults() assignment if any, else the member initializer."""
        assigned = self.assignment(cpp_path)
        return assigned if assigned is not None else self.initializer(struct_name, field)

    def trading_intervals(self):
        body = self.function_body("defaultTradingSchedule")
        rows = re.findall(r"intervals\[(\d)\]\s*=\s*\{([^,]+),([^}]+)\}", body)
        if not rows:
            fail("could not parse defaultTradingSchedule() intervals")
        hhmm = lambda minutes: "%02d:%02d" % divmod(minutes, 60)
        return [{"start": hhmm(self.literal(a)), "stop": hhmm(self.literal(b))}
                for _index, a, b in sorted(rows)]

    def trading_interval_count(self):
        match = re.search(r"intervalCount\s*=\s*([^;]+);", self.function_body("defaultTradingSchedule"))
        if not match:
            fail("could not parse defaultTradingSchedule() intervalCount")
        return self.literal(match.group(1))

    def wifi_defaults(self):
        match = re.search(r"WifiConfig\{([^}]*)\}", self.function_body("defaultWifiConfig"))
        if not match:
            fail("could not parse defaultWifiConfig()")
        return [self.literal(part) for part in match.group(1).split(",")]

    def function_body(self, name):
        # The definition, not a call such as fillDefaults()'s use of it.
        start = self.defaults.find(name + "() {")
        if start < 0:
            fail("could not find %s in defaults.cpp" % name)
        return self.defaults[start:self.defaults.index("\n}", start)]


def expected_values(cpp):
    """JSON path -> the value the compiled defaults give it."""
    d = cpp.default
    wifi = cpp.wifi_defaults()  # staSsid, staPassword, apSsid, apPassword
    pattern = lambda number, field: d(
        "sound.boundaryAlert.boundary%d.%s" % (number, field), "BeepPattern", field)
    return {
        "configVersion": cpp.constant("kConfigSchemaVersion"),
        "display.activeMode": cpp.assignment("activeMode"),
        "display.brightness": d("display.brightness", "DisplayConfig", "brightness"),
        "display.clock12Hour": d("display.clockUse12Hour", "DisplayConfig", "clockUse12Hour"),
        "display.messages.splash": cpp.assignment("messages.splash"),
        "display.messages.final": cpp.assignment("messages.countdownDone"),
        "display.messages.fridaySunset": cpp.assignment("messages.fridaySunset"),
        "display.messages.tradingOpen": cpp.assignment("messages.tradingOpen"),
        "display.messages.tradingClose": cpp.assignment("messages.tradingClose"),
        "display.modes.countdown.format": cpp.assignment("countdown.format"),
        "display.modes.countdown.end": cpp.assignment("countdown.end"),
        "display.modes.countup.format": cpp.assignment("countup.format"),
        "display.modes.countup.start": cpp.assignment("countup.start"),
        "display.modes.clock.format": cpp.assignment("display.clockFormat"),
        "display.modes.friday.clockFormat": cpp.assignment("friday.clockFormat"),
        "display.modes.friday.toFridaySunsetFormat": cpp.assignment("friday.toFridaySunsetFormat"),
        "display.modes.friday.toSaturdaySunsetFormat": cpp.assignment("friday.toSaturdaySunsetFormat"),
        "display.modes.friday.blinkBeforeMinutes": d("friday.blinkBeforeMinutes", "FridayConfig", "blinkBeforeMinutes"),
        "display.modes.friday.blinkAfterMinutes": d("friday.blinkAfterMinutes", "FridayConfig", "blinkAfterMinutes"),
        "display.modes.trading.format": cpp.assignment("trading.format"),
        "display.modes.trading.formatOver24": d("trading.formatOver24", "TradingConfig", "formatOver24"),
        "display.modes.trading.intervalCount": cpp.trading_interval_count(),
        "display.modes.trading.intervals": cpp.trading_intervals(),
        "sound.enabled": d("sound.enabled", "SoundConfig", "enabled"),
        "sound.volume": d("sound.volumePercent", "SoundConfig", "volumePercent"),
        "sound.startupBeep": d("sound.startupBeep", "SoundConfig", "startupBeep"),
        "sound.finalBeep": d("sound.finalBeep", "SoundConfig", "finalBeep"),
        "sound.fridaySunsetBeep": d("sound.fridaySunsetBeep", "SoundConfig", "fridaySunsetBeep"),
        "sound.tradingOpenBeep": d("sound.tradingOpenBeep", "SoundConfig", "tradingOpenBeep"),
        "sound.tradingCloseBeep": d("sound.tradingCloseBeep", "SoundConfig", "tradingCloseBeep"),
        "sound.boundaryAlert.enabled": d("sound.boundaryAlert.enabled", "BoundaryAlertConfig", "enabled"),
        "sound.boundaryAlert.boundary1.toneHz": pattern(1, "toneHz"),
        "sound.boundaryAlert.boundary1.totalDurationSeconds": pattern(1, "totalDurationSeconds"),
        "sound.boundaryAlert.boundary1.startingBeatsHz": pattern(1, "startingBeatsHz"),
        "sound.boundaryAlert.boundary2.toneHz": pattern(2, "toneHz"),
        "sound.boundaryAlert.boundary2.totalDurationSeconds": pattern(2, "totalDurationSeconds"),
        "sound.boundaryAlert.boundary2.startingBeatsHz": pattern(2, "startingBeatsHz"),
        "wifi.station.ssid": wifi[0],
        "wifi.station.password": wifi[1],
        "wifi.accessPoint.ssid": wifi[2],
        "wifi.accessPoint.password": wifi[3],
    }


def flatten(node, prefix=""):
    """Leaf values by dotted path; arrays are compared whole."""
    if isinstance(node, dict):
        leaves = {}
        for key, value in node.items():
            leaves.update(flatten(value, prefix + key + "."))
        return leaves
    return {prefix[:-1]: node}


def main(project_dir):
    config_path = os.path.join(project_dir, "data", "config.json")
    try:
        shipped = flatten(json.loads(read(config_path)))
    except ValueError as error:
        fail("data/config.json does not parse: %s" % error)

    expected = expected_values(CppSources(os.path.join(project_dir, "src")))
    device_specific = [p for p in shipped if p.startswith(DEVICE_SPECIFIC_PREFIXES)]

    missing = sorted(set(expected) - set(shipped))
    if missing:
        fail("data/config.json is missing %s" % ", ".join(missing))
    unknown = sorted(set(shipped) - set(expected) - set(device_specific))
    if unknown:
        fail("data/config.json has fields this check does not know: %s "
             "(add them to EXPECTED in tools/check_config_defaults.py)" % ", ".join(unknown))
    for path, value in expected.items():
        # bool is an int in Python; compare types too, so 0 never equals false.
        if (shipped[path] != value) or (type(shipped[path]) is not type(value)):
            fail("data/config.json %s is %r, but the compiled default is %r"
                 % (path, shipped[path], value))

    print("check_config_defaults: data/config.json matches %d compiled defaults "
          "(%d device-specific fields not compared)" % (len(expected), len(device_specific)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
else:
    # PlatformIO pre-script entry point; see check_formats.py for why only the
    # probe is guarded.
    try:
        Import("env")  # noqa: F821
        main(env.subst("$PROJECT_DIR"))  # noqa: F821
    except NameError:
        pass
