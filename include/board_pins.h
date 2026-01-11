#pragma once

// E-paper SPI pins from the board silkscreen in the provided photo.
// Adjust here if your wiring differs.
static constexpr int EPD_CS = 3;
static constexpr int EPD_DC = 4;
static constexpr int EPD_RST = 5;
static constexpr int EPD_SCK = 6;
static constexpr int EPD_MOSI = 7;
static constexpr int EPD_BUSY = 8;
static constexpr int EPD_CS2 = 13; // Not used.

// Button pins. Avoid IO9 (BOOT).
static constexpr int BTN_PREV = 1;
static constexpr int BTN_NEXT = 20;

#ifdef LED_BUILTIN
static constexpr int LED_PIN = LED_BUILTIN;
#else
static constexpr int LED_PIN = -1;
#endif
static constexpr bool LED_ACTIVE_LOW = false;
