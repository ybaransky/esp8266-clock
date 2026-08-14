"""Pack the song JSON sources into the single songs.bin the device reads.

Packs assets/sounds.json and assets/songs/*.json into data/songs.bin, the
catalog that ships on LittleFS and that src/sound_player.cpp reads. This script
and sound_player.cpp are the only two places the binary layout is spelled out;
change them together.

Why one file: LittleFS charges a whole 8192-byte block per file, so 28 small
JSON sources would cost ~229 KB of flash to store ~26 KB of notes. One packed
file costs ~32 KB, and playing a song streams notes two bytes at a time straight
from flash instead of parsing JSON into RAM.

The sources live under assets/ rather than data/ - the same split the ZIP code
table uses - so the JSON never reaches the device.

Format (little endian), see SOUNDS.md:

    magic   "SNG1"
    u8      version (1)
    u8      songCount
    u8      pitchCount
    u8      reserved (0)
    u16     pitchTable[pitchCount]        distinct frequencies in Hz, 0 = rest
    directory, songCount entries in name order:
        u16 offset                        absolute file offset of the record
        u16 tempo                         0 = simple timing mode
        u16 noteCount
        u8  nameLength
        u8  name[nameLength]              UTF-8
    records:
        { u8 pitchIndex, s8 durationDivisor } * noteCount

Usage:
    pack_songs.py                 rebuild data/songs.bin if the sources changed
    pack_songs.py list  [BIN]     show what is in a packed catalog
    pack_songs.py dump  [BIN] NAME   reconstruct one song's JSON
"""

import json
import pathlib
import struct
import sys

MAGIC = b"SNG1"
VERSION = 1
HEADER = struct.Struct("<4sBBBB")
DIRECTORY_ENTRY = struct.Struct("<HHHB")
NOTE = struct.Struct("<Bb")

# The device caches the pitch table in RAM (2 bytes per entry), and song and
# pitch indexes are single bytes. Both limits are enormous next to real data:
# 28 songs use 50 distinct pitches today.
MAX_SONGS = 255
MAX_PITCHES = 128

# A sound is selected by name in /config.json, and SoundConfig stores those
# names in fixed-size fields. Enforcing the limit here means a name that would
# be silently truncated on save fails the build instead of half-matching
# nothing at run time. Must match kSoundNameLength in src/config.h.
MAX_NAME_BYTES = 47

BLOCK_SIZE = 8192  # LittleFS block on this board.
BUDGET = 100 * 1024


class PackError(Exception):
    """A song source is unusable. Reported as a build failure, never skipped."""


def read_json(path):
    """Read one JSON source, tolerating the BOM some Windows editors write."""
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except ValueError as error:
        raise PackError("%s: %s" % (path, error))


def source_paths(assets_dir):
    """Every file the packed catalog is built from, in a stable order."""
    paths = []
    catalog = assets_dir / "sounds.json"
    if catalog.exists():
        paths.append(catalog)
    paths.extend(sorted((assets_dir / "songs").glob("*.json")))
    return paths


def load_songs(assets_dir):
    """Read every song source. Returns a list of dicts sorted by name.

    Both the small-sound catalog (sounds.json) and the one-file-per-song
    directory feed the same packed catalog; the device does not distinguish
    between them.
    """
    songs = []

    catalog_path = assets_dir / "sounds.json"
    if catalog_path.exists():
        for sound in read_json(catalog_path).get("sounds", []):
            songs.append((catalog_path, sound))

    for path in sorted((assets_dir / "songs").glob("*.json")):
        songs.append((path, read_json(path)))

    validated = [validate(path, song) for path, song in songs]

    names = {}
    for song in validated:
        if song["name"] in names:
            raise PackError(
                "duplicate song name %r in %s and %s"
                % (song["name"], names[song["name"]], song["path"])
            )
        names[song["name"]] = song["path"]

    validated.sort(key=lambda song: song["name"])
    return validated


def validate(path, song):
    """Reject anything the firmware could not play, naming the offending file."""
    name = song.get("name")
    if not isinstance(name, str) or not name.strip():
        raise PackError("%s: missing or empty 'name'" % path)
    encoded = name.encode("utf-8")
    if len(encoded) > MAX_NAME_BYTES:
        raise PackError(
            "%s: name is %d bytes; the firmware's SoundConfig fields hold %d"
            % (path, len(encoded), MAX_NAME_BYTES)
        )

    notes = song.get("notes")
    durations = song.get("durations")
    if not isinstance(notes, list) or not notes:
        raise PackError("%s: 'notes' must be a non-empty array" % path)
    if not isinstance(durations, list):
        raise PackError("%s: 'durations' must be an array" % path)
    if len(notes) != len(durations):
        raise PackError(
            "%s: %d notes but %d durations; they must match"
            % (path, len(notes), len(durations))
        )
    if len(notes) > 0xFFFF:
        raise PackError("%s: more than %d notes" % (path, 0xFFFF))

    for index, frequency in enumerate(notes):
        if not isinstance(frequency, int) or isinstance(frequency, bool):
            raise PackError("%s: note %d is not an integer" % (path, index))
        if not 0 <= frequency <= 0xFFFF:
            raise PackError(
                "%s: note %d is %d Hz, outside 0..65535" % (path, index, frequency)
            )
    for index, divisor in enumerate(durations):
        if not isinstance(divisor, int) or isinstance(divisor, bool):
            raise PackError("%s: duration %d is not an integer" % (path, index))
        if not -128 <= divisor <= 127:
            raise PackError(
                "%s: duration %d is %d, outside -128..127" % (path, index, divisor)
            )

    tempo = song.get("tempo", 0)
    if not isinstance(tempo, int) or isinstance(tempo, bool) or not 0 <= tempo <= 0xFFFF:
        raise PackError("%s: 'tempo' must be an integer in 0..65535" % path)

    return {
        "path": path,
        "name": name,
        "encoded_name": encoded,
        "tempo": tempo,
        "notes": notes,
        "durations": durations,
    }


def build(songs):
    """Serialize the catalog. Offsets are resolved in a second pass."""
    if len(songs) > MAX_SONGS:
        raise PackError("%d songs; the format holds %d" % (len(songs), MAX_SONGS))

    pitches = sorted({frequency for song in songs for frequency in song["notes"]})
    if len(pitches) > MAX_PITCHES:
        raise PackError(
            "%d distinct frequencies; the format holds %d. Round some notes to "
            "shared values or raise MAX_PITCHES here and kPitchTableMax in "
            "sound_player.cpp together." % (len(pitches), MAX_PITCHES)
        )
    pitch_index = {frequency: index for index, frequency in enumerate(pitches)}

    directory_size = sum(
        DIRECTORY_ENTRY.size + len(song["encoded_name"]) for song in songs
    )
    records_start = HEADER.size + 2 * len(pitches) + directory_size

    directory = bytearray()
    records = bytearray()
    for song in songs:
        offset = records_start + len(records)
        if offset > 0xFFFF:
            raise PackError(
                "catalog exceeds 64 KB at %r; offsets are 16-bit" % song["name"]
            )
        directory += DIRECTORY_ENTRY.pack(
            offset, song["tempo"], len(song["notes"]), len(song["encoded_name"])
        )
        directory += song["encoded_name"]
        for frequency, divisor in zip(song["notes"], song["durations"]):
            records += NOTE.pack(pitch_index[frequency], divisor)

    image = bytearray(HEADER.pack(MAGIC, VERSION, len(songs), len(pitches), 0))
    for frequency in pitches:
        image += struct.pack("<H", frequency)
    image += directory
    image += records
    return bytes(image), pitches


def read_catalog(image):
    """Parse a packed image back into songs. Used by verify, 'list' and 'dump'."""
    magic, version, song_count, pitch_count, _ = HEADER.unpack_from(image, 0)
    if magic != MAGIC:
        raise PackError("not a songs.bin (bad magic %r)" % magic)
    if version != VERSION:
        raise PackError("unsupported version %d" % version)

    cursor = HEADER.size
    pitches = list(struct.unpack_from("<%dH" % pitch_count, image, cursor))
    cursor += 2 * pitch_count

    songs = []
    for _ in range(song_count):
        offset, tempo, note_count, name_length = DIRECTORY_ENTRY.unpack_from(
            image, cursor
        )
        cursor += DIRECTORY_ENTRY.size
        name = image[cursor:cursor + name_length].decode("utf-8")
        cursor += name_length
        songs.append(
            {"name": name, "tempo": tempo, "note_count": note_count, "offset": offset}
        )

    for song in songs:
        notes, durations = [], []
        at = song["offset"]
        for _ in range(song["note_count"]):
            pitch, divisor = NOTE.unpack_from(image, at)
            at += NOTE.size
            notes.append(pitches[pitch])
            durations.append(divisor)
        song["notes"] = notes
        song["durations"] = durations

    return songs, pitches


def verify(packed, songs):
    """Read the image back the way the firmware does and compare every note."""
    decoded, _ = read_catalog(packed)
    if len(decoded) != len(songs):
        raise PackError(
            "encoded %d songs but decoded %d" % (len(songs), len(decoded))
        )
    for source, result in zip(songs, decoded):
        if result["name"] != source["name"]:
            raise PackError(
                "directory order broke: expected %r, decoded %r"
                % (source["name"], result["name"])
            )
        if result["tempo"] != source["tempo"]:
            raise PackError("%s: tempo did not survive packing" % source["name"])
        if result["notes"] != source["notes"]:
            raise PackError("%s: notes did not survive packing" % source["name"])
        if result["durations"] != source["durations"]:
            raise PackError("%s: durations did not survive packing" % source["name"])


def report(packed, songs, pitches, source_bytes, source_files):
    blocks = -(-len(packed) // BLOCK_SIZE)
    print(
        "Song catalog: %d songs, %d notes, %d distinct pitches"
        % (len(songs), sum(len(song["notes"]) for song in songs), len(pitches))
    )
    print(
        "  %d bytes packed from %d bytes of JSON in %d files"
        % (len(packed), source_bytes, source_files)
    )
    print(
        "  ~%d blocks (%d bytes) of LittleFS vs ~%d bytes as separate files"
        % (blocks, blocks * BLOCK_SIZE, source_files * BLOCK_SIZE)
    )
    if blocks * BLOCK_SIZE > BUDGET:
        print("  WARNING: over the %d byte budget" % BUDGET)


def build_catalog(project):
    """PlatformIO pre-script entry point: rebuild data/songs.bin when stale."""
    assets_dir = project / "assets"
    output = project / "data" / "songs.bin"

    sources = source_paths(assets_dir)
    if not sources:
        raise PackError("no song sources found under %s" % assets_dir)
    if output.exists():
        newest = max(path.stat().st_mtime for path in sources)
        if output.stat().st_mtime >= newest:
            print(
                "Song catalog up to date: %s (%d bytes)"
                % (output.name, output.stat().st_size)
            )
            return

    songs = load_songs(assets_dir)
    packed, pitches = build(songs)
    verify(packed, songs)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(packed)
    report(
        packed,
        songs,
        pitches,
        sum(path.stat().st_size for path in sources),
        len(sources),
    )


def default_image(project):
    return project / "data" / "songs.bin"


def command_list(image_path):
    songs, pitches = read_catalog(image_path.read_bytes())
    print("%-28s %6s %6s %8s" % ("name", "tempo", "notes", "offset"))
    for song in songs:
        print(
            "%-28s %6d %6d %8d"
            % (song["name"], song["tempo"], song["note_count"], song["offset"])
        )
    print("\n%d songs, %d distinct pitches: %s" % (len(songs), len(pitches), pitches))


def command_dump(image_path, name):
    songs, _ = read_catalog(image_path.read_bytes())
    for song in songs:
        if song["name"] == name:
            output = {"name": song["name"], "notes": song["notes"],
                      "durations": song["durations"]}
            if song["tempo"]:
                output["tempo"] = song["tempo"]
            print(json.dumps(output, indent=2))
            return
    raise PackError("no song named %r; try 'list'" % name)


def main(project, argv):
    if len(argv) == 1:
        build_catalog(project)
    elif (len(argv) in (2, 3)) and argv[1] == "list":
        command_list(pathlib.Path(argv[2]) if len(argv) == 3 else default_image(project))
    elif (len(argv) == 4) and argv[1] == "dump":
        command_dump(pathlib.Path(argv[2]), argv[3])
    elif (len(argv) == 3) and argv[1] == "dump":
        command_dump(default_image(project), argv[2])
    else:
        raise SystemExit(__doc__)


try:
    Import("env")  # noqa: F821 - injected by PlatformIO when run as a pre-script.
    try:
        build_catalog(pathlib.Path(env.subst("$PROJECT_DIR")))  # noqa: F821
    except PackError as error:
        raise SystemExit("pack_songs: %s" % error)
except NameError:
    # Run directly to regenerate the catalog, or to inspect an existing one.
    try:
        main(pathlib.Path(__file__).resolve().parent.parent, sys.argv)
    except PackError as error:
        raise SystemExit("pack_songs: %s" % error)
