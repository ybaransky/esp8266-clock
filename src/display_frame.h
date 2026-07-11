#pragma once

#include <stddef.h>

constexpr size_t kDisplayPanelCount = 3;

// Rendered text for one four-character panel may include formatting markers
// such as ':' or '.', so each row has room beyond its four visible slots.
constexpr size_t kDisplayFrameRowSize = 8;

// Hardware-independent output of the display rendering layer.
struct DisplayFrame {
  char rows[kDisplayPanelCount][kDisplayFrameRowSize] = {};
};
