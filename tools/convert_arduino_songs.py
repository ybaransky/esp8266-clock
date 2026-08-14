"""Convert robsoncouto/arduino-songs sketches to SoundCloc JSON files."""

import json
import re
import sys
from pathlib import Path

SOURCE = "robsoncouto/arduino-songs"
MELODY_PATTERN = re.compile(
    r"(?:const\s+)?(?:PROGMEM\s+)?int\s+melody\[\]"
    r"(?:\s+PROGMEM)?\s*=\s*\{(.*?)\};",
    re.DOTALL,
)


def convert(sketch: Path) -> tuple[str, dict]:
    text = sketch.read_text(encoding="utf-8-sig", errors="replace")
    title_match = re.search(r"/\*\s*\r?\n\s*([^\r\n]+)", text)
    title = title_match.group(1).strip() if title_match else sketch.stem
    tempo = int(re.search(r"\bint\s+tempo\s*=\s*(\d+)", text).group(1))
    definitions = {
        name: int(value)
        for name, value in re.findall(
            r"#define\s+(NOTE_[A-Z0-9]+|REST)\s+(-?\d+)", text
        )
    }
    body = MELODY_PATTERN.search(text).group(1)
    body = re.sub(r"/\*.*?\*/|//[^\r\n]*", "", body, flags=re.DOTALL)
    tokens = [token.strip() for token in body.split(",") if token.strip()]
    notes = [
        definitions.get(tokens[index], int(tokens[index])
                        if re.fullmatch(r"-?\d+", tokens[index]) else 0)
        for index in range(0, len(tokens), 2)
    ]
    durations = [int(tokens[index]) for index in range(1, len(tokens), 2)]
    return title, {
        "name": title,
        "tempo": tempo,
        "notes": notes,
        "durations": durations,
        "source": SOURCE,
    }


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: convert_arduino_songs.py REPOSITORY DATA_DIR")

    repository = Path(sys.argv[1])
    data_dir = Path(sys.argv[2])

    # Only the song files are written. sounds.json holds the small built-in
    # sounds and nothing else; pack_songs.py picks up songs/*.json on its own,
    # and an index entry here would collide with the song's own name.
    songs_dir = data_dir / "songs"
    songs_dir.mkdir(exist_ok=True)
    converted = 0
    for sketch in sorted(repository.rglob("*.ino")):
        _, song = convert(sketch)
        filename = sketch.parent.name.lower() + ".json"
        (songs_dir / filename).write_text(
            json.dumps(song, separators=(",", ":")) + "\n", encoding="utf-8"
        )
        converted += 1

    print(f"Converted {converted} songs into {songs_dir}")


if __name__ == "__main__":
    main()
