"""Web assets generated at build time from data outside web/.

Shared by build_web.py (packages them into firmware) and dev_server.py (serves
them during page development), so both produce identical bytes.
"""

import csv
import json
import pathlib

# Must match kTimezoneRuleLength in src/timezone_rule.h (which includes NUL).
TIMEZONE_RULE_LENGTH = 48


def timezones_js(project_dir: pathlib.Path) -> bytes:
    """IANA zone names and their POSIX rules, for the /time and /sunset pickers.

    Source: assets/timezones.csv, vendored from nayarsystems/posix_tz_db (the
    same database the ESP8266 core's TZ.h is generated from). The browser holds
    the table, so the firmware stores only the one rule it needs.
    """
    rows = []
    with open(project_dir / "assets" / "timezones.csv", newline="", encoding="utf-8") as f:
        for name, rule in csv.reader(f):
            if len(rule) >= TIMEZONE_RULE_LENGTH:
                raise RuntimeError(f"timezone rule for {name} exceeds {TIMEZONE_RULE_LENGTH - 1} chars")
            rows.append([name, rule])
    if len(rows) < 400:
        raise RuntimeError(f"assets/timezones.csv has only {len(rows)} zones")
    body = json.dumps(rows, separators=(",", ":"))
    return f"var TZ_ZONES={body};\n".encode("utf-8")


# route -> generator(project_dir) -> bytes
GENERATORS = {
    "/tz.js": timezones_js,
}
