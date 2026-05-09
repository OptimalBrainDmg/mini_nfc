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
#include <SdFat.h>                // SD card & FAT filesystem library
#include <Adafruit_SPIFlash.h>    // SPI / QSPI flash library
#include <Adafruit_ImageReader.h> // Image-reading functions
#include <ArduinoJson.h>          // https://arduinojson.org — install via Library Manager

#define PN532_IRQ   (2)
#define PN532_RESET (3)  
#define TFT_CS      (9)
#define TFT_DC      (10)
#define SD_CS       (5)

void determineGame();

// PN5232 breakout board attached via I2C
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// Adafruit 2.4" TFT featherwing
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

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


#define MAX_GAME_COUNT       8
#define MAX_TOTAL_FACTIONS  64
#define MAX_NAME_LEN        32
#define MAX_CODE_LEN         8
#define MAX_PATH_LEN        24

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
  char     logoFile[MAX_PATH_LEN];
  uint8_t  factionStart;
  uint8_t  factionCount;
} GameSystem;

GameFaction ALL_FACTIONS[MAX_TOTAL_FACTIONS];
uint8_t totalFactionCount = 0;
GameSystem GAMES[MAX_GAME_COUNT];
uint8_t gameCount = 0;

int currentGame = -1;
uint8_t miniUid[] = { 0, 0, 0, 0, 0, 0, 0 };


// Reads /games.json from the SD card and populates GAMES[].
// Increase the DynamicJsonDocument capacity if you add many games/factions.
bool loadGames() {
  SdFile file;
  if (!file.open("/games.json", O_RDONLY)) {
    Serial.println("Failed to open /games.json");
    return false;
  }

  DynamicJsonDocument doc(2048);
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
    strlcpy(g.name,     game["name"] | "",  sizeof(g.name));
    strlcpy(g.code,     game["code"] | "",  sizeof(g.code));
    strlcpy(g.logoFile, game["logo"] | "",  sizeof(g.logoFile));
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
  uint8_t success;
  uint8_t uidLength;  
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 }; 
  char gameBuffer[5];
  uint8_t data[32];
  char faction;
  
  // scan a mini!
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    Serial.println("failed");
    Serial.print(uidLength, HEX);
    Serial.println("");
    return;
  }

  // designed to work with NTAG 215
  if (uidLength != 7) {
    Serial.println("This doesn't seem to be an NTAG203 tag (UUID length != 7 bytes)!");
    return;
  }

  // check to see if it's the same id or not
  if (sameMini(uid)) { return; }

  for (uint i = 0; i < 7; i++) {
    miniUid[i] = uid[i];
  }

  // NTAG2x3 cards have 39*4 bytes of user pages (156 user bytes),
  // starting at page 4 ... larger cards just add pages to the end of
  // this range:

  // See: http://www.nxp.com/documents/short_data_sheet/NTAG203_SDS.pdf

  // TAG Type       PAGES   USER START    USER STOP
  // --------       -----   ----------    ---------
  // NTAG 203       42      4             39
  // NTAG 213       45      4             39
  // NTAG 215       135     4             129
  // NTAG 216       231     4             225

  if(nfcReadMiniData()) {
    Serial.println("this is what I got: ");
    Serial.println(&nfcPageData[1]);
    determineGame();
    idMini(getFactionId(), getMiniContent());
  }

}

void loop(void) {
  scanMini();
  delay(100);
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
      reader.bmpDimensions(GAMES[currentGame].logoFile, &img_width, &img_height);
      reader.drawBMP(GAMES[currentGame].logoFile, tft, (320 - img_width)/2, 240 - img_height);
    }
  }
}

void idMini(char fid, char *name) {
  int margin = 8;

  tft.fillRect(0, 0, 320, 140, ILI9341_WHITE);
  tft.setCursor(margin, margin);  
  
  // TODO: better word wrap to make it pretty

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


  GameFaction *faction = NULL;
  uint8_t fstart = GAMES[currentGame].factionStart;
  uint8_t fcount = GAMES[currentGame].factionCount;
  for (uint8_t i = 0; i < fcount; i++) {
    GameFaction &f = ALL_FACTIONS[fstart + i];
    if (f.id == fid || (fid == NULL && f.id == '*')) {
      faction = &f;
      break;
    }
  }

  tft.setTextSize(2); tft.setCursor(margin, tft.getCursorY()+4);
  if (faction) {
    tft.setTextColor(faction->color);
    tft.println(faction->name);
  } else {
    tft.println(" ");
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

char getFactionId() {
  for (uint8_t i = 0; i < 12; i++) {
    if ((nfcPageData[i] == ']' || nfcPageData[i] == NULL) && nfcPageData[i+1] == '(') return (char) nfcPageData[i+2];
  }
  return NULL;
}

void determineGame() {
  uint8_t gIdEnd;
  for (gIdEnd = 3; gIdEnd < 12; gIdEnd++) {
    if (nfcPageData[gIdEnd] == ']') {
      nfcPageData[gIdEnd] = NULL;
    }
  }

  uint8_t gameId = idGame(&nfcPageData[2]);
  if (gameId == GAME_UNKNOWN) {
    //TODO: do something
    return;
  }
  changeGame(gameId);
}

char *getMiniContent() {
  for (uint8_t i = 3; i < 12; i++) {
    if (nfcPageData[i] == NULL && nfcPageData[i+1] == ' ') return &nfcPageData[i+2];
    if (nfcPageData[i] == NULL && nfcPageData[i+1] == '(') return &nfcPageData[i+5];
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

