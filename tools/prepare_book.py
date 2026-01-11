from pathlib import Path

try:
    Import("env")
    PROJECT_DIR = Path(env["PROJECT_DIR"])
except Exception:
    PROJECT_DIR = Path(__file__).resolve().parents[1]
DATA_DIR = PROJECT_DIR / "data"
OUTPUT_FILE = DATA_DIR / "book.txt"


def pick_source_file() -> Path:
    candidates = [p for p in PROJECT_DIR.glob("*.txt") if not p.name.startswith("~$")]
    if not candidates:
        raise RuntimeError("No .txt files found in project root.")
    candidates.sort(key=lambda p: p.stat().st_size, reverse=True)
    return candidates[0]


def needs_update(src: Path, dst: Path) -> bool:
    if not dst.exists():
        return True
    return src.stat().st_mtime > dst.stat().st_mtime


def convert_gb18030_to_utf8(src: Path, dst: Path) -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    with src.open("r", encoding="gb18030") as fin, dst.open("w", encoding="utf-8", newline="") as fout:
        while True:
            chunk = fin.read(4096)
            if not chunk:
                break
            fout.write(chunk)


source = pick_source_file()
if needs_update(source, OUTPUT_FILE):
    print(f"[ebook] Converting {source.name} -> data/book.txt")
    convert_gb18030_to_utf8(source, OUTPUT_FILE)
