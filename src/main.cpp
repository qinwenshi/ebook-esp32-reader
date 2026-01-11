#include <Arduino.h>
#include <SPI.h>
#include <LittleFS.h>
#include <Preferences.h>
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

static constexpr char BOOKS_MANIFEST[] = "/books.txt";
static constexpr uint8_t MAX_BOOKS = 2;

static constexpr uint32_t DEBOUNCE_MS = 30;

enum class PageAction {
  None,
  Prev,
  Next,
  Menu,
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
    {BTN_MENU, false, false, 0},
};

struct RenderResult {
  uint32_t nextOffset;
  bool eof;
};

struct BookInfo {
  String path;
  String title;
  uint32_t size;
  uint32_t savedOffset;
};

enum class UiMode {
  List,
  Reading,
};

static BookInfo books[MAX_BOOKS];
static size_t bookCount = 0;
static size_t selectedBook = 0;
static size_t currentBook = 0;
static UiMode uiMode = UiMode::List;
static Preferences prefs;

static std::vector<uint32_t> pageOffsets;
static size_t currentPage = 0;
static uint32_t currentBookSize = 0;
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

String truncateUtf8(const String &text, size_t maxChars) {
  const char *data = text.c_str();
  size_t len = text.length();
  size_t index = 0;
  size_t count = 0;
  while (index < len && count < maxChars) {
    index += utf8CharLen(static_cast<uint8_t>(data[index]));
    count++;
  }
  if (index < len) {
    String out = text.substring(0, index);
    out += "...";
    return out;
  }
  return text;
}

void progressKey(size_t index, char *out, size_t outLen) {
  snprintf(out, outLen, "b%u", static_cast<unsigned>(index));
}

bool addBook(const String &path, const String &title) {
  if (bookCount >= MAX_BOOKS) {
    return false;
  }
  if (!LittleFS.exists(path)) {
    return false;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }
  books[bookCount].path = path;
  books[bookCount].title = title;
  books[bookCount].size = static_cast<uint32_t>(file.size());
  books[bookCount].savedOffset = 0;
  file.close();
  bookCount++;
  return true;
}

void loadBooks() {
  bookCount = 0;
  File manifest = LittleFS.open(BOOKS_MANIFEST, "r");
  if (manifest) {
    while (manifest.available() && bookCount < MAX_BOOKS) {
      String line = manifest.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) {
        continue;
      }
      int sep = line.indexOf('|');
      String path = (sep >= 0) ? line.substring(0, sep) : line;
      String title = (sep >= 0) ? line.substring(sep + 1) : line;
      path.trim();
      title.trim();
      if (!path.startsWith("/")) {
        path = "/" + path;
      }
      if (title.length() == 0) {
        title = "Book";
      }
      addBook(path, title);
    }
    manifest.close();
  }

  if (bookCount == 0) {
    if (!addBook("/book1.txt", "Book 1")) {
      addBook("/book.txt", "Book 1");
    }
  }
}

void loadProgress() {
  for (size_t i = 0; i < bookCount; ++i) {
    char key[8] = {0};
    progressKey(i, key, sizeof(key));
    uint32_t saved = prefs.getUInt(key, 0);
    books[i].savedOffset = (saved < books[i].size) ? saved : 0;
  }
  uint8_t last = prefs.getUChar("last", 0);
  selectedBook = (last < bookCount) ? last : 0;
}

void saveProgress(size_t index, uint32_t offset) {
  if (index >= bookCount) {
    return;
  }
  char key[8] = {0};
  progressKey(index, key, sizeof(key));
  prefs.putUInt(key, offset);
  books[index].savedOffset = offset;
  prefs.putUChar("last", static_cast<uint8_t>(index));
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
        if (i == 0) {
          return PageAction::Prev;
        }
        if (i == 1) {
          return PageAction::Next;
        }
        return PageAction::Menu;
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

void drawHeader(const String &title) {
  display.fillRect(0, 0, SCREEN_W, HEADER_H, GxEPD_BLACK);
  u8g2Fonts.setForegroundColor(GxEPD_WHITE);
  u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2Fonts.setCursor(MARGIN_X, 18);
  if (title.length() == 0) {
    u8g2Fonts.print("EBOOK");
  } else {
    String headerTitle = truncateUtf8(title, 18);
    u8g2Fonts.print(headerTitle);
  }
}

void drawHeaderPartial(const String &title) {
  display.setPartialWindow(0, 0, SCREEN_W, HEADER_H);
  display.firstPage();
  do {
    drawHeader(title);
  } while (display.nextPage());
}

RenderResult renderPage(const char *path, uint32_t startOffset, bool draw) {
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  File file = LittleFS.open(path, "r");
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

    if (!hadChar && line.length() == 0 && offset >= currentBookSize) {
      break;
    }

    if (draw) {
      u8g2Fonts.setCursor(MARGIN_X, y);
      u8g2Fonts.print(line);
    }

    y += lineHeight;
    lines++;

    if (offset >= currentBookSize) {
      break;
    }
  }

  file.close();
  return {offset, offset >= currentBookSize};
}

void drawContent(size_t pageIndex) {
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  renderPage(books[currentBook].path.c_str(), pageOffsets[pageIndex], true);
}

void drawPageFull(size_t pageIndex) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawHeader(books[currentBook].title);
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

void drawBookListContent() {
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  int16_t y = contentTop + lineHeight;
  for (size_t i = 0; i < bookCount; ++i) {
    String title = truncateUtf8(books[i].title, 16);
    u8g2Fonts.setCursor(MARGIN_X, y);
    u8g2Fonts.print((i == selectedBook) ? "> " : "  ");
    u8g2Fonts.print(i + 1);
    u8g2Fonts.print(". ");
    u8g2Fonts.print(title);
    y += lineHeight;
  }
  u8g2Fonts.setFont(u8g2_font_profont11_mr);
  u8g2Fonts.setCursor(MARGIN_X, SCREEN_H - 6);
  u8g2Fonts.print("[PREV/NEXT] SELECT  [MENU] OPEN");
}

void drawBookListPartial() {
  display.setPartialWindow(0, CONTENT_AREA_Y, SCREEN_W, CONTENT_AREA_H);
  display.firstPage();
  do {
    display.fillRect(0, CONTENT_AREA_Y, SCREEN_W, CONTENT_AREA_H, GxEPD_WHITE);
    drawBookListContent();
  } while (display.nextPage());
}

void buildOffsetsTo(uint32_t targetOffset) {
  pageOffsets.clear();
  pageOffsets.push_back(0);
  currentPage = 0;
  if (targetOffset == 0 || currentBookSize == 0) {
    return;
  }
  while (pageOffsets.back() < targetOffset) {
    RenderResult result = renderPage(books[currentBook].path.c_str(), pageOffsets.back(), false);
    if (result.nextOffset <= pageOffsets.back()) {
      break;
    }
    pageOffsets.push_back(result.nextOffset);
    if (result.nextOffset >= targetOffset) {
      break;
    }
    delay(0);
  }
  if (pageOffsets.size() > 1 && pageOffsets.back() > targetOffset) {
    currentPage = pageOffsets.size() - 2;
  } else {
    currentPage = pageOffsets.size() - 1;
  }
}

void openBook(size_t index) {
  if (index >= bookCount) {
    return;
  }
  currentBook = index;
  currentBookSize = books[index].size;
  if (currentBookSize == 0) {
    drawError("Empty book");
    return;
  }
  buildOffsetsTo(books[index].savedOffset);
  uiMode = UiMode::Reading;
  drawPageFull(currentPage);
  saveProgress(currentBook, pageOffsets[currentPage]);
}

void enterListMode() {
  uiMode = UiMode::List;
  drawHeaderPartial("LIBRARY");
  drawBookListPartial();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);
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
  prefs.begin("ebook", false);
  loadBooks();
  if (bookCount == 0) {
    drawError("No books found");
    return;
  }
  loadProgress();

  pageOffsets.reserve(256);
  enterListMode();
}

void loop() {
  PageAction action = pollButtons();
  if (action == PageAction::None) {
    delay(20);
    return;
  }

  blinkLed();
  if (uiMode == UiMode::List) {
    if (action == PageAction::Prev && selectedBook > 0) {
      selectedBook--;
      drawBookListPartial();
    } else if (action == PageAction::Next && selectedBook + 1 < bookCount) {
      selectedBook++;
      drawBookListPartial();
    } else if (action == PageAction::Menu) {
      openBook(selectedBook);
    }
    delay(20);
    return;
  }

  bool pageChanged = false;
  if (action == PageAction::Menu) {
    saveProgress(currentBook, pageOffsets[currentPage]);
    enterListMode();
    delay(20);
    return;
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
      RenderResult result = renderPage(books[currentBook].path.c_str(), pageOffsets[currentPage], false);
      if (result.nextOffset < currentBookSize && result.nextOffset > pageOffsets[currentPage]) {
        pageOffsets.push_back(result.nextOffset);
        currentPage++;
        pageChanged = true;
      }
    }
  }

  if (pageChanged) {
    drawPagePartial(currentPage);
    saveProgress(currentBook, pageOffsets[currentPage]);
  }

  delay(20);
}
