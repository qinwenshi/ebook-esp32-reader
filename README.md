# ebook-esp32-reader

ESP32 + e-paper ebook viewer using PlatformIO.

## Usage

1. Put the GB18030-encoded book `.txt` in the project root.
2. Upload the filesystem (converts to UTF-8 for rendering):

   pio run -t uploadfs

3. Flash firmware:

   pio run -t upload

The `tools/prepare_book.py` script picks the largest `.txt` in the project
root (ignoring `~$` temp files) and writes `data/book.txt`.

## Buttons

- BTN_PREV: previous page
- BTN_NEXT: next page

Pins are defined in `include/board_pins.h`.
