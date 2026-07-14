Import("env")

import gzip
import hashlib
import pathlib

# Packages web/ sources into a single generated header of gzipped PROGMEM
# assets. Pages reference /common.css and /common.js; those references are
# stamped with a content hash so the browser's immutable cache is busted
# whenever a shared asset changes, while pages themselves stay no-cache.

project = pathlib.Path(env.subst("$PROJECT_DIR"))
web_dir = project / "web"
build_dir = pathlib.Path(env.subst("$BUILD_DIR"))
output = build_dir / "generated_web_assets.h"

# route -> (source file, content type, immutable cache)
PAGES = {
    "/": "home.html",
    "/format": "format.html",
    "/settings": "settings.html",
    "/config": "files.html",
    "/time": "time.html",
    "/sunset": "sunset.html",
    "/messages": "messages.html",
    "/location": "location.html",
    "/wifi": "wifi.html",
    "/view": "view.html",
}
SHARED = {
    "/common.css": ("common.css", "text/css"),
    "/common.js": ("common.js", "application/javascript"),
}


def short_hash(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()[:8]


def gzip_bytes(raw: bytes, label: str) -> bytes:
    compressed = gzip.compress(raw, compresslevel=9, mtime=0)
    if gzip.decompress(compressed) != raw:
        raise RuntimeError(f"Gzip verification failed for {label}")
    return compressed


assets = []  # (route, content_type, immutable, symbol, gzipped)

versions = {}
for route, (filename, content_type) in SHARED.items():
    raw = (web_dir / filename).read_bytes()
    versions[route] = short_hash(raw)
    symbol = "WEB_" + filename.replace(".", "_").upper()
    assets.append((route, content_type, True, symbol, gzip_bytes(raw, filename)))

for route, filename in PAGES.items():
    text = (web_dir / "pages" / filename).read_text(encoding="utf-8")
    for shared_route, version in versions.items():
        reference = f'"{shared_route}"'
        if reference not in text:
            raise RuntimeError(f"{filename} does not reference {shared_route}")
        text = text.replace(reference, f'"{shared_route}?v={version}"')
    symbol = "WEB_PAGE_" + filename.replace(".html", "").upper()
    assets.append((route, "text/html", False, symbol, gzip_bytes(text.encode("utf-8"), filename)))

lines = [
    "#pragma once",
    "#include <Arduino.h>",
    "",
    "struct WebAsset {",
    "  const char* path;",
    "  const char* contentType;",
    "  bool immutable;  // Long-lived cache (hash-versioned shared assets).",
    "  const uint8_t* data;  // Gzipped body in PROGMEM.",
    "  size_t size;",
    "};",
    "",
]
for route, content_type, immutable, symbol, gz in assets:
    print(f"Web asset {route}: {len(gz)} bytes gzipped")
    values = ",".join(f"0x{value:02x}" for value in gz)
    lines.append(f"const uint8_t {symbol}[] PROGMEM = {{{values}}};")
lines.append("")
lines.append("const WebAsset kWebAssets[] = {")
for route, content_type, immutable, symbol, gz in assets:
    flag = "true" if immutable else "false"
    lines.append(f'  {{"{route}", "{content_type}", {flag}, {symbol}, sizeof({symbol})}},')
lines.append("};")
lines.append("constexpr size_t kWebAssetCount = sizeof(kWebAssets) / sizeof(kWebAssets[0]);")
lines.append("")

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("\n".join(lines), encoding="utf-8")
env.Append(CPPPATH=[str(build_dir)])
