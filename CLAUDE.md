# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Arduino firmware that reads NFC-tagged tabletop gaming miniatures and displays faction/game information on a TFT screen. The entire project is a single monolithic sketch: `mini_nfc.ino`. Game data is defined in `sdcard/games.json` (copied to SD card root), not in firmware.

## Build & Deploy

There is no Makefile or PlatformIO config. Use the **Arduino IDE** or **Arduino CLI**:

```bash
# Arduino CLI — compile
arduino-cli compile --fqbn <board-fqbn> mini_nfc

# Arduino CLI — upload
arduino-cli upload -p <port> --fqbn <board-fqbn> mini_nfc
```

Serial monitor baud rate: **115200**.

## Hardware

| Component | Interface | Pins |
|---|---|---|
| PN532 NFC breakout | I2C | IRQ=2, RESET=3 |
| Adafruit 2.4" ILI9341 TFT Featherwing | SPI | CS=9, DC=10 |
| SD card module | SPI | CS=5 |

SD card is **required** — the sketch halts with a fatal error on screen if it is absent or `games.json` fails to parse. BMP files required: `/scan.bmp` (idle splash) plus one logo per game in `/logo/` named by game code (e.g. `/logo/FWW.bmp`, `/logo/BB.bmp`, `/logo/GA.bmp`).

BMP requirements: 24-bit color, uncompressed (no RLE), Windows BITMAPV4HEADER format. Suggested sizes: 320×240 for splash, up to ~320×100 for game logos.

## Required Libraries

Install via Arduino Library Manager:
- `Adafruit PN532` — NFC reader
- `Adafruit GFX Library` — graphics primitives
- `Adafruit ILI9341` — TFT display driver
- `SdFat` — SD card / FAT filesystem
- `ArduinoJson` — parses `games.json` (v6 API, `DynamicJsonDocument`)
- `Adafruit ImageReader Library` — loads BMP images from SD
- `Adafruit SPIFlash` — SPI flash (linked but unused)

## Architecture

Single-file sketch (`mini_nfc.ino`). Key data structures (fixed-length char arrays, all stack/global — no heap allocation beyond JSON parse):

```cpp
typedef struct { char id; char name[32]; uint16_t color; } GameFaction;
typedef struct { char name[32]; uint16_t color; char code[8];
                 uint8_t factionStart; uint8_t factionCount; } GameSystem;

GameFaction ALL_FACTIONS[MAX_TOTAL_FACTIONS];  // flat pool, all games
GameSystem  GAMES[MAX_GAME_COUNT];             // populated by loadGames() at startup
```

Factions are stored in a flat pool (`ALL_FACTIONS[]`) rather than embedded per-game. Each `GameSystem` holds a `factionStart` index and `factionCount` into that pool. This avoids forcing every game to pre-allocate slots for the largest game's faction count — important when faction counts vary significantly across games. `MAX_TOTAL_FACTIONS` (currently 64) caps the pool; `MAX_GAME_COUNT` (8) caps games.

**Startup sequence (`setup()`):**
1. Init TFT display
2. Mount SD — fatal halt if missing
3. `loadGames()` — parse `games.json` into `GAMES[]` — fatal halt on failure
4. Init PN532 NFC reader — fatal halt if not found
5. Draw `/scan.bmp` idle splash

**Main loop:**
`loop()` → `scanMini()` → on new 7-byte NTAG UID:
1. `nfcReadMiniData()` — reads pages 6–37 (128 bytes) into `nfcPageData[]`
2. `determineGame()` — finds and null-terminates the game code in `nfcPageData`, calls `changeGame()` which draws the logo
3. `getFactionId()` / `getMiniContent()` — parse faction char and mini name
4. `idMini()` — renders mini name (word-wrapped) + faction name/color + UID on screen

**NFC data format** (raw bytes starting at page 6):
```
[GAME_CODE](FACTION_ID) MINI_NAME
[GAME_CODE](FACTION_IDFACTION2_ID) MINI_NAME    ← optional second faction
e.g.  [FWW](B) Brotherhood Paladin
e.g.  [FWW](BN) Defected Paladin
```
- Bytes 0–1 are a header (`[` at index 1); game code starts at index 2
- `determineGame()` null-terminates `]` **before** `parseFactionIds()`/`getMiniContent()` are called — call order in `scanMini()` matters
- `parseFactionIds(fids, MAX_FACTIONS_PER_MINI)` fills a `char[]` with up to 3 faction IDs and returns the count; 0 means no faction encoded (falls back to `*` wildcard in `idMini()`)
- `getMiniContent()` scans forward to find `)` then skips past it — faction count–agnostic

**Adding a new game:**
1. Add an entry to `sdcard/games.json` (copy updated file to SD card)
2. Add the logo BMP to the SD card
3. No firmware changes needed unless limits are hit: `MAX_GAME_COUNT = 8`, `MAX_TOTAL_FACTIONS = 64`, `DynamicJsonDocument` capacity is 2048 bytes (increase if JSON grows large)

**Currently defined games** (in `games.json`):
- `FWW` — Fallout: Wasteland Warfare (12 factions)
- `BB` — Blood Bowl (2 factions)
- `GA` — Gundam Assemble (2 factions)

## Known Gaps / TODOs

- `unableToScan()` logs to Serial only — no display feedback
- Word-wrap in `idMini()` scans backward for a space but has no fallback if none found
- `determineGame()` silently returns on unknown game code — no error shown
- `sameMini()` debounce never resets — the same mini is ignored until a different tag is scanned
