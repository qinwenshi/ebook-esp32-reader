# ebook-esp32-reader

ESP32 + e-paper ebook viewer using PlatformIO.

## Usage

1. Put up to two GB18030-encoded `.txt` books in the project root.
2. Upload the filesystem (converts to UTF-8 for rendering):

   pio run -t uploadfs

3. Flash firmware:

   pio run -t upload

The `tools/prepare_book.py` script picks up to two `.txt` files in the
project root (sorted by filename, ignoring `~$` temp files), converts them
to UTF-8, wraps lines to a fixed width (24 chars), writes `data/book1.txt`,
`data/book2.txt`, and creates `data/books.txt` for the on-device list.
The firmware uses a monospaced font to match this wrapping.
On first open, the device builds a page index cache for each book.
To reduce ghosting, the firmware performs a full refresh after several
partial updates.

## Buttons

- BTN_PREV: previous page
- BTN_NEXT: next page
- BTN_MENU: open/close book, enter list

Pins are defined in `include/board_pins.h`.
