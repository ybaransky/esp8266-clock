#include "display.h"

#include "hardware.h"
#include <TM1637Display.h>

namespace {

constexpr uint8_t SEG_BLANK = 0x00;
constexpr uint8_t SEG_MINUS = 0x40;
constexpr uint8_t SEG_COLON = 0x80;
constexpr uint8_t SEG_VALUE_MASK = 0x7F;
constexpr size_t PANEL_COUNT = 3;
constexpr size_t PANEL_WIDTH = 4;
TM1637Display displays[PANEL_COUNT] = {
    TM1637Display(Hardware::Pins::SEGMENT_CLK, Hardware::Pins::SEGMENT_DIO[0]),
    TM1637Display(Hardware::Pins::SEGMENT_CLK, Hardware::Pins::SEGMENT_DIO[1]),
    TM1637Display(Hardware::Pins::SEGMENT_CLK, Hardware::Pins::SEGMENT_DIO[2]),
};

constexpr uint8_t ASCII_SEGMENTS[96] = {
  /*       a
   *      ---
   *  f |  g | b
   *      ---
   *  e |    | c
   *      ---
   *       d
   * TM1637Display encoding is 0bXGFEDCBA. On this hardware X controls the
   * panel's center colon when set on the second digit; there are no decimals.
   */
0b00000000, /* (space) this is 32 */
0b10000110, /* ! */
0b00100010, /* " */
0b01111110, /* # */
0b01101101, /* $ */
0b11010010, /* % */
0b01000110, /* & */
0b00100000, /* ' */
0b00101001, /* ( */
0b00001011, /* ) */
0b00100001, /* * */
0b01110000, /* + */
0b00010000, /* , */
0b01000000, /* - */
0b00000000, /* . (not supported by this hardware) */
0b01010010, /* / */
0b00111111, /* 0 */
0b00000110, /* 1 */
0b01011011, /* 2 */
0b01001111, /* 3 */
0b01100110, /* 4 */
0b01101101, /* 5 */
0b01111101, /* 6 */
0b00000111, /* 7 */
0b01111111, /* 8 */
0b01101111, /* 9 */
0b00001001, /* : */
0b00001101, /* ; */
0b01100001, /* < */
0b01001000, /* = */
0b01000011, /* > */
0b11010011, /* ? */
0b01011111, /* @ */
0b01110111, /* A */
0b01111100, /* B */
0b00111001, /* C */
0b01011110, /* D */
0b01111001, /* E */
0b01110001, /* F */
0b00111101, /* G */
0b01110110, /* H */
0b00110000, /* I */
0b00011110, /* J */
0b01110101, /* K */
0b00111000, /* L */
0b00010101, /* M */
0b00110111, /* N */
0b00111111, /* O */
0b01110011, /* P */
0b01101011, /* Q */
0b00110011, /* R */
0b01101101, /* S */
0b00110001, /* T */
0b00111110, /* U */
0b00111110, /* V */
0b00101010, /* W */
0b01110110, /* X */
0b01101110, /* Y */
0b01011011, /* Z */
0b00111001, /* [ */
0b01100100, /* \ */
0b00001111, /* ] */
0b00100011, /* ^ */
0b00001000, /* _ */
0b00000010, /* ` */
0b01011111, /* a */
0b01111100, /* b */
0b01011000, /* c */
0b01011110, /* d */
0b01111011, /* e */
0b01110001, /* f */
0b01101111, /* g */
0b01110100, /* h */
0b00010000, /* i */
0b00001100, /* j */
0b01110101, /* k */
0b00110000, /* l */
0b00010100, /* m */
0b01010100, /* n */
0b01011100, /* o */
0b01110011, /* p */
0b01100111, /* q */
0b01010000, /* r */
0b01101101, /* s */
0b01111000, /* t */
0b00011100, /* u */
0b00011100, /* v */
0b00010100, /* w */
0b01110110, /* x */
0b01101110, /* y */
0b01011011, /* z */
0b01000110, /* { */
0b00110000, /* | */
0b01110000, /* } */
0b00000001, /* ~ */
0b00000000, /* (del) */
};
 
uint8_t segmentForChar(char c) {
  const uint8_t code = static_cast<uint8_t>(c);
  if ((code < 32) || (code > 127)) return SEG_BLANK;
  return ASCII_SEGMENTS[code - 32] & SEG_VALUE_MASK;
}

void renderPanelSegments(const char *text, uint8_t segments[PANEL_WIDTH]) {
  for (size_t index = 0; index < PANEL_WIDTH; ++index) {
    segments[index] = SEG_BLANK;
  }

  size_t slot = 0;

  for (size_t index = 0; text[index] != '\0' && slot < PANEL_WIDTH; ++index) {
    const char value = text[index];

    if (((value == ':') || (value == ';')) && (slot == 2)) {
      segments[1] |= SEG_COLON;
      continue;
    }

    segments[slot++] = segmentForChar(value);
  }
}

void copySegments(uint8_t destination[PANEL_WIDTH], const uint8_t source[PANEL_WIDTH]) {
  memcpy(destination, source, PANEL_WIDTH);
}

}  // namespace

// -----------------------------------------------------------------------------
// SegmentDisplay
// -----------------------------------------------------------------------------

void SegmentDisplay::begin(uint8_t brightness) {
  setBrightness(brightness);
  blank();
}

void SegmentDisplay::setBrightness(uint8_t level) {
  const uint8_t clamped = min<uint8_t>(level, 7);
  for (size_t panel = 0; panel < PANEL_COUNT; ++panel) {
    displays[panel].setBrightness(clamped);
    if (cacheValid_[panel]) {
      displays[panel].setSegments(lastSegments_[panel]);
    }
  }
}

void SegmentDisplay::showFrame(const DisplayFrame& frame) {
  for (size_t panel = 0; panel < PANEL_COUNT; ++panel) {
    uint8_t segments[PANEL_WIDTH];
    renderPanelSegments(frame.panels[panel], segments);

    if (!cacheValid_[panel]) {
      displays[panel].setSegments(segments);
      copySegments(lastSegments_[panel], segments);
      cacheValid_[panel] = true;
      continue;
    }

    size_t slot = 0;
    while (slot < PANEL_WIDTH) {
      if (segments[slot] == lastSegments_[panel][slot]) {
        ++slot;
        continue;
      }

      const size_t runStart = slot;
      while ((slot < PANEL_WIDTH) && (segments[slot] != lastSegments_[panel][slot])) {
        ++slot;
      }

      const uint8_t runLength = static_cast<uint8_t>(slot - runStart);
      displays[panel].setSegments(&segments[runStart], runLength,
                                  static_cast<uint8_t>(runStart));
    }

    copySegments(lastSegments_[panel], segments);
  }
}

void SegmentDisplay::blank() {
  const uint8_t blankSegments[PANEL_WIDTH] = {};

  for (size_t panel = 0; panel < PANEL_COUNT; ++panel) {
    displays[panel].setSegments(blankSegments);
    copySegments(lastSegments_[panel], blankSegments);
    cacheValid_[panel] = true;
  }
}
