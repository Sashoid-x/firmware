# Meshtastic Pixel Art (Monochrome Image) — Firmware Specification & Integration Guide

> **Target Audience:** AI Coding Agents & C++ Firmware Developers working on the `meshtastic/firmware` repository.  
> **Purpose:** Comprehensive, byte-exact technical manual for receiving, decoding, and rendering Pixel Art packets on Meshtastic hardware devices (supporting both Monochrome OLED/E-Paper and Color TFT displays).  
> **Source Ecosystem:** Meshtastic Android Advanced (`feature/monochrome-image-messaging` / `feature/messaging/image/MonochromeImageCodec.kt`).

---

## 1. Executive Summary & Wire Protocol

### 1.1 What is Pixel Art Messaging?
Pixel Art messaging enables users to send and receive high-contrast pixel illustrations (drawings, logos, icons, dithered camera photos) over the LoRa mesh network. 

Images are compressed to fit into a **single LoRa packet** (typically under **50–120 bytes**, well below the Meshtastic ~237-byte single-packet MTU limit), without requiring chunked reassembly or multi-packet overhead.

### 1.2 PortNum Allocation
Pixel Art packets are transmitted over Meshtastic's private application port:
- **PortNum:** `PortNum_PRIVATE_APP`
- **Numeric Value:** `256` (`0x0100` in protobuf `meshtastic.PortNum.PRIVATE_APP`)

### 1.3 Distinguishing Pixel Art from Other Private App Traffic
Multiple sub-protocols share `PortNum_PRIVATE_APP` (most notably Meshtastic File Transfer — MFT). Your packet receiver **must** safely identify Pixel Art payloads:

```cpp
bool isPixelArtPacket(const uint8_t* payload, size_t length) {
    if (length < 2) return false;

    // 1. Check for Meshtastic File Transfer (MFT) magic: "MFT\x01" (0x4D, 0x46, 0x54, 0x01)
    if (length >= 4 && payload[0] == 0x4D && payload[1] == 0x46 && payload[2] == 0x54 && payload[3] == 0x01) {
        return false; // MFT file transfer packet
    }

    // 2. Validate Pixel Art Header byte
    uint8_t header = payload[0];
    uint8_t enc = (header >> 4) & 0x0F;
    uint8_t preset = header & 0x0F;

    if (enc > 6) return false;     // Valid algorithms are 0..6
    if (preset > 9) return false;  // Valid presets are 0..9

    return true;
}
```

---

## 2. Packet Layout & Binary Format

```
+-------------------+-----------------------------------------+--------------------+
|  Byte 0 (Header)  |       Bytes 1 .. N-1 (Bitstream)        |  Byte N (Trailer)  |
+-------------------+-----------------------------------------+--------------------+
| [7..4] ENC        | Compressed / packed 1-bit pixel data    | [7]   SHOW_GRID    |
| [3..0] PRESET_IDX | (Variable length, decoded per algorithm)| [6..0] THEME_INDEX |
+-------------------+-----------------------------------------+--------------------+
```

### 2.1 Byte 0: Header Byte
- **Bits 7..4 (`enc`):** Compression algorithm ID (0 to 6):
  - `0 (ENC_RAW)`: Raw bit-packed stream (1 bpp, MSB first)
  - `1 (ENC_BLOCK_4X4)`: Quadtree/tile block compression (4×4 blocks)
  - `2 (ENC_BLOCK_8X8)`: Two-level block compression (8×8 -> 4×4 blocks)
  - `3 (ENC_VAR_RLE_H)`: Variable-length Run-Length Encoding (horizontal row-major)
  - `4 (ENC_VAR_RLE_V)`: Variable-length RLE on transposed grid (vertical column-major)
  - `5 (ENC_DELTA_2D)`: 2D MED predictor residuals + Variable RLE
  - `6 (ENC_LZSS)`: Sliding-window LZSS on raw packed bytes
- **Bits 3..0 (`presetIndex`):** Resolution preset ID (0 to 9).

### 2.2 Resolution Presets Table

| Preset ID | Name | Dimensions (W × H) | Total Pixels | Raw Bytes `(px+7)/8` | Aspect Ratio | Typical Use Case |
|:---------:|:----:|:------------------:|:------------:|:--------------------:|:------------:|:----------------:|
| **0** | 39×40 | 39 × 40 | 1560 | 195 | ~1:1 (Square) | Default drawing canvas |
| **1** | 32×32 | 32 × 32 | 1024 | 128 | 1:1 (Small Sq) | Icons, avatars, fast LoRa |
| **2** | 48×32 | 48 × 32 | 1536 | 192 | 3:2 Landscape | Landscape photos / badges |
| **3** | 32×48 | 32 × 48 | 1536 | 192 | 2:3 Portrait | Characters, portraits |
| **4** | 64×24 | 64 × 24 | 1536 | 192 | 8:3 Wide | Wide banners / text logos |
| **5** | 24×64 | 24 × 64 | 1536 | 192 | 3:8 Tall | Vertical bookmarks / tall art |
| **6** | 44×36 | 44 × 36 | 1584 | 198 | 11:9 Landscape | Detailed scene |
| **7** | 36×44 | 36 × 44 | 1584 | 198 | 9:11 Portrait | Detailed character |
| **8** | 52×30 | 52 × 30 | 1560 | 195 | 16:9 Landscape | Widescreen landscape |
| **9** | 30×52 | 30 × 52 | 1560 | 195 | 9:16 Portrait | Mobile aspect portrait |

> [!NOTE]
> Maximum pixels in any preset is **1584 pixels** (198 bytes). In firmware, a buffer of `bool[1584]` or `uint8_t[1584]` takes less than 1.6 KB of RAM, making it very safe for microcontrollers.

### 2.3 Last Byte: Trailer Byte (Theme & Grid)
The last byte in the packet specifies the color palette and grid line preference:
- **Bit 7 (`showGrid`):** 
  - `0`: Render pixels seamlessly.
  - `1`: Render subtle pixel grid lines (if screen resolution allows, e.g. scale ≥ 3 on color displays).
- **Bits 6..0 (`themeIndex`):**
  - Integer `0..23` selecting one of the **24 Color Palettes** (see Section 4).

#### Backward Compatibility Rule (Legacy Packets):
Early versions of the Android app did not include the trailer byte.
To distinguish whether the trailer is present:
1. Decompress the payload to get `preset.totalPixels`.
2. Compute `consumedBytes`:
   - For `ENC_RAW`: `consumedBytes = (totalPixels + 7) / 8`
   - For compressed formats (`ENC_BLOCK_4X4` .. `ENC_LZSS`): `consumedBytes = (bitReader.bitPos + 7) / 8`
3. Compare:
   - If `payload_size > consumedBytes`: The very last byte is the **Trailer Byte** (`themeIndex = trailer & 0x7F`, `showGrid = (trailer & 0x80) != 0`).
   - If `payload_size <= consumedBytes`: It is a **Legacy Packet**. Default to `themeIndex = 0` (Classic B/W), `showGrid = false`.

---

## 3. Pixel Bit Order & Decompression Algorithms

Pixels are arranged in standard **Row-Major** order:
- Index `i = y * width + x`, where `0 <= x < width` and `0 <= y < height`.
- `bit == 0`: **Paper / Background**
- `bit == 1`: **Pencil / Foreground** (the drawn pixel)

Bit streams are read **MSB first** (Most Significant Bit first in each byte):
- Bit 0 of a byte is read from `(byte >> 7) & 1`.
- Bit 7 of a byte is read from `(byte >> 0) & 1`.

### 3.1 `ENC_RAW` (Algorithm 0)
Uncompressed 1 bpp bitmap:
```cpp
for (int i = 0; i < totalPixels; i++) {
    int byteIdx = i / 8;
    int bitOffset = 7 - (i % 8);
    outBits[i] = (payload[byteIdx] >> bitOffset) & 1;
}
```

### 3.2 `ENC_BLOCK_4X4` (Algorithm 1)
Image is divided into 4×4 pixel tiles:
- Tile grid: `bxCount = (width + 3) / 4`, `byCount = (height + 3) / 4`.
- For each tile `(by, bx)`:
  1. Read 1 bit `isMixed`.
  2. If `isMixed == 0`: Tile is solid `0` (all pixels false).
  3. If `isMixed == 1`:
     - Read 1 bit: if `0`, tile is solid `1` (all pixels true).
     - If `1`: tile is mixed. Read 1 bit for each in-bounds pixel `(x, y)`:
       ```cpp
       for (int py = 0; py < 4; py++) {
           for (int px = 0; px < 4; px++) {
               int x = bx * 4 + px;
               int y = by * 4 + py;
               if (x < width && y < height) {
                   outBits[y * width + x] = reader.readBit();
               }
           }
       }
       ```

### 3.3 `ENC_BLOCK_8X8` (Algorithm 2)
Two-level hierarchical quadtree compression:
- Image divided into 8×8 tiles: `bx8Count = (width + 7) / 8`, `by8Count = (height + 7) / 8`.
- For each 8×8 tile:
  1. Read 1 bit `isMixed8`.
  2. If `0`: entire 8×8 tile is solid `0`.
  3. If `1`:
     - Read 1 bit: if `0`, entire 8×8 tile is solid `1`.
     - If `1`: tile has 4 sub-blocks of 4×4 (quadtree):
       - For each 4×4 sub-block:
         - Read 1 bit `isMixed4`.
         - If `0`: sub-block solid `0`.
         - If `1`:
           - Read 1 bit: if `0`, sub-block solid `1`.
           - If `1`: read in-bounds individual bits row-by-row.

### 3.4 `ENC_VAR_RLE_H` (Algorithm 3)
Variable-length prefix RLE:
1. Read 1 bit `currentColor`.
2. Loop until all `totalPixels` are decoded:
   - Read run length:
     - Read 2 bits `tag2`:
       - `0b00`: run = 1
       - `0b01`: run = 2 + `readBits(1)` (range 2..3)
       - `0b10`: read 1 bit:
         - if `0`: run = 4 + `readBits(2)` (range 4..7)
         - if `1`: run = 8 + `readBits(3)` (range 8..15)
       - `0b11`: read 2 bits `tag4`:
         - `0b00`: run = 16 + `readBits(4)` (range 16..31)
         - `0b01`: run = 32 + `readBits(5)` (range 32..63)
         - `0b10`: run = 64 + `readBits(8)` (range 64..255)
         - `0b11`: run = 256 + `readBits(12)` (range 256..4351)
   - Fill `min(run, remainingPixels)` with `currentColor`.
   - Invert `currentColor = !currentColor`.

### 3.5 `ENC_VAR_RLE_V` (Algorithm 4)
Same RLE bitstream as `ENC_VAR_RLE_H`, but decoded into a temporary buffer in **column-major** order, then transposed:
```cpp
// Transpose from column-major to row-major
for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
        outBits[y * width + x] = tempBits[x * height + y];
    }
}
```

### 3.6 `ENC_DELTA_2D` (Algorithm 5)
2D Median Predictor (similar to PNG Paeth/MED predictor):
1. Decode residual bitstream using `ENC_VAR_RLE_H` into `residuals[]`.
2. Reconstruct each pixel `(x, y)`:
   ```cpp
   for (int y = 0; y < height; y++) {
       for (int x = 0; x < width; x++) {
           int i = y * width + x;
           bool pred;
           if (x == 0 && y == 0) {
               pred = false;
           } else if (y == 0) {
               pred = outBits[x - 1];
           } else if (x == 0) {
               pred = outBits[(y - 1) * width];
           } else {
               bool left = outBits[y * width + (x - 1)];
               bool top  = outBits[(y - 1) * width + x];
               bool diag = outBits[(y - 1) * width + (x - 1)];
               pred = (left == top) ? left : (diag ^ left ^ top);
           }
           outBits[i] = residuals[i] ^ pred;
       }
   }
   ```

### 3.7 `ENC_LZSS` (Algorithm 6)
Byte-level LZSS decompression with sliding window:
- Target byte count: `expectedByteCount = (totalPixels + 7) / 8`.
- Loop until `outPos == expectedByteCount`:
  - Read 1 bit `isMatch`:
    - If `1` (Match):
      - `offset = readBits(6) + 1` (1..64 lookback)
      - `length = readBits(4) + 2` (2..17 bytes)
      - Copy `length` bytes from `outPos - offset` to `outPos`.
    - If `0` (Literal):
      - Read 8 bits and write as literal byte.
- Finally, unpack the decoded bytes into `outBits[totalPixels]`.

---

## 4. The 24 Palettes & Color Definitions

The app includes **24 curated retro/modern color palettes**.  
For color displays (TFT/IPS), use the pre-calculated **RGB565** color values below:

| ID | Name | Background (RGB888) | Foreground (RGB888) | BG (RGB565) | FG (RGB565) | Grid (RGB565) | Is Dark BG? |
|:--:|:----:|:-------------------:|:-------------------:|:-----------:|:-----------:|:-------------:|:-----------:|
| **0** | Classic | `0xFFFFFF` | `0x000000` | `0xFFFF` | `0x0000` | `0x0000` | No |
| **1** | Classic Dark | `0x000000` | `0xFFFFFF` | `0x0000` | `0xFFFF` | `0xFFFF` | Yes |
| **2** | E-Paper | `0xF5EFEB` | `0x2C2420` | `0xF77D` | `0x2924` | `0x2924` | No |
| **3** | Sepia | `0xEADCC9` | `0x4A3525` | `0xEEF9` | `0x49A4` | `0x49A4` | No |
| **4** | Blueprint | `0x0A2E5C` | `0xE0F0FF` | `0x096B` | `0xE79F` | `0xE79F` | Yes |
| **5** | Game Boy | `0x8B956D` | `0x0F380F` | `0x8CAD` | `0x09C1` | `0x09C1` | No |
| **6** | Game Boy Pocket | `0xC4BEBB` | `0x2C2C2C` | `0xC5F7` | `0x2965` | `0x2965` | No |
| **7** | Matrix Green | `0x0A0F0D` | `0x00FF66` | `0x0861` | `0x07EC` | `0x07EC` | Yes |
| **8** | Amber CRT | `0x140A00` | `0xFFB000` | `0x1040` | `0xFD80` | `0xFD80` | Yes |
| **9** | Solarized Light | `0xFDF6E3` | `0x657B83` | `0xFFBC` | `0x63D0` | `0x63D0` | No |
| **10** | Solarized Dark | `0x002B36` | `0x2AA198` | `0x0146` | `0x2D13` | `0x2D13` | Yes |
| **11** | Cyberpunk | `0x0B001A` | `0xFFFF007F` | `0x0803` | `0xF80F` | `0xF80F` | Yes |
| **12** | Synthwave | `0x1A0A2A` | `0x00F0FF` | `0x1845` | `0x079F` | `0x079F` | Yes |
| **13** | Ocean Blue | `0x001428` | `0x00D2FF` | `0x00A5` | `0x069F` | `0x069F` | Yes |
| **14** | Forest Moss | `0x0D1A10` | `0xA8E063` | `0x08C2` | `0xAF0C` | `0xAF0C` | Yes |
| **15** | Blood Moon | `0x150000` | `0xFFFF3333` | `0x1000` | `0xF986` | `0xF986` | Yes |
| **16** | Sunset Gold | `0x1A091A` | `0xFFAA33` | `0x1843` | `0xFD46` | `0xFD46` | Yes |
| **17** | Nordic Frost | `0x2E3440` | `0x88C0D0` | `0x29A8` | `0x8E1A` | `0x8E1A` | Yes |
| **18** | Dracula | `0x282A36` | `0xBD93F9` | `0x2946` | `0xBC9F` | `0xBC9F` | Yes |
| **19** | Chalkboard | `0x233227` | `0xE8F5E9` | `0x2184` | `0xEFBD` | `0xEFBD` | Yes |
| **20** | Monokai | `0x272822` | `0xE6DB74` | `0x2144` | `0xE6CE` | `0xE6CE` | Yes |
| **21** | Terminal White | `0x0F0F0F` | `0xF0F0F0` | `0x0861` | `0xF79E` | `0xFFFF` | Yes |
| **22** | Notebook | `0xFAF8F5` | `0x1A365D` | `0xFFDE` | `0x19AB` | `0x19AB` | No |
| **23** | Graphite | `0xECEFF1` | `0x37474F` | `0xEF7E` | `0x3229` | `0x3229` | No |

---

## 5. Screen Rendering Strategies: Monochrome vs. Color

Devices in the Meshtastic ecosystem have wildly different screens. Here is how to handle each category cleanly:

### 5.1 Monochrome Displays (OLED SSD1306/SH1106, 1-bit E-Paper)
*Examples: Heltec V3/V2 (0.96" OLED), LilyGO T-Beam, LilyGO T-Echo (E-paper), RAK4631 with OLED, Heltec T114.*

1. **Pixel Semantics**:
   - `bit == 0`: Background (Paper)
   - `bit == 1`: Drawing Stroke (Pencil)
2. **E-Paper Screens (T-Echo, Heltec T114 E-paper)**:
   - Paper is naturally white.
   - Set `bit == 0` -> White / Clean.
   - Set `bit == 1` -> Black / Ink.
   - Natural 100% fidelity.
3. **OLED Displays (128×64 / 64×32)**:
   - On OLED, a pixel is lit (`WHITE`/`1`) or dark (`BLACK`/`0`).
   - If the theme has a **Dark Background** (`is_dark_bg == true`, e.g. Classic Dark, Matrix Green, Amber CRT, Dracula):
     - Background (`bit == 0`) = OLED OFF (`BLACK`)
     - Foreground (`bit == 1`) = OLED ON (`WHITE`)
   - If the theme has a **Light Background** (`is_dark_bg == false`, e.g. Classic, E-Paper, Sepia, Solarized Light):
     - Inverting the whole 128×64 screen to lit white OLED can blind the user in dark conditions and consume high battery current.
     - **Recommendation**: Always draw drawn strokes (`bit == 1`) as lit OLED pixels (`WHITE`), leaving the rest black (`BLACK`). Optionally, allow users to toggle "Invert Monochrome Art" in screen settings.
4. **Centering & Scaling on 128×64 OLED**:
   - For Preset 0 (39×40):
     - 1× scale fits nicely centered: `xOffset = (128 - 39) / 2 = 44`, `yOffset = (64 - 40) / 2 = 12`.
   - For Preset 1 (32×32):
     - **2× scale** gives 64×64: fits perfectly height-wise! `xOffset = (128 - 64) / 2 = 32`, `yOffset = 0`.
   - For Preset 2 (48×32):
     - **2× scale** gives 96×64: fits perfectly! `xOffset = (128 - 96) / 2 = 16`, `yOffset = 0`.
   - General integer scaling formula:
     ```cpp
     int scale = 1;
     if (width * 2 <= dispWidth && height * 2 <= dispHeight) {
         scale = 2;
     }
     int xOffset = (dispWidth - width * scale) / 2;
     int yOffset = (dispHeight - height * scale) / 2;
     ```

### 5.2 Color Displays (TFT / IPS ST7789, ILI9341, ST7735)
*Examples: LilyGO T-Deck (320×240 TFT), LilyGO T-Watch-2020 (240×240), Heltec Vision Master, LilyGO T-Display.*

1. **Full Palette Rendering**:
   - Look up theme `t = THEMES[themeIndex]`.
   - Draw background filling the screen or a centered frame with `t.bg_565`.
   - Calculate maximum integer scale:
     ```cpp
     int scale = min(dispWidth / width, dispHeight / height);
     if (scale < 1) scale = 1;
     int xOffset = (dispWidth - width * scale) / 2;
     int yOffset = (dispHeight - height * scale) / 2;
     ```
2. **Pixel Drawing**:
   - For each pixel at `(x, y)`:
     ```cpp
     uint16_t color = bits[y * width + x] ? t.fg_565 : t.bg_565;
     display->fillRect(xOffset + x * scale, yOffset + y * scale, scale, scale, color);
     ```
3. **Optional Pixel Grid Rendering**:
   - If `showGrid == true` and `scale >= 3`:
     Draw a 1-pixel line along the bottom and right edge of each pixel using `t.grid_565`:
     ```cpp
     if (showGrid && scale >= 3) {
         display->drawFastHLine(xOffset + x * scale, yOffset + (y + 1) * scale - 1, scale, t.grid_565);
         display->drawFastVLine(xOffset + (x + 1) * scale - 1, yOffset + y * scale, scale, t.grid_565);
     }
     ```

---

## 6. Standalone C++ Reference Implementation

Below is a complete, self-contained C++ header file (`PixelArtDecoder.h`) designed for microcontrollers (zero heap allocation, safe bounds checking, no external dependencies). Copy it into your firmware project under `src/graphics/` or `src/modules/`.

```cpp
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <algorithm>

namespace PixelArt {

struct Preset {
    uint8_t index;
    uint8_t width;
    uint8_t height;
};

static const Preset PRESETS[10] = {
    {0, 39, 40}, // Square
    {1, 32, 32}, // Small Sq
    {2, 48, 32}, // 3:2
    {3, 32, 48}, // 2:3
    {4, 64, 24}, // 8:3
    {5, 24, 64}, // 3:8
    {6, 44, 36}, // 11:9
    {7, 36, 44}, // 9:11
    {8, 52, 30}, // 16:9
    {9, 30, 52}  // 9:16
};

struct Theme {
    uint8_t id;
    const char* name;
    uint32_t bg_888;
    uint32_t fg_888;
    uint16_t bg_565;
    uint16_t fg_565;
    uint16_t grid_565;
    bool is_dark;
};

static const Theme THEMES[24] = {
    {  0, "Classic",         0xFFFFFF, 0x000000, 0xFFFF, 0x0000, 0x0000, false },
    {  1, "Classic Dark",    0x000000, 0xFFFFFF, 0x0000, 0xFFFF, 0xFFFF, true  },
    {  2, "E-Paper",         0xF5EFEB, 0x2C2420, 0xF77D, 0x2924, 0x2924, false },
    {  3, "Sepia",           0xEADCC9, 0x4A3525, 0xEEF9, 0x49A4, 0x49A4, false },
    {  4, "Blueprint",       0x0A2E5C, 0xE0F0FF, 0x096B, 0xE79F, 0xE79F, true  },
    {  5, "Game Boy",        0x8B956D, 0x0F380F, 0x8CAD, 0x09C1, 0x09C1, false },
    {  6, "Game Boy Pocket", 0xC4BEBB, 0x2C2C2C, 0xC5F7, 0x2965, 0x2965, false },
    {  7, "Matrix Green",    0x0A0F0D, 0x00FF66, 0x0861, 0x07EC, 0x07EC, true  },
    {  8, "Amber CRT",       0x140A00, 0xFFB000, 0x1040, 0xFD80, 0xFD80, true  },
    {  9, "Solarized Light", 0xFDF6E3, 0x657B83, 0xFFBC, 0x63D0, 0x63D0, false },
    { 10, "Solarized Dark",  0x002B36, 0x2AA198, 0x0146, 0x2D13, 0x2D13, true  },
    { 11, "Cyberpunk",       0x0B001A, 0xFF007F, 0x0803, 0xF80F, 0xF80F, true  },
    { 12, "Synthwave",       0x1A0A2A, 0x00F0FF, 0x1845, 0x079F, 0x079F, true  },
    { 13, "Ocean Blue",      0x001428, 0x00D2FF, 0x00A5, 0x069F, 0x069F, true  },
    { 14, "Forest Moss",     0x0D1A10, 0xA8E063, 0x08C2, 0xAF0C, 0xAF0C, true  },
    { 15, "Blood Moon",      0x150000, 0xFF3333, 0x1000, 0xF986, 0xF986, true  },
    { 16, "Sunset Gold",     0x1A091A, 0xFFAA33, 0x1843, 0xFD46, 0xFD46, true  },
    { 17, "Nordic Frost",    0x2E3440, 0x88C0D0, 0x29A8, 0x8E1A, 0x8E1A, true  },
    { 18, "Dracula",         0x282A36, 0xBD93F9, 0x2946, 0xBC9F, 0xBC9F, true  },
    { 19, "Chalkboard",      0x233227, 0xE8F5E9, 0x2184, 0xEFBD, 0xEFBD, true  },
    { 20, "Monokai",         0x272822, 0xE6DB74, 0x2144, 0xE6CE, 0xE6CE, true  },
    { 21, "Terminal White",  0x0F0F0F, 0xF0F0F0, 0x0861, 0xF79E, 0xFFFF, true  },
    { 22, "Notebook",        0xFAF8F5, 0x1A365D, 0xFFDE, 0x19AB, 0x19AB, false },
    { 23, "Graphite",        0xECEFF1, 0x37474F, 0xEF7E, 0x3229, 0x3229, false }
};

struct DecodedImage {
    uint8_t presetIndex;
    uint8_t width;
    uint8_t height;
    uint8_t themeIndex;
    bool showGrid;
    uint16_t pixelCount;
    // 1584 is the maximum possible pixels across all presets (Preset 6 and 7)
    bool pixels[1584];
};

class BitReader {
public:
    const uint8_t* data;
    size_t length;
    size_t bitPos;

    BitReader(const uint8_t* d, size_t len) : data(d), length(len), bitPos(0) {}

    bool hasBits() const {
        return bitPos < length * 8;
    }

    bool readBit() {
        if (bitPos >= length * 8) return false;
        size_t byteIdx = bitPos / 8;
        int bitOffset = 7 - (bitPos % 8);
        bitPos++;
        return (data[byteIdx] >> bitOffset) & 1;
    }

    uint32_t readBits(int count) {
        uint32_t val = 0;
        for (int i = 0; i < count; i++) {
            val = (val << 1) | (readBit() ? 1 : 0);
        }
        return val;
    }
};

class Decoder {
private:
    static int readVarRleRun(BitReader& reader) {
        uint32_t tag2 = reader.readBits(2);
        switch (tag2) {
            case 0b00: return 1;
            case 0b01: return 2 + reader.readBits(1);
            case 0b10: {
                bool b = reader.readBit();
                return b ? (8 + reader.readBits(3)) : (4 + reader.readBits(2));
            }
            default: {
                uint32_t tag4 = reader.readBits(2);
                switch (tag4) {
                    case 0b00: return 16 + reader.readBits(4);
                    case 0b01: return 32 + reader.readBits(5);
                    case 0b10: return 64 + reader.readBits(8);
                    default:   return 256 + reader.readBits(12);
                }
            }
        }
    }

    static void decodeVarRle(BitReader& reader, bool* out, int count) {
        if (!reader.hasBits()) return;
        bool currentColor = reader.readBit();
        int written = 0;
        while (written < count && reader.hasBits()) {
            int run = readVarRleRun(reader);
            int toWrite = std::min(run, count - written);
            for (int i = 0; i < toWrite; i++) {
                out[written++] = currentColor;
            }
            currentColor = !currentColor;
        }
    }

    static void decodeBlock4x4(BitReader& reader, bool* out, int width, int height) {
        int bxCount = (width + 3) / 4;
        int byCount = (height + 3) / 4;
        for (int by = 0; by < byCount; by++) {
            for (int bx = 0; bx < bxCount; bx++) {
                int startX = bx * 4;
                int startY = by * 4;
                bool isMixed = reader.readBit();
                if (!isMixed) {
                    // All 0s (already false)
                } else {
                    bool isAllOne = !reader.readBit();
                    if (isAllOne) {
                        for (int py = 0; py < 4; py++) {
                            for (int px = 0; px < 4; px++) {
                                int x = startX + px;
                                int y = startY + py;
                                if (x < width && y < height) out[y * width + x] = true;
                            }
                        }
                    } else {
                        for (int py = 0; py < 4; py++) {
                            for (int px = 0; px < 4; px++) {
                                int x = startX + px;
                                int y = startY + py;
                                if (x < width && y < height) out[y * width + x] = reader.readBit();
                            }
                        }
                    }
                }
            }
        }
    }

    static void decodeBlock8x8(BitReader& reader, bool* out, int width, int height) {
        int bx8Count = (width + 7) / 8;
        int by8Count = (height + 7) / 8;
        for (int by8 = 0; by8 < by8Count; by8++) {
            for (int bx8 = 0; bx8 < bx8Count; bx8++) {
                int startX8 = bx8 * 8;
                int startY8 = by8 * 8;
                bool isMixed8 = reader.readBit();
                if (!isMixed8) {
                    // All 0s
                } else {
                    bool isAllOne8 = !reader.readBit();
                    if (isAllOne8) {
                        for (int py = 0; py < 8; py++) {
                            for (int px = 0; px < 8; px++) {
                                int x = startX8 + px;
                                int y = startY8 + py;
                                if (x < width && y < height) out[y * width + x] = true;
                            }
                        }
                    } else {
                        for (int subY = 0; subY < 2; subY++) {
                            for (int subX = 0; subX < 2; subX++) {
                                int startX4 = startX8 + subX * 4;
                                int startY4 = startY8 + subY * 4;
                                bool isMixed4 = reader.readBit();
                                if (!isMixed4) {
                                    // Sub-block all 0s
                                } else {
                                    bool isAllOne4 = !reader.readBit();
                                    if (isAllOne4) {
                                        for (int py = 0; py < 4; py++) {
                                            for (int px = 0; px < 4; px++) {
                                                int x = startX4 + px;
                                                int y = startY4 + py;
                                                if (x < width && y < height) out[y * width + x] = true;
                                            }
                                        }
                                    } else {
                                        for (int py = 0; py < 4; py++) {
                                            for (int px = 0; px < 4; px++) {
                                                int x = startX4 + px;
                                                int y = startY4 + py;
                                                if (x < width && y < height) out[y * width + x] = reader.readBit();
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    static void decodeDelta2D(BitReader& reader, bool* out, int width, int height) {
        bool residuals[1584];
        memset(residuals, 0, sizeof(residuals));
        decodeVarRle(reader, residuals, width * height);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int i = y * width + x;
                bool pred;
                if (x == 0 && y == 0) {
                    pred = false;
                } else if (y == 0) {
                    pred = out[x - 1];
                } else if (x == 0) {
                    pred = out[(y - 1) * width];
                } else {
                    bool left = out[y * width + (x - 1)];
                    bool top  = out[(y - 1) * width + x];
                    bool diag = out[(y - 1) * width + (x - 1)];
                    pred = (left == top) ? left : (diag ^ left ^ top);
                }
                out[i] = residuals[i] ^ pred;
            }
        }
    }

    static void decodeLzss(BitReader& reader, bool* out, int pixelCount) {
        int expectedBytes = (pixelCount + 7) / 8;
        uint8_t byteBuf[200];
        memset(byteBuf, 0, sizeof(byteBuf));
        int outPos = 0;

        while (outPos < expectedBytes && reader.hasBits()) {
            bool isMatch = reader.readBit();
            if (isMatch) {
                int offset = reader.readBits(6) + 1;
                int length = reader.readBits(4) + 2;
                int start = outPos - offset;
                for (int i = 0; i < length; i++) {
                    if (outPos < expectedBytes && (start + i) >= 0) {
                        byteBuf[outPos] = byteBuf[start + i];
                        outPos++;
                    }
                }
            } else {
                byteBuf[outPos++] = (uint8_t)reader.readBits(8);
            }
        }

        // Unpack bytes to bits
        for (int i = 0; i < pixelCount; i++) {
            int byteIdx = i / 8;
            if (byteIdx < expectedBytes) {
                out[i] = (byteBuf[byteIdx] >> (7 - (i % 8))) & 1;
            }
        }
    }

public:
    static bool decode(const uint8_t* data, size_t length, DecodedImage* out) {
        if (!data || length < 2 || !out) return false;

        uint8_t header = data[0];
        uint8_t enc = (header >> 4) & 0x0F;
        uint8_t presetIdx = header & 0x0F;

        if (enc > 6 || presetIdx > 9) return false;

        const Preset& preset = PRESETS[presetIdx];
        out->presetIndex = presetIdx;
        out->width = preset.width;
        out->height = preset.height;
        out->pixelCount = preset.width * preset.height;
        memset(out->pixels, 0, sizeof(out->pixels));

        const uint8_t* payload = data + 1;
        size_t payloadLen = length - 1;
        BitReader reader(payload, payloadLen);

        switch (enc) {
            case 0: // ENC_RAW
                for (int i = 0; i < out->pixelCount; i++) {
                    int byteIdx = i / 8;
                    if ((size_t)byteIdx < payloadLen) {
                        out->pixels[i] = (payload[byteIdx] >> (7 - (i % 8))) & 1;
                    }
                }
                break;
            case 1: // ENC_BLOCK_4X4
                decodeBlock4x4(reader, out->pixels, out->width, out->height);
                break;
            case 2: // ENC_BLOCK_8X8
                decodeBlock8x8(reader, out->pixels, out->width, out->height);
                break;
            case 3: // ENC_VAR_RLE_H
                decodeVarRle(reader, out->pixels, out->pixelCount);
                break;
            case 4: { // ENC_VAR_RLE_V
                bool temp[1584];
                memset(temp, 0, sizeof(temp));
                decodeVarRle(reader, temp, out->pixelCount);
                for (int y = 0; y < out->height; y++) {
                    for (int x = 0; x < out->width; x++) {
                        out->pixels[y * out->width + x] = temp[x * out->height + y];
                    }
                }
                break;
            }
            case 5: // ENC_DELTA_2D
                decodeDelta2D(reader, out->pixels, out->width, out->height);
                break;
            case 6: // ENC_LZSS
                decodeLzss(reader, out->pixels, out->pixelCount);
                break;
            default:
                return false;
        }

        // Backward compatibility: detect trailer
        size_t consumedBytes = (enc == 0) ? ((out->pixelCount + 7) / 8) : ((reader.bitPos + 7) / 8);
        bool hasTrailer = (payloadLen > consumedBytes);

        if (hasTrailer) {
            uint8_t trailer = payload[payloadLen - 1];
            out->showGrid = (trailer & 0x80) != 0;
            out->themeIndex = trailer & 0x7F;
            if (out->themeIndex >= 24) out->themeIndex = 0;
        } else {
            // Legacy packet fallback
            out->showGrid = false;
            out->themeIndex = 0;
        }

        return true;
    }
};

} // namespace PixelArt
```

---

## 7. Firmware Architecture & Integration Steps

### Step 1: Create `PixelArtModule`
In `src/modules/PixelArtModule.h` and `.cpp`:
- Derive from `SinglePortModule`:
  ```cpp
  #include "SinglePortModule.h"
  #include "PixelArtDecoder.h"

  class PixelArtModule : public SinglePortModule {
  public:
      PixelArtModule() : SinglePortModule("pixelart", meshtastic_PortNum_PRIVATE_APP) {}

  protected:
      virtual bool wantPacket(const meshtastic_MeshPacket *p) override {
          if (!SinglePortModule::wantPacket(p)) return false;
          const auto &d = p->decoded;
          return PixelArt::isPixelArtPacket(d.payload.bytes, d.payload.size);
      }

      virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
  };
  ```
- Register the module in `src/modules/Modules.cpp`.

### Step 2: Handle Incoming Packet
In `PixelArtModule::handleReceived`:
```cpp
ProcessMessage PixelArtModule::handleReceived(const meshtastic_MeshPacket &mp) {
    auto &d = mp.decoded;
    PixelArt::DecodedImage img;
    if (PixelArt::Decoder::decode(d.payload.bytes, d.payload.size, &img)) {
        LOG_INFO("Pixel Art received: %dx%d, theme: %d, grid: %d", 
                 img.width, img.height, img.themeIndex, img.showGrid);
        
        // Save image to shared UI state buffer
        graphics::setLatestPixelArt(img, mp.from);
        
        // Wake up screen and switch to pixel art screen or alert overlay
        if (screen) {
            screen->wake();
            screen->showPixelArtAlert();
        }
    }
    return ProcessMessage::STOP; // Handled
}
```

### Step 3: Implement Display Rendering in Screen UI
In `src/graphics/Screen.cpp` or `src/graphics/draw/UIRenderer.cpp`:
- Implement `drawPixelArt(const PixelArt::DecodedImage &img)`:
  - Check whether `screen->isColorDisplay()` or `screen->isMonochrome()`.
  - Apply the scaling and color logic detailed in **Section 5**.
  - Show the sender's short node name or ID at the top or bottom bar.
  - Automatically dismiss back to normal carousel after 30 seconds, or immediately on user button press.

---

## 8. Summary Checklist for Testing & Verification

1. [ ] **Packet Identification:** Confirm MFT packets (`"MFT\x01"`) are ignored by the Pixel Art decoder.
2. [ ] **Preset Dimensions:** Verify all 10 presets (from 32×32 to 64×24) calculate offsets and fit within the display frame.
3. [ ] **Decompression Accuracy:** Verify all 7 compression modes decode without corruption.
4. [ ] **Monochrome Displays (OLED / E-Paper):** Verify background/pencil mapping (pencil drawn as lit pixels on OLED, black ink on E-paper).
5. [ ] **Color Displays (TFT):** Verify themes render in 16-bit RGB565 and grid appears when `showGrid == true`.
6. [ ] **Legacy Compatibility:** Confirm packets without trailer byte gracefully fallback to Classic theme (theme 0).
