/*
 * mini_nfc — Miniature NFC Scanner
 *
 * Scans NFC-tagged miniatures from tabletop games (boardgames, wargames, etc.)
 * and displays the miniature's name, faction, and game on a TFT screen.
 *
 * Tags are NTAG2xx (7-byte UID). Game and faction data is loaded from
 * /games.json on the SD card, making it easy to add new games without
 * reflashing the firmware.
 */

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_STMPE610.h>
#include <SdFat.h>                // SD card & FAT filesystem library
#include <Adafruit_SPIFlash.h>    // SPI / QSPI flash library
#include <Adafruit_ImageReader.h> // Image-reading functions
#include <ArduinoJson.h>          // https://arduinojson.org — install via Library Manager

#define PN532_IRQ   (2)
#define PN532_RESET (3)
#define TFT_CS      (9)
#define TFT_DC      (10)
#define SD_CS       (5)
#define STMPE_CS    (6)

uint8_t determineGame();
uint8_t parseFactionIds(char[], uint8_t);
void showInventoryScreen();
void recordMini(const char[], char[], uint8_t, char*);

// PN5232 breakout board attached via I2C
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// Adafruit 2.4" TFT featherwing
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_STMPE610 ts  = Adafruit_STMPE610(STMPE_CS);

// SD Card and Adafruit ImageReader
// BMP requirements: 24-bit color, uncompressed (no RLE), Windows BMP format (BITMAPV4HEADER).
// Adafruit_ImageReader does not support other depths or compression modes.
// Suggested dimensions: full-screen splash 320x240, game logos up to ~320x100.
SdFat SD;
Adafruit_ImageReader reader(SD);
bool hasStorage = true;

// Cache for loaded BMP images in RAM
int32_t img_width  = 0, 
        img_height = 0;

#define NFCDATASIZE           128
#define NFC_DATA_PAGE_OFFSET  6
#define NFC_PAGE_SIZE         4

#define SCREEN_WIDTH_3        17

uint8_t nfcReadPage;
uint8_t nfcDataPos;
char nfcPageData[NFCDATASIZE + 1];


#define MAX_GAME_COUNT          8
#define MAX_TOTAL_FACTIONS     64
#define MAX_FACTIONS_PER_MINI   3
#define MAX_NAME_LEN           32
#define MAX_CODE_LEN            8

#define GAME_UNKNOWN        99

typedef struct {
  char     id;
  char     name[MAX_NAME_LEN];
  uint16_t color;
} GameFaction;

typedef struct {
  char     name[MAX_NAME_LEN];
  uint16_t color;
  char     code[MAX_CODE_LEN];
  uint8_t  factionStart;
  uint8_t  factionCount;
} GameSystem;

GameFaction ALL_FACTIONS[MAX_TOTAL_FACTIONS];
uint8_t totalFactionCount = 0;
GameSystem GAMES[MAX_GAME_COUNT];
uint8_t gameCount = 0;

int currentGame = -1;
uint8_t miniUid[] = { 0, 0, 0, 0, 0, 0, 0 };

#define MODE_SCAN      0
#define MODE_MENU      1
#define MODE_INVENTORY 2
uint8_t  appMode       = MODE_SCAN;
uint16_t inventoryCount = 0;

// Uncomment to enable touch calibration mode.
// Tap corners/edges of each button and note the raw p.x values,
// then update map() in checkTouch() and remove this define.
//#define CALIBRATE_TOUCH


// Reads /games.json from the SD card and populates GAMES[].
// Increase the DynamicJsonDocument capacity if you add many games/factions.
bool loadGames() {
  SdFile file;
  if (!file.open("/games.json", O_RDONLY)) {
    Serial.println("Failed to open /games.json");
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArray games = doc["games"].as<JsonArray>();
  gameCount = 0;
  totalFactionCount = 0;
  for (JsonObject game : games) {
    if (gameCount >= MAX_GAME_COUNT) break;
    GameSystem &g = GAMES[gameCount];
    strlcpy(g.name, game["name"] | "", sizeof(g.name));
    strlcpy(g.code, game["code"] | "", sizeof(g.code));
    g.color = game["color"] | (uint16_t)0xFFFF;

    JsonArray factions = game["factions"].as<JsonArray>();
    g.factionStart = totalFactionCount;
    g.factionCount = 0;
    for (JsonObject faction : factions) {
      if (totalFactionCount >= MAX_TOTAL_FACTIONS) break;
      GameFaction &f = ALL_FACTIONS[totalFactionCount];
      const char *idStr = faction["id"] | " ";
      f.id = idStr[0];
      strlcpy(f.name, faction["name"] | "", sizeof(f.name));
      f.color = faction["color"] | (uint16_t)0xFFFF;
      g.factionCount++;
      totalFactionCount++;
    }
    gameCount++;
  }

  return gameCount > 0;
}


void setup(void) {
  Serial.begin(115200);
  //while (!Serial) delay(10); 
  delay(200);

  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextWrap(true);

  if (!ts.begin()) {
    tft.setTextColor(ILI9341_RED); tft.setTextSize(2);
    tft.println("Touch init FAILED");
    for (;;);
  }

  delay(10);
  
  tft.setTextColor(ILI9341_DARKGREEN); tft.setTextSize(2);
  tft.println("Display Initialized");
  tft.println("Initializing Storage...");

  if(!SD.begin(SD_CS, SD_SCK_MHZ(10))) { 
    tft.setTextColor(ILI9341_RED);
    tft.println("FAILED");
    hasStorage = false;
    for(;;); // Fatal error, do not continue
  }
  tft.println("Storage Initialized");

  tft.println("Loading game config...");
  if (!loadGames()) {
    tft.setTextColor(ILI9341_RED);
    tft.println("games.json FAILED");
    for(;;);
  }
  tft.print(gameCount); tft.println(" game(s) loaded");

  tft.println("Initializing Scanner...");
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    tft.setTextColor(ILI9341_RED);
    tft.println("FAILED");
    while (1); delay(1000);
  }
  tft.println("Scanner Initialized");

  if(hasStorage) { reader.drawBMP("/scan.bmp", tft, 0, 0); }
}

void unableToScan() {
  Serial.println("show something for scan error");
}

void scanMini() {
  uint8_t uidLength;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 150)) return;
  if (uidLength != 7) return;
  if (sameMini(uid)) return;

  for (uint8_t i = 0; i < 7; i++) miniUid[i] = uid[i];

  if (!nfcReadMiniData()) return;

  // Capture raw tag string before determineGame() null-terminates the ']'
  char rawNfc[NFCDATASIZE];
  strlcpy(rawNfc, &nfcPageData[1], sizeof(rawNfc));

  uint8_t gameId = determineGame();
  if (gameId == GAME_UNKNOWN) return;

  if (appMode == MODE_SCAN) {
    changeGame(gameId);
  } else {
    currentGame = gameId;
  }

  char fids[MAX_FACTIONS_PER_MINI];
  uint8_t fidCount = parseFactionIds(fids, MAX_FACTIONS_PER_MINI);
  char *miniName = getMiniContent();

  if (appMode == MODE_INVENTORY) {
    recordMini(rawNfc, fids, fidCount, miniName);
  } else {
    idMini(fids, fidCount, miniName);
  }
}

void showMenu() {
  const char* labels[3] = { "Scan Mode", "Mode 2", "Inventory Mode" };
  uint16_t    colors[3] = { ILI9341_DARKGREEN, ILI9341_NAVY, ILI9341_MAROON };

  tft.fillScreen(ILI9341_BLACK);
  for (uint8_t i = 0; i < 3; i++) {
    int16_t y = i * 80;
    tft.fillRect(1, y + 1, 318, 78, colors[i]);
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    int16_t labelW = strlen(labels[i]) * 12;
    tft.setCursor((320 - labelW) / 2, y + 32);
    tft.print(labels[i]);
  }
}

void showInventoryScreen() {
  inventoryCount = 0;
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(3);
  tft.setCursor(8, 80);
  tft.println("INVENTORY");
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2);
  tft.setCursor(8, 120);
  tft.println("Scan minis to record.");
}

// Writes a single CSV field, quoting and escaping if it contains , " or newlines.
void writeCsvField(SdFile &file, const char *field) {
  bool needsQuote = false;
  for (const char *p = field; *p; p++) {
    if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') { needsQuote = true; break; }
  }
  if (!needsQuote) { file.print(field); return; }
  file.print('"');
  for (const char *p = field; *p; p++) {
    if (*p == '"') file.print('"'); // double any embedded quote
    file.print(*p);
  }
  file.print('"');
}

void recordMini(const char rawNfc[], char fids[], uint8_t fidCount, char *miniName) {
  if (!hasStorage) return;

  // Create /inventory directory if needed (mkdir returns false if it exists, ignore that)
  if (!SD.exists("/inventory")) SD.mkdir("/inventory");

  char path[24];
  snprintf(path, sizeof(path), "/inventory/%s.csv", GAMES[currentGame].code);

  SdFile file;
  if (!file.open(path, O_WRITE | O_CREAT | O_APPEND)) {
    Serial.print("Failed to open "); Serial.println(path);
    return;
  }

  if (file.fileSize() == 0) file.println("uid,nfc_data");

  char uidStr[22];
  snprintf(uidStr, sizeof(uidStr), "%02X:%02X:%02X:%02X:%02X:%02X:%02X",
           miniUid[0], miniUid[1], miniUid[2], miniUid[3],
           miniUid[4], miniUid[5], miniUid[6]);
  file.print(uidStr);
  file.print(",");
  writeCsvField(file, rawNfc);
  file.println();
  file.close();

  inventoryCount++;

  // Top section: same mini info layout as scan mode
  idMini(fids, fidCount, miniName);

  // Bottom section: inventory status (replaces game logo area)
  tft.fillRect(0, 140, 320, 100, ILI9341_BLACK);
  tft.setTextColor(ILI9341_YELLOW); tft.setTextSize(2);
  tft.setCursor(8, 148);
  tft.println(GAMES[currentGame].name);
  tft.setTextColor(ILI9341_GREEN); tft.setTextSize(1);
  tft.setCursor(8, 185);
  tft.print("Saved to /inventory/"); tft.println(GAMES[currentGame].code);
  tft.setCursor(8, 198);
  tft.print(inventoryCount); tft.println(" recorded this session");
}

void checkTouch() {
  if (!ts.touched()) return;

  // Collect samples for the full duration of the press; keep the latest valid one.
  // Combining read and wait-for-release in one loop prevents post-release x=0
  // artifacts from sitting in the FIFO and poisoning the next touch read.
  TS_Point p;
  bool valid = false;
  uint32_t start = millis();
  while (ts.touched() && millis() - start < 2000) {
    while (!ts.bufferEmpty()) {
      TS_Point s = ts.getPoint();
      if (s.z > 0 && s.x > 0) { p = s; valid = true; }
    }
    delay(10);
  }
  ts.writeRegister8(STMPE_INT_STA, 0xFF);

  if (!valid) return;

#ifdef CALIBRATE_TOUCH
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2);
  tft.setCursor(8, 80);  tft.print("raw x: "); tft.println(p.x);
  tft.setCursor(8, 110); tft.print("raw y: "); tft.println(p.y);
  tft.setCursor(8, 140); tft.print("z:     "); tft.println(p.z);
  Serial.print("raw x="); Serial.print(p.x);
  Serial.print(" y="); Serial.print(p.y);
  Serial.print(" z="); Serial.println(p.z);
  return;
#endif

  if (appMode == MODE_SCAN || appMode == MODE_INVENTORY) {
    appMode = MODE_MENU;
    showMenu();
    return;
  }

  // p.x maps to screen Y in rotation 3 (raw ~150–3900 → screen 0–239)
  // Update these bounds from CALIBRATE_TOUCH readings if button hits are off.
  int16_t screenY = constrain(map(p.x, 3900, 300, 0, 239), 0, 239);
  uint8_t button  = screenY / 80;

  Serial.print("raw x: "); Serial.print(p.x); Serial.print(" ("); Serial.print(screenY); Serial.print(") button: "); Serial.println(button);

  if (button == 0) {
    appMode = MODE_SCAN;
    currentGame = -1;
    memset(miniUid, 0, sizeof(miniUid));
    if (hasStorage) reader.drawBMP("/scan.bmp", tft, 0, 0);
  }
  // button 1: no-op (Mode 2 placeholder)
  if (button == 2) {
    appMode = MODE_INVENTORY;
    currentGame = -1;
    memset(miniUid, 0, sizeof(miniUid));
    showInventoryScreen();
  }
}

uint8_t idGame(char *id) {
  for (uint8_t i = 0; i < gameCount; i++) {
    if (strcmp(GAMES[i].code, id) == 0) return i;
  }
  return GAME_UNKNOWN;
}

void changeGame(uint8_t g) {
  if (currentGame != g) {
    currentGame = g;
    tft.fillScreen(ILI9341_WHITE);
    Serial.print("changing game: "); Serial.println(g, DEC);

    if (hasStorage) {
      char logoPath[18];
      snprintf(logoPath, sizeof(logoPath), "/logo/%s.bmp", GAMES[currentGame].code);
      reader.bmpDimensions(logoPath, &img_width, &img_height);
      reader.drawBMP(logoPath, tft, (320 - img_width)/2, 240 - img_height);
    }
  }
}

void idMini(char fids[], uint8_t fidCount, char *name) {
  int margin = 8;

  tft.fillRect(0, 0, 320, 140, ILI9341_WHITE);
  tft.setCursor(margin, margin);

  tft.setTextSize(3); tft.setTextColor(ILI9341_BLACK);

  char *nameStr = name;
  while (strlen(nameStr) > SCREEN_WIDTH_3) {
    uint8_t space = findSpace(nameStr);
    nameStr[space] = NULL;
    tft.println(nameStr);
    nameStr[space] = ' ';
    nameStr = &nameStr[space+1];
    tft.setCursor(margin, tft.getCursorY());
  }
  tft.println(nameStr);

  uint8_t fstart = GAMES[currentGame].factionStart;
  uint8_t fcount = GAMES[currentGame].factionCount;

  tft.setTextSize(2); tft.setCursor(margin, tft.getCursorY()+4);

  if (fidCount == 0) {
    GameFaction *faction = NULL;
    for (uint8_t i = 0; i < fcount; i++) {
      if (ALL_FACTIONS[fstart + i].id == '*') { faction = &ALL_FACTIONS[fstart + i]; break; }
    }
    if (faction) { tft.setTextColor(faction->color); tft.println(faction->name); }
    else          { tft.println(" "); }
  } else {
    for (uint8_t fi = 0; fi < fidCount; fi++) {
      if (fi > 0) tft.setCursor(margin, tft.getCursorY());
      GameFaction *faction = NULL;
      for (uint8_t i = 0; i < fcount; i++) {
        if (ALL_FACTIONS[fstart + i].id == fids[fi]) { faction = &ALL_FACTIONS[fstart + i]; break; }
      }
      if (faction)      { tft.setTextColor(faction->color); tft.println(faction->name); }
      else if (fi == 0) { tft.println(" "); }
    }
  }

  tft.setTextSize(1); tft.setTextColor(ILI9341_RED);
  tft.setCursor(margin, tft.getCursorY()+6);
  if (miniUid[0] < 16) tft.print("0");
  tft.print(miniUid[0], HEX);
  for (int i = 1; i < 7; i++) {
    tft.print(":"); 
    if (miniUid[i] < 16) tft.print("0");
    tft.print(miniUid[i], HEX);
  }
}

bool sameMini(uint8_t uid[]) {
  for (uint8_t i = 0; i < 7; i++){
    if (miniUid[i] != uid[i]) return false;
  }
  return true; 
}


uint8_t nfcReadMiniData() {
  uint8_t buffer[4];
  for (int i = 0; i < NFCDATASIZE/NFC_PAGE_SIZE; i++) {
    if (!nfc.ntag2xx_ReadPage(NFC_DATA_PAGE_OFFSET+i, buffer)) {
      Serial.print("error reading page "); Serial.println(NFC_DATA_PAGE_OFFSET+i);
      nfcPageData[i * NFC_PAGE_SIZE] = 0;
      return 0;
    }
    for (uint8_t j = 0; j < NFC_PAGE_SIZE; j++) {
      if (buffer[j] > 128) buffer[j] = 0;
      nfcPageData[i * NFC_PAGE_SIZE + j] = buffer[j];
    }
  }
  nfcPageData[NFCDATASIZE] = 0;
  return 1;
}

uint8_t parseFactionIds(char fids[], uint8_t maxFactions) {
  for (uint8_t i = 0; i < 12; i++) {
    if ((nfcPageData[i] == ']' || nfcPageData[i] == '\0') && nfcPageData[i+1] == '(') {
      uint8_t count = 0;
      uint8_t j = i + 2;
      while (nfcPageData[j] != ')' && nfcPageData[j] != '\0' && count < maxFactions) {
        fids[count++] = nfcPageData[j++];
      }
      return count;
    }
  }
  return 0;
}

// Null-terminates the ']' in nfcPageData so parseFactionIds/getMiniContent work,
// then returns the matching game index (or GAME_UNKNOWN).
uint8_t determineGame() {
  for (uint8_t gIdEnd = 3; gIdEnd < 12; gIdEnd++) {
    if (nfcPageData[gIdEnd] == ']') {
      nfcPageData[gIdEnd] = '\0';
    }
  }
  return idGame(&nfcPageData[2]);
}

char *getMiniContent() {
  for (uint8_t i = 3; i < 12; i++) {
    if (nfcPageData[i] == '\0' && nfcPageData[i+1] == ' ') return &nfcPageData[i+2];
    if (nfcPageData[i] == '\0' && nfcPageData[i+1] == '(') {
      for (uint8_t j = i+2; j < i+8; j++) {
        if (nfcPageData[j] == ')') return &nfcPageData[j+2];
      }
    }
  }
  return &nfcPageData[NFCDATASIZE];
}

uint8_t findSpace(char *str) {
  uint8_t i;
  for (i = SCREEN_WIDTH_3; i > 0; i--) {
    if (str[i] == ' ') break;
  }
  return i;
}


void loop(void) {
  checkTouch();
  if (appMode == MODE_SCAN || appMode == MODE_INVENTORY) scanMini();
}

