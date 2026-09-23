"""Build-time guard on the display-format catalog's stable keys.

A format's key is its identity: it is what /config.json stores and what the web
dropdowns post back. Two rows sharing a key, or a default naming a key that does
not exist, silently repoints saved selections at the wrong format on every
device. This script fails the build on either, plus the related invariants that
have no other enforcement.

It parses the C++ rather than executing it, so it runs on any machine with
Python and needs no host C++ compiler. The equivalent assertions also exist as
Unity tests in test/test_formats/ for anyone who has one; this is the copy that
runs on every `pio run`.

Checks:
  1. The three parallel tables per group (keys, labels, specs) are the same
     length, so no row can pick up another row's key.
  2. Every key in a group is unique, non-empty, and fits kFormatKeyLength.
  3. Every label fits kFormatLabelLength.
  4. Every format key named in defaults.cpp exists in the right group.
  5. Every format key in data/config.json exists in the right group.
  6. Every format that renders a combined hhh:mm panel has a split fallback
     with an identical seconds panel, which resolveCountingOverflow() needs.
"""

import json
import os
import re
import sys

# Mirrors FormatGroup in display_format.h.
COUNTING = "counting"
CLOCK = "clock"


def fail(message):
    sys.stderr.write("check_formats: %s\n" % message)
    sys.exit(1)


def read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def parse_length(header_text, name):
    match = re.search(name + r"\s*=\s*(\d+)", header_text)
    if not match:
        fail("could not find %s in display_format.h" % name)
    return int(match.group(1))


def parse_string_array(source_text, array_name):
    """Returns the string literals of a `const char name[][N]` table, in order."""
    start = source_text.find(array_name + "[][")
    if start < 0:
        fail("could not find string table %s" % array_name)
    open_brace = source_text.find("{", start)
    end = source_text.find("\n};", open_brace)
    if end < 0:
        fail("unterminated string table %s" % array_name)
    body = source_text[open_brace:end]
    values = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    if not values:
        fail("parsed zero entries from %s" % array_name)
    return values


def parse_spec_table(source_text, table_name):
    """Returns the per-row panel-spec text of a FormatSpec table, in order."""
    start = source_text.find("const FormatSpec %s[] = {" % table_name)
    if start < 0:
        fail("could not find table %s" % table_name)
    end = source_text.find("\n};", start)
    if end < 0:
        fail("unterminated table %s" % table_name)
    body = source_text[start:end]
    rows = re.findall(r"^\s*(\{\{.*\}\}),\s*$", body, re.M)
    if not rows:
        fail("parsed zero rows from %s" % table_name)
    return rows


def parse_group(source_text, keys_name, labels_name, table_name, group_name):
    """Zips the three parallel tables for one format group."""
    keys = parse_string_array(source_text, keys_name)
    labels = parse_string_array(source_text, labels_name)
    panels = parse_spec_table(source_text, table_name)
    if not (len(keys) == len(labels) == len(panels)):
        fail(
            "%s tables are not parallel: %d keys, %d labels, %d specs"
            % (group_name, len(keys), len(labels), len(panels))
        )
    return list(zip(keys, labels, panels))


def check_unique(group_name, rows, key_length):
    seen = {}
    for index, (key, label, _panels) in enumerate(rows):
        if not key:
            fail("%s row %d has an empty key (label %r)" % (group_name, index, label))
        if len(key) > key_length - 1:
            fail(
                "%s key %r is %d chars; kFormatKeyLength allows %d"
                % (group_name, key, len(key), key_length - 1)
            )
        if key in seen:
            fail(
                "%s rows %d (%r) and %d (%r) share the key %r"
                % (group_name, seen[key][0], seen[key][1], index, label, key)
            )
        seen[key] = (index, label)


def check_label_widths(group_name, rows, label_length):
    for index, (_key, label, _panels) in enumerate(rows):
        if len(label) > label_length - 1:
            fail(
                "%s row %d label %r is %d chars; kFormatLabelLength allows %d"
                % (group_name, index, label, len(label), label_length - 1)
            )


def seconds_panel(panels_source):
    """The third panel's spec text, normalized for comparison."""
    inner = panels_source.strip()[1:-1].strip()  # drop the outer {}
    panels = re.findall(r"\{[^{}]*\}", inner)
    if len(panels) != 3:
        return None
    return re.sub(r"\s+", "", panels[2])


def check_overflow_fallbacks(rows):
    """resolveCountingOverflow() needs a split variant for each combined row."""
    splits = []
    for key, _label, panels in rows:
        normalized = re.sub(r"\s+", "", panels)
        if (
            "S::kNumber,F::kTotalHours" in normalized
            and "S::kNumber,F::kMinutes" in normalized
        ):
            splits.append((key, seconds_panel(panels)))

    for key, label, panels in rows:
        normalized = re.sub(r"\s+", "", panels)
        if "S::kColon,F::kTotalHours" not in normalized:
            continue
        wanted = seconds_panel(panels)
        if not any(existing == wanted for _k, existing in splits):
            fail(
                "counting format %r (%s) renders a combined hhh:mm panel but no "
                "split hhh | mm variant shares its seconds panel, so "
                "resolveCountingOverflow() cannot fall back above 99 hours"
                % (key, label)
            )


def keys_for(rows):
    return set(key for key, _label, _panels in rows)


def check_defaults(defaults_text, counting_keys, clock_keys):
    expectations = [
        ("kDefaultClockFormat", clock_keys, CLOCK),
        ("kDefaultCountingFormat", counting_keys, COUNTING),
    ]
    for name, valid, group in expectations:
        match = re.search(name + r'\s*=\s*"([^"]*)"', defaults_text)
        if not match:
            fail("could not find %s in defaults.cpp" % name)
        key = match.group(1)
        if key not in valid:
            fail(
                "defaults.cpp %s is %r, which is not a %s format key"
                % (name, key, group)
            )


FIELD_GROUPS = {
    "modes.countdown.format": COUNTING,
    "modes.countup.format": COUNTING,
    "modes.clock.format": CLOCK,
    "modes.friday.clockFormat": CLOCK,
    "modes.friday.toFridaySunsetFormat": COUNTING,
    "modes.friday.toSaturdaySunsetFormat": COUNTING,
    "modes.trading.format": COUNTING,
    "modes.trading.formatOver24": COUNTING,
}


def check_config_formats(node, path, counting_keys, clock_keys):
    """Every format key in a shipped config.json must name a real format."""
    display = node.get("display")
    if not isinstance(display, dict):
        return
    for field_path, group in FIELD_GROUPS.items():
        valid = counting_keys if group == COUNTING else clock_keys
        cursor = display
        for part in field_path.split("."):
            if not isinstance(cursor, dict) or part not in cursor:
                cursor = None
                break
            cursor = cursor[part]
        if cursor is None:
            continue
        # An integer is a legacy index, still accepted by the firmware.
        if isinstance(cursor, int):
            continue
        if not isinstance(cursor, str):
            fail(
                "%s display.%s is %r, expected a format key"
                % (path, field_path, cursor)
            )
        if cursor == "same":
            continue
        if cursor not in valid:
            fail(
                "%s display.%s is %r, which is not a valid %s format key"
                % (path, field_path, cursor, group)
            )


def main(project_dir):
    src = os.path.join(project_dir, "src")
    header = read(os.path.join(src, "display_format.h"))
    source = read(os.path.join(src, "display_format.cpp"))
    defaults = read(os.path.join(src, "defaults.cpp"))

    key_length = parse_length(header, "kFormatKeyLength")
    label_length = parse_length(header, "kFormatLabelLength")

    counting = parse_group(
        source, "kCountingKeys", "kCountingLabels", "kCountingFormats", COUNTING
    )
    clock = parse_group(source, "kClockKeys", "kClockLabels", "kClockFormats", CLOCK)

    check_unique(COUNTING, counting, key_length)
    check_unique(CLOCK, clock, key_length)
    check_label_widths(COUNTING, counting, label_length)
    check_label_widths(CLOCK, clock, label_length)
    check_overflow_fallbacks(counting)

    counting_keys = keys_for(counting)
    clock_keys = keys_for(clock)
    check_defaults(defaults, counting_keys, clock_keys)

    config_path = os.path.join(project_dir, "data", "config.json")
    if os.path.exists(config_path):
        try:
            config = json.loads(read(config_path))
        except ValueError as error:
            fail("data/config.json does not parse: %s" % error)
        check_config_formats(config, "data/config.json", counting_keys, clock_keys)

    print(
        "check_formats: %d counting + %d clock formats, keys unique and resolvable"
        % (len(counting), len(clock))
    )


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
else:
    # PlatformIO pre-script entry point. SCons does not define __file__ here, so
    # paths come from $PROJECT_DIR. Only this probe is guarded: widening the try
    # would report a NameError anywhere above as "not running under PlatformIO".
    try:
        Import("env")  # noqa: F821
        main(env.subst("$PROJECT_DIR"))  # noqa: F821
    except NameError:
        pass
