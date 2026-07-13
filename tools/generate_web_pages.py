Import("env")

import gzip
import pathlib
import re

project = pathlib.Path(env.subst("$PROJECT_DIR"))
source = (project / "src" / "html.cpp").read_text(encoding="utf-8")
build_dir = pathlib.Path(env.subst("$BUILD_DIR"))
output = build_dir / "generated_web_pages.h"

pages = {
    "SETTINGS_HTML": "GZIP_SETTINGS_HTML",
    "COMPACT_FORMAT_HTML": "GZIP_FORMAT_HTML",
    "WIFI_HTML": "GZIP_WIFI_HTML",
    "CONFIG_JSON_HTML": "GZIP_CONFIG_JSON_HTML",
    "COMPACT_TIME_HTML": "GZIP_TIME_HTML",
    "SUNSET_HTML": "GZIP_SUNSET_HTML",
    "MESSAGE_HTML": "GZIP_MESSAGE_HTML",
    "VIEW_FILE_HTML": "GZIP_VIEW_FILE_HTML",
    "LOCATION_HTML": "GZIP_LOCATION_HTML",
}

lines = ["#pragma once", "#include <Arduino.h>", ""]
for source_name, generated_name in pages.items():
    pattern = rf'const char\s+{source_name}\[\]\s+PROGMEM\s*=\s*R"rawliteral\((.*?)\)rawliteral";'
    match = re.search(pattern, source, re.DOTALL)
    if not match:
        raise RuntimeError(f"Could not find HTML source {source_name}")
    raw = match.group(1).encode("utf-8")
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)
    if gzip.decompress(compressed) != raw:
        raise RuntimeError(f"Gzip verification failed for {source_name}")
    print(f"Web page {source_name}: {len(raw)} -> {len(compressed)} bytes")
    values = ",".join(f"0x{value:02x}" for value in compressed)
    lines.append(f"const uint8_t {generated_name}[] PROGMEM = {{{values}}};")
    lines.append(f"constexpr size_t {generated_name}_SIZE = sizeof({generated_name});")
    lines.append("")

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("\n".join(lines), encoding="utf-8")
env.Append(CPPPATH=[str(build_dir)])
