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
| STMPE610 resistive touch controller | SPI | CS=6 |
| SD card module | SPI | CS=5 |

SD card is **required** — the sketch halts with a fatal error on screen if it is absent or `games.json` fails to parse. BMP files required: `/scan.bmp` (idle splash) plus one logo per game in `/logo/` named by game code (e.g. `/logo/FWW.bmp`, `/logo/BB.bmp`, `/logo/GA.bmp`).

BMP requirements: 24-bit color, uncompressed (no RLE), Windows BITMAPV4HEADER format. Suggested sizes: 320×240 for splash, up to ~320×100 for game logos.

## Required Libraries

Install via Arduino Library Manager:
- `Adafruit PN532` — NFC reader
- `Adafruit GFX Library` — graphics primitives
- `Adafruit ILI9341` — TFT display driver
- `Adafruit STMPE610` — resistive touch controller
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

Factions are stored in a flat pool (`ALL_FACTIONS[]`) rather than embedded per-game. Each `GameSystem` holds a `factionStart` index and `factionCount` into that pool. `MAX_TOTAL_FACTIONS` (currently 64) caps the pool; `MAX_GAME_COUNT` (8) caps games.

**App modes** (`appMode` global):
- `MODE_SCAN` (0) — actively scanning for NFC tags, displays mini info
- `MODE_MENU` (1) — full-screen touch menu, NFC scanning paused
- `MODE_INVENTORY` (2) — scanning active, each new mini appended to SD card CSV

Any touch while in `MODE_SCAN` or `MODE_INVENTORY` opens the menu. The menu has three full-screen buttons (each 80px tall): Scan Mode (top), Mode 2 (middle, no-op), Inventory Mode (bottom).

**Startup sequence (`setup()`):**
1. Init TFT display + STMPE610 touch controller — fatal halt if touch init fails
2. Mount SD — fatal halt if missing
3. `loadGames()` — parse `games.json` into `GAMES[]` — fatal halt on failure
4. Init PN532 NFC reader — fatal halt if not found
5. Draw `/scan.bmp` idle splash

**Main loop:**
```
loop() → checkTouch() → if MODE_SCAN or MODE_INVENTORY: scanMini()
```

`scanMini()` on a new 7-byte NTAG UID:
1. `nfcReadMiniData()` — reads pages 6–37 (128 bytes) into `nfcPageData[]`
2. Captures `rawNfc[]` (copy of `nfcPageData[1..]`) **before** calling `determineGame()`, which destructively null-terminates the `]` separator
3. `determineGame()` — null-terminates `]` in `nfcPageData`, returns game index (or `GAME_UNKNOWN`)
4. If `MODE_SCAN`: calls `changeGame()` which draws the logo; if `MODE_INVENTORY`: sets `currentGame` directly (no logo draw)
5. `parseFactionIds()` / `getMiniContent()` — parse faction chars and mini name
6. If `MODE_INVENTORY`: `recordMini()` — writes CSV row, updates display; otherwise `idMini()` — renders to screen

**NFC data format** (raw bytes starting at page 6):
```
[GAME_CODE](FACTION_ID) MINI_NAME
[GAME_CODE](FACTION_IDFACTION2_ID) MINI_NAME    ← optional second/third faction
e.g.  [FWW](B) Brotherhood Paladin
e.g.  [FWW](BN) Defected Paladin
```
- `nfcPageData[0]` is a header byte; `[` is at index 1; game code starts at index 2
- `determineGame()` must be called before `parseFactionIds()`/`getMiniContent()` — they depend on the `]` being null-terminated
- `parseFactionIds(fids, MAX_FACTIONS_PER_MINI)` returns count 0–3; 0 falls back to `*` wildcard in `idMini()`
- `getMiniContent()` scans forward past `)` — faction count–agnostic

**Touch controller (`checkTouch()`):**
The STMPE610 FIFO fills continuously while finger is held and emits `x=0, z=0` artifacts after release. `checkTouch()` uses a unified read+wait-for-release loop, keeping only samples where `z>0 && x>0`, then clears the interrupt register. In rotation 3, raw `p.x` maps to screen Y (inverted: high p.x ≈ 3900 = top of screen, low p.x ≈ 300 = bottom). Uncomment `#define CALIBRATE_TOUCH` to display raw coordinates for recalibration.

**Inventory mode:**
`recordMini()` appends one row per scan to `/inventory/<GAMECODE>.csv` on the SD card. The directory is created automatically on first write. Files get a `uid,nfc_data` header row on creation and are appended on subsequent sessions. CSV fields are RFC 4180-escaped (quoted if they contain `,`, `"`, or newlines). The display shows the same mini info layout as scan mode (top 140px) with an inventory status footer below.

**Adding a new game:**
1. Add an entry to `sdcard/games.json` (copy updated file to SD card)
2. Add the logo BMP to `/logo/` on the SD card
3. No firmware changes needed unless limits are hit: `MAX_GAME_COUNT = 8`, `MAX_TOTAL_FACTIONS = 64`, `DynamicJsonDocument` capacity is 4096 bytes (increase if JSON grows large)

To share factions with an existing game, add `"inheritFactions": "<CODE>"` instead of a `"factions"` array. The referenced game must appear **earlier** in the `games.json` array (one-pass loading). If the code is not found, `loadGames()` falls back to the `"factions"` array; if that is also absent the game loads with zero factions.

**Currently defined games** (in `games.json`):
- `FWW` — Fallout: Wasteland Warfare (22 factions)
- `FF` — Fallout: Factions (inherits FWW factions)
- `BB` — Blood Bowl (2 factions)
- `GA` — Gundam Assemble (2 factions)

## Known Gaps / TODOs

- `unableToScan()` logs to Serial only — no display feedback
- Word-wrap in `idMini()` scans backward for a space but has no fallback if none found
- `determineGame()` returning `GAME_UNKNOWN` silently drops the scan — no error shown on screen
- `sameMini()` debounce never resets — the same mini is ignored until a different tag is scanned
- Debug `Serial.print` statements remain in `checkTouch()` (button mapping output)
- `CALIBRATE_TOUCH` define block can be removed once calibration is finalized
- "Mode 2" menu button is a no-op placeholder
