from pathlib import Path

try:
    Import("env")
    PROJECT_DIR = Path(env["PROJECT_DIR"])
except Exception:
    PROJECT_DIR = Path(__file__).resolve().parents[1]

DATA_DIR = PROJECT_DIR / "data"
MANIFEST_FILE = DATA_DIR / "books.txt"
MAX_BOOKS = 2
MAX_CHARS_PER_LINE = 24  # Keep in sync with firmware.


def pick_source_files():
    candidates = [p for p in PROJECT_DIR.glob("*.txt") if not p.name.startswith("~$")]
    if not candidates:
        raise RuntimeError("No .txt files found in project root.")
    candidates.sort(key=lambda p: p.name.lower())
    return candidates[:MAX_BOOKS]


def needs_update(src: Path, dst: Path) -> bool:
    if not dst.exists():
        return True
    return src.stat().st_mtime > dst.stat().st_mtime


def wrap_text(fin, fout, max_chars: int) -> None:
    for raw_line in fin:
        line = raw_line.rstrip("\r\n")
        if not line:
            fout.write("\n")
            continue
        buf = []
        count = 0
        for ch in line:
            if ch == "\t":
                ch = " "
            buf.append(ch)
            count += 1
            if count >= max_chars:
                fout.write("".join(buf))
                fout.write("\n")
                buf = []
                count = 0
        if buf:
            fout.write("".join(buf))
            fout.write("\n")


def convert_gb18030_to_utf8(src: Path, dst: Path) -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    with src.open("r", encoding="gb18030") as fin, dst.open("w", encoding="utf-8", newline="\n") as fout:
        wrap_text(fin, fout, MAX_CHARS_PER_LINE)


def write_manifest(entries) -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    with MANIFEST_FILE.open("w", encoding="utf-8", newline="\n") as fout:
        for dst_name, title in entries:
            fout.write(f"{dst_name}|{title}\n")


sources = pick_source_files()
manifest_entries = []
for i, source in enumerate(sources):
    dst_name = f"book{i + 1}.txt"
    output = DATA_DIR / dst_name
    title = source.stem
    if needs_update(source, output):
        print(f"[ebook] Converting {source.name} -> data/{dst_name}")
        convert_gb18030_to_utf8(source, output)
    manifest_entries.append((dst_name, title))

write_manifest(manifest_entries)
