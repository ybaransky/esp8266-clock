#pragma once

#include <Arduino.h>

class CommaNumber {
 public:
  explicit CommaNumber(uint32_t value) {
    formatUnsignedWithCommas(value, text_, sizeof(text_));
  }

  const char* c_str() const {
    return text_;
  }

 private:
  static void formatUnsignedWithCommas(uint32_t value, char* out, size_t outSize) {
    if (outSize == 0) {
      return;
    }

    char raw[11];
    snprintf(raw, sizeof(raw), "%lu", static_cast<unsigned long>(value));

    const size_t rawLength = strlen(raw);
    const size_t commaCount = rawLength > 0 ? (rawLength - 1) / 3 : 0;
    const size_t required = rawLength + commaCount + 1;
    if (required > outSize) {
      out[0] = '\0';
      return;
    }

    size_t readIndex = rawLength;
    size_t writeIndex = required - 1;
    uint8_t digitGroup = 0;
    out[writeIndex] = '\0';

    while (readIndex > 0) {
      if (digitGroup == 3) {
        out[--writeIndex] = ',';
        digitGroup = 0;
      }
      out[--writeIndex] = raw[--readIndex];
      ++digitGroup;
    }
  }

  char text_[15] = {};  // Comma-formatted decimal text.
};
