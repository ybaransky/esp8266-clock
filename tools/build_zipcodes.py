import csv
import math
import pathlib
import struct
import sys

# Packs assets/zipcodes.csv into data/zipcodes.bin, the table that ships on
# LittleFS and that src/zipcode.cpp reads. This script and zipcode.cpp are the
# only two places the binary layout is spelled out; change them together.
#
#   [0]     8 bytes       "ZIPB", uint8 version, uint8 recordSize,
#                         uint16 recordCount
#   [8]     2002 bytes    directory: 1001 x uint16 cumulative record index, one
#                         per three-digit prefix plus a terminating entry
#   [2010]  5 bytes each  records in ZIP order: uint8 suffix, int16 latitude,
#                         int16 longitude, both in hundredths of a degree
#
# A prefix's records are [directory[prefix], directory[prefix + 1]), so the
# reader needs one four-byte read to learn both where a bucket starts and how
# long it is. Coordinates are quantized to 0.01 degrees (~1 km, under two
# seconds of sunset error) and the three-digit prefix is implied by the
# directory slot instead of stored. Those two choices are what take the table
# from 826 KB of CSV to 167 KB.

MAGIC = b"ZIPB"
VERSION = 1
RECORD_SIZE = 5
HEADER_SIZE = 8
PREFIX_COUNT = 1000
DIRECTORY_SIZE = (PREFIX_COUNT + 1) * 2
RECORDS_OFFSET = HEADER_SIZE + DIRECTORY_SIZE
SCALE = 100.0  # Hundredths of a degree.
TOLERANCE = 0.0051  # Half a quantization step, plus room for float noise.


def read_source(path):
    """Returns [(zipcode, latitude, longitude)] sorted by ZIP code."""
    rows = []
    seen = set()
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        if header != ["zipcode", "lat", "lon"]:
            raise RuntimeError(f"{path.name}: unexpected header {header}")
        for line, row in enumerate(reader, start=2):
            if not row:
                continue
            if len(row) != 3:
                raise RuntimeError(f"{path.name}:{line}: expected 3 columns, got {len(row)}")
            zipcode = row[0]
            if (len(zipcode) != 5) or not zipcode.isdigit():
                raise RuntimeError(f"{path.name}:{line}: bad ZIP code {zipcode!r}")
            if zipcode in seen:
                raise RuntimeError(f"{path.name}:{line}: duplicate ZIP code {zipcode}")
            seen.add(zipcode)
            rows.append((zipcode, float(row[1]), float(row[2])))
    rows.sort(key=lambda row: row[0])
    return rows


def quantize(degrees, label):
    value = math.floor(degrees * SCALE + 0.5)
    if not (-32768 <= value <= 32767):
        raise RuntimeError(f"{label}: {degrees} does not fit an int16 at 0.01 degrees")
    return value


def encode(rows):
    if len(rows) > 0xFFFF:
        raise RuntimeError(f"{len(rows)} records exceeds the uint16 index space")

    counts = [0] * PREFIX_COUNT
    for zipcode, _, _ in rows:
        counts[int(zipcode[:3])] += 1

    # Cumulative starts, with a terminating entry so the last prefix's length is
    # read the same way as every other prefix's.
    directory = []
    running = 0
    for count in counts:
        directory.append(running)
        running += count
    directory.append(running)

    records = bytearray()
    for zipcode, latitude, longitude in rows:
        records += struct.pack("<Bhh", int(zipcode[3:]),
                               quantize(latitude, zipcode),
                               quantize(longitude, zipcode))

    header = MAGIC + struct.pack("<BBH", VERSION, RECORD_SIZE, len(rows))
    return header + struct.pack(f"<{len(directory)}H", *directory) + bytes(records)


def verify(blob, rows):
    """Looks every source row back up the way the firmware does."""
    if len(blob) != RECORDS_OFFSET + len(rows) * RECORD_SIZE:
        raise RuntimeError(f"Encoded size {len(blob)} does not match {len(rows)} records")
    if blob[:4] != MAGIC:
        raise RuntimeError("Encoded header is missing its magic")
    version, record_size, count = struct.unpack_from("<BBH", blob, 4)
    if (version, record_size, count) != (VERSION, RECORD_SIZE, len(rows)):
        raise RuntimeError(f"Encoded header {version}/{record_size}/{count} is wrong")

    for zipcode, latitude, longitude in rows:
        start, end = struct.unpack_from("<HH", blob, HEADER_SIZE + int(zipcode[:3]) * 2)
        suffix = int(zipcode[3:])
        found = None
        for index in range(start, end):
            stored, stored_lat, stored_lon = struct.unpack_from(
                "<Bhh", blob, RECORDS_OFFSET + index * RECORD_SIZE)
            if stored == suffix:
                found = (stored_lat / SCALE, stored_lon / SCALE)
                break
        if found is None:
            raise RuntimeError(f"{zipcode}: not found in the encoded table")
        if (abs(found[0] - latitude) > TOLERANCE) or (abs(found[1] - longitude) > TOLERANCE):
            raise RuntimeError(f"{zipcode}: decoded {found} but source is ({latitude}, {longitude})")


def build(project):
    source = project / "assets" / "zipcodes.csv"
    output = project / "data" / "zipcodes.bin"

    if output.exists() and (output.stat().st_mtime >= source.stat().st_mtime):
        print(f"Zipcode table up to date: {output.name} ({output.stat().st_size} bytes)")
        return

    rows = read_source(source)
    blob = encode(rows)
    verify(blob, rows)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(blob)
    print(f"Zipcode table: {len(rows)} records, {len(blob)} bytes "
          f"(from {source.stat().st_size} bytes of CSV)")


try:
    Import("env")  # noqa: F821 - injected by PlatformIO when run as a pre-script.
    build(pathlib.Path(env.subst("$PROJECT_DIR")))  # noqa: F821
except NameError:
    # Run directly to regenerate and re-verify the table without a build.
    sys.exit(build(pathlib.Path(__file__).resolve().parent.parent))
