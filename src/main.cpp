#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <vector>

#include <GxEPD2_BW.h>
#if defined(EPD_PANEL_UC8176) && EPD_PANEL_UC8176
#include <epd/GxEPD2_420_M01.h>
#else
#include <epd/GxEPD2_420.h>
#endif
#include <U8g2_for_Adafruit_GFX.h>

#include "board_pins.h"

#ifndef EPD_PANEL_UC8176
#define EPD_PANEL_UC8176 0
#endif

#if EPD_PANEL_UC8176
using EpdPanel = GxEPD2_420_M01;
#else
using EpdPanel = GxEPD2_420;
#endif

GxEPD2_BW<EpdPanel, EpdPanel::HEIGHT> display(EpdPanel(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

static constexpr int SCREEN_W = 400;
static constexpr int SCREEN_H = 300;
static constexpr int HEADER_H = 24;
static constexpr int MARGIN_X = 8;
static constexpr int MARGIN_Y = 6;
static constexpr int LINE_GAP = 2;
static constexpr int CONTENT_Y = HEADER_H + MARGIN_Y;
static constexpr int CONTENT_AREA_Y = HEADER_H;
static constexpr int CONTENT_AREA_H = SCREEN_H - HEADER_H;
static constexpr int CONTENT_W = SCREEN_W - MARGIN_X * 2;
static constexpr int CONTENT_H = SCREEN_H - CONTENT_Y - MARGIN_Y;

static constexpr char BOOK_PATH[] = "/book.txt";

static constexpr uint32_t DEBOUNCE_MS = 30;

enum class PageAction {
  None,
  Prev,
  Next,
};

struct ButtonState {
  int pin;
  bool stable;
  bool reading;
  uint32_t lastReadMs;
};

ButtonState buttons[] = {
    {BTN_PREV, false, false, 0},
    {BTN_NEXT, false, false, 0},
};

struct RenderResult {
  uint32_t nextOffset;
  bool eof;
};

static std::vector<uint32_t> pageOffsets;
static size_t currentPage = 0;
static uint32_t bookSize = 0;
static int16_t lineHeight = 0;
static int16_t contentTop = 0;
static int16_t linesPerPage = 0;

void setLed(bool on) {
  if (LED_PIN < 0) {
    return;
  }
  bool active = LED_ACTIVE_LOW ? !on : on;
  digitalWrite(LED_PIN, active ? HIGH : LOW);
}

void blinkLed() {
  if (LED_PIN < 0) {
    return;
  }
  setLed(true);
  delay(40);
  setLed(false);
}

uint8_t utf8CharLen(uint8_t lead) {
  if ((lead & 0x80) == 0) {
    return 1;
  }
  if ((lead & 0xE0) == 0xC0) {
    return 2;
  }
  if ((lead & 0xF0) == 0xE0) {
    return 3;
  }
  if ((lead & 0xF8) == 0xF0) {
    return 4;
  }
  return 1;
}

bool readUtf8Char(File &file, char *out, size_t &outLen) {
  int first = file.read();
  if (first < 0) {
    return false;
  }
  uint8_t lead = static_cast<uint8_t>(first);
  size_t len = utf8CharLen(lead);
  out[0] = static_cast<char>(lead);
  outLen = 1;
  for (size_t i = 1; i < len; ++i) {
    int next = file.read();
    if (next < 0) {
      break;
    }
    out[i] = static_cast<char>(next);
    outLen++;
  }
  out[outLen] = '\0';
  return true;
}

PageAction pollButtons() {
  uint32_t now = millis();
  for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    bool current = digitalRead(buttons[i].pin) == LOW;
    if (current != buttons[i].reading) {
      buttons[i].reading = current;
      buttons[i].lastReadMs = now;
    }

    if ((now - buttons[i].lastReadMs) > DEBOUNCE_MS && buttons[i].stable != buttons[i].reading) {
      buttons[i].stable = buttons[i].reading;
      if (!buttons[i].stable) {
        return (i == 0) ? PageAction::Prev : PageAction::Next;
      }
    }
  }

  return PageAction::None;
}

void drawError(const char *message) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_profont11_mr);
    u8g2Fonts.setCursor(MARGIN_X, 20);
    u8g2Fonts.print("ERROR:");
    u8g2Fonts.setCursor(MARGIN_X, 36);
    u8g2Fonts.print(message);
  } while (display.nextPage());
}

void drawHeader() {
  display.fillRect(0, 0, SCREEN_W, HEADER_H, GxEPD_BLACK);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
  u8g2Fonts.setFont(u8g2_font_profont11_mr);
  u8g2Fonts.setCursor(MARGIN_X, 16);
  u8g2Fonts.print("EBOOK");
}

RenderResult renderPage(uint32_t startOffset, bool draw) {
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  File file = LittleFS.open(BOOK_PATH, "r");
  if (!file) {
    return {startOffset, true};
  }

  if (!file.seek(startOffset, SeekSet)) {
    file.close();
    return {startOffset, true};
  }

  int16_t y = contentTop + lineHeight;
  int lines = 0;
  uint32_t offset = startOffset;
  String line;
  line.reserve(256);

  while (lines < linesPerPage) {
    line = "";
    bool hadChar = false;

    while (true) {
      char ch[5] = {0};
      size_t chLen = 0;
      if (!readUtf8Char(file, ch, chLen)) {
        break;
      }
      offset = file.position();
      if (ch[0] == '\n') {
        break;
      }
      if (ch[0] == '\r') {
        continue;
      }
      hadChar = true;
      line += ch;
      if (u8g2Fonts.getUTF8Width(line.c_str()) > CONTENT_W) {
        if (line.length() > chLen) {
          line.remove(line.length() - chLen);
          file.seek(static_cast<int32_t>(file.position()) - static_cast<int32_t>(chLen), SeekSet);
          offset = file.position();
        }
        break;
      }
    }

    if (!hadChar && line.length() == 0 && offset >= bookSize) {
      break;
    }

    if (draw) {
      u8g2Fonts.setCursor(MARGIN_X, y);
      u8g2Fonts.print(line);
    }

    y += lineHeight;
    lines++;

    if (offset >= bookSize) {
      break;
    }
  }

  file.close();
  return {offset, offset >= bookSize};
}

void drawContent(size_t pageIndex) {
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  renderPage(pageOffsets[pageIndex], true);
}

void drawPageFull(size_t pageIndex) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHeader();
    display.fillRect(0, CONTENT_AREA_Y, SCREEN_W, CONTENT_AREA_H, GxEPD_WHITE);
    drawContent(pageIndex);
  } while (display.nextPage());
}

void drawPagePartial(size_t pageIndex) {
  display.setPartialWindow(0, CONTENT_AREA_Y, SCREEN_W, CONTENT_AREA_H);
  display.firstPage();
  do {
    display.fillRect(0, CONTENT_AREA_Y, SCREEN_W, CONTENT_AREA_H, GxEPD_WHITE);
    drawContent(pageIndex);
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  if (LED_PIN >= 0) {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
  }

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200);
  display.setRotation(0);
  u8g2Fonts.begin(display);
  u8g2Fonts.setFontMode(1);

  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  int16_t ascent = u8g2Fonts.getFontAscent();
  int16_t descent = u8g2Fonts.getFontDescent();
  lineHeight = ascent - descent + LINE_GAP;
  contentTop = CONTENT_Y;
  if (lineHeight <= 0) {
    drawError("Font metrics invalid");
    return;
  }
  linesPerPage = CONTENT_H / lineHeight;
  if (linesPerPage <= 0) {
    drawError("Layout too small");
    return;
  }

  uint32_t now = millis();
  for (auto &button : buttons) {
    bool current = digitalRead(button.pin) == LOW;
    button.stable = current;
    button.reading = current;
    button.lastReadMs = now;
  }

  if (!LittleFS.begin()) {
    drawError("LittleFS mount failed");
    return;
  }

  File book = LittleFS.open(BOOK_PATH, "r");
  if (!book) {
    drawError("Missing /book.txt");
    return;
  }
  bookSize = static_cast<uint32_t>(book.size());
  book.close();

  pageOffsets.reserve(256);
  pageOffsets.push_back(0);
  currentPage = 0;
  drawPageFull(currentPage);
}

void loop() {
  PageAction action = pollButtons();
  bool pageChanged = false;

  if (action != PageAction::None) {
    blinkLed();
  }

  if (action == PageAction::Prev && currentPage > 0) {
    currentPage--;
    pageChanged = true;
  }

  if (action == PageAction::Next) {
    if (currentPage + 1 < pageOffsets.size()) {
      currentPage++;
      pageChanged = true;
    } else {
      RenderResult result = renderPage(pageOffsets[currentPage], false);
      if (result.nextOffset < bookSize && result.nextOffset > pageOffsets[currentPage]) {
        pageOffsets.push_back(result.nextOffset);
        currentPage++;
        pageChanged = true;
      }
    }
  }

  if (pageChanged) {
    drawPagePartial(currentPage);
  }

  delay(20);
}
