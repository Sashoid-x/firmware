#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace PixelArt
{

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
    const char *name;
    uint32_t bg_888;
    uint32_t fg_888;
    uint16_t bg_565;
    uint16_t fg_565;
    uint16_t grid_565;
    bool is_dark;
};

static const Theme THEMES[24] = {
    {0, "Classic", 0xFFFFFF, 0x000000, 0xFFFF, 0x0000, 0x0000, false},
    {1, "Classic Dark", 0x000000, 0xFFFFFF, 0x0000, 0xFFFF, 0xFFFF, true},
    {2, "E-Paper", 0xF5EFEB, 0x2C2420, 0xF77D, 0x2924, 0x2924, false},
    {3, "Sepia", 0xEADCC9, 0x4A3525, 0xEEF9, 0x49A4, 0x49A4, false},
    {4, "Blueprint", 0x0A2E5C, 0xE0F0FF, 0x096B, 0xE79F, 0xE79F, true},
    {5, "Game Boy", 0x8B956D, 0x0F380F, 0x8CAD, 0x09C1, 0x09C1, false},
    {6, "Game Boy Pocket", 0xC4BEBB, 0x2C2C2C, 0xC5F7, 0x2965, 0x2965, false},
    {7, "Matrix Green", 0x0A0F0D, 0x00FF66, 0x0861, 0x07EC, 0x07EC, true},
    {8, "Amber CRT", 0x140A00, 0xFFB000, 0x1040, 0xFD80, 0xFD80, true},
    {9, "Solarized Light", 0xFDF6E3, 0x657B83, 0xFFBC, 0x63D0, 0x63D0, false},
    {10, "Solarized Dark", 0x002B36, 0x2AA198, 0x0146, 0x2D13, 0x2D13, true},
    {11, "Cyberpunk", 0x0B001A, 0xFF007F, 0x0803, 0xF80F, 0xF80F, true},
    {12, "Synthwave", 0x1A0A2A, 0x00F0FF, 0x1845, 0x079F, 0x079F, true},
    {13, "Ocean Blue", 0x001428, 0x00D2FF, 0x00A5, 0x069F, 0x069F, true},
    {14, "Forest Moss", 0x0D1A10, 0xA8E063, 0x08C2, 0xAF0C, 0xAF0C, true},
    {15, "Blood Moon", 0x150000, 0xFF3333, 0x1000, 0xF986, 0xF986, true},
    {16, "Sunset Gold", 0x1A091A, 0xFFAA33, 0x1843, 0xFD46, 0xFD46, true},
    {17, "Nordic Frost", 0x2E3440, 0x88C0D0, 0x29A8, 0x8E1A, 0x8E1A, true},
    {18, "Dracula", 0x282A36, 0xBD93F9, 0x2946, 0xBC9F, 0xBC9F, true},
    {19, "Chalkboard", 0x233227, 0xE8F5E9, 0x2184, 0xEFBD, 0xEFBD, true},
    {20, "Monokai", 0x272822, 0xE6DB74, 0x2144, 0xE6CE, 0xE6CE, true},
    {21, "Terminal White", 0x0F0F0F, 0xF0F0F0, 0x0861, 0xF79E, 0xFFFF, true},
    {22, "Notebook", 0xFAF8F5, 0x1A365D, 0xFFDE, 0x19AB, 0x19AB, false},
    {23, "Graphite", 0xECEFF1, 0x37474F, 0xEF7E, 0x3229, 0x3229, false}};

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

inline bool isPixelArtPacket(const uint8_t *payload, size_t length)
{
    if (!payload || length < 2)
        return false;

    // 1. Check for Meshtastic File Transfer (MFT) magic: "MFT\x01" (0x4D, 0x46, 0x54, 0x01)
    if (length >= 4 && payload[0] == 0x4D && payload[1] == 0x46 && payload[2] == 0x54 && payload[3] == 0x01) {
        return false;
    }

    // 2. Validate Pixel Art Header byte
    uint8_t header = payload[0];
    uint8_t enc = (header >> 4) & 0x0F;
    uint8_t preset = header & 0x0F;

    if (enc > 6)
        return false;
    if (preset > 9)
        return false;

    return true;
}

class BitReader
{
  public:
    const uint8_t *data;
    size_t length;
    size_t bitPos;

    BitReader(const uint8_t *d, size_t len) : data(d), length(len), bitPos(0) {}

    bool hasBits() const { return bitPos < length * 8; }

    bool readBit()
    {
        if (bitPos >= length * 8)
            return false;
        size_t byteIdx = bitPos / 8;
        int bitOffset = 7 - (bitPos % 8);
        bitPos++;
        return (data[byteIdx] >> bitOffset) & 1;
    }

    uint32_t readBits(int count)
    {
        uint32_t val = 0;
        for (int i = 0; i < count; i++) {
            val = (val << 1) | (readBit() ? 1 : 0);
        }
        return val;
    }
};

class Decoder
{
  private:
    static int readVarRleRun(BitReader &reader)
    {
        uint32_t tag2 = reader.readBits(2);
        switch (tag2) {
        case 0b00:
            return 1;
        case 0b01:
            return 2 + reader.readBits(1);
        case 0b10: {
            bool b = reader.readBit();
            return b ? (8 + reader.readBits(3)) : (4 + reader.readBits(2));
        }
        default: {
            uint32_t tag4 = reader.readBits(2);
            switch (tag4) {
            case 0b00:
                return 16 + reader.readBits(4);
            case 0b01:
                return 32 + reader.readBits(5);
            case 0b10:
                return 64 + reader.readBits(8);
            default:
                return 256 + reader.readBits(12);
            }
        }
        }
    }

    static void decodeVarRle(BitReader &reader, bool *out, int count)
    {
        if (!reader.hasBits())
            return;
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

    static void decodeBlock4x4(BitReader &reader, bool *out, int width, int height)
    {
        int bxCount = (width + 3) / 4;
        int byCount = (height + 3) / 4;
        for (int by = 0; by < byCount; by++) {
            for (int bx = 0; bx < bxCount; bx++) {
                int startX = bx * 4;
                int startY = by * 4;
                bool isMixed = reader.readBit();
                if (!isMixed) {
                    // Solid 0
                } else {
                    bool isAllOne = !reader.readBit();
                    if (isAllOne) {
                        for (int py = 0; py < 4; py++) {
                            for (int px = 0; px < 4; px++) {
                                int x = startX + px;
                                int y = startY + py;
                                if (x < width && y < height)
                                    out[y * width + x] = true;
                            }
                        }
                    } else {
                        for (int py = 0; py < 4; py++) {
                            for (int px = 0; px < 4; px++) {
                                int x = startX + px;
                                int y = startY + py;
                                if (x < width && y < height)
                                    out[y * width + x] = reader.readBit();
                            }
                        }
                    }
                }
            }
        }
    }

    static void decodeBlock8x8(BitReader &reader, bool *out, int width, int height)
    {
        int bx8Count = (width + 7) / 8;
        int by8Count = (height + 7) / 8;
        for (int by8 = 0; by8 < by8Count; by8++) {
            for (int bx8 = 0; bx8 < bx8Count; bx8++) {
                int startX8 = bx8 * 8;
                int startY8 = by8 * 8;
                bool isMixed8 = reader.readBit();
                if (!isMixed8) {
                    // Solid 0
                } else {
                    bool isAllOne8 = !reader.readBit();
                    if (isAllOne8) {
                        for (int py = 0; py < 8; py++) {
                            for (int px = 0; px < 8; px++) {
                                int x = startX8 + px;
                                int y = startY8 + py;
                                if (x < width && y < height)
                                    out[y * width + x] = true;
                            }
                        }
                    } else {
                        for (int subY = 0; subY < 2; subY++) {
                            for (int subX = 0; subX < 2; subX++) {
                                int startX4 = startX8 + subX * 4;
                                int startY4 = startY8 + subY * 4;
                                bool isMixed4 = reader.readBit();
                                if (!isMixed4) {
                                    // Sub-block solid 0
                                } else {
                                    bool isAllOne4 = !reader.readBit();
                                    if (isAllOne4) {
                                        for (int py = 0; py < 4; py++) {
                                            for (int px = 0; px < 4; px++) {
                                                int x = startX4 + px;
                                                int y = startY4 + py;
                                                if (x < width && y < height)
                                                    out[y * width + x] = true;
                                            }
                                        }
                                    } else {
                                        for (int py = 0; py < 4; py++) {
                                            for (int px = 0; px < 4; px++) {
                                                int x = startX4 + px;
                                                int y = startY4 + py;
                                                if (x < width && y < height)
                                                    out[y * width + x] = reader.readBit();
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

    static void decodeDelta2D(BitReader &reader, bool *out, int width, int height)
    {
        static bool residuals[1584];
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
                    bool top = out[(y - 1) * width + x];
                    bool diag = out[(y - 1) * width + (x - 1)];
                    pred = (left == top) ? left : (diag ^ left ^ top);
                }
                out[i] = residuals[i] ^ pred;
            }
        }
    }

    static void decodeLzss(BitReader &reader, bool *out, int pixelCount)
    {
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
                byteBuf[outPos++] = static_cast<uint8_t>(reader.readBits(8));
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
    static bool decode(const uint8_t *data, size_t length, DecodedImage *out)
    {
        if (!data || length < 2 || !out)
            return false;

        uint8_t header = data[0];
        uint8_t enc = (header >> 4) & 0x0F;
        uint8_t presetIdx = header & 0x0F;

        if (enc > 6 || presetIdx > 9)
            return false;

        const Preset &preset = PRESETS[presetIdx];
        out->presetIndex = presetIdx;
        out->width = preset.width;
        out->height = preset.height;
        out->pixelCount = preset.width * preset.height;
        memset(out->pixels, 0, sizeof(out->pixels));

        const uint8_t *payload = data + 1;
        size_t payloadLen = length - 1;
        BitReader reader(payload, payloadLen);

        switch (enc) {
        case 0: // ENC_RAW
            for (int i = 0; i < out->pixelCount; i++) {
                int byteIdx = i / 8;
                if (static_cast<size_t>(byteIdx) < payloadLen) {
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
            static bool temp[1584];
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
            if (out->themeIndex >= 24)
                out->themeIndex = 0;
        } else {
            // Legacy packet fallback
            out->showGrid = false;
            out->themeIndex = 0;
        }

        return true;
    }
};

} // namespace PixelArt
