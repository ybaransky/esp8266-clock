#include "page_manager.h"

#include "display_manager.h"

namespace {

constexpr uint16_t kNetworkInfoPageMs = 3000;

void copyPageRow(DisplayPage& page, uint8_t row, const char* text) {
  snprintf(page.rows[row], sizeof(page.rows[row]), "%-4.4s", text);
}

char displaySafeChar(char value) {
  const uint8_t code = static_cast<uint8_t>(value);
  return (code >= 32 && code <= 126) ? value : '-';
}

uint8_t appendSsidPages(DisplayPage* pages, uint8_t pageCount, const String& ssid) {
  char text[49] = {};
  const uint8_t sourceLength =
      ssid.length() < sizeof(text) - 1 ? ssid.length() : sizeof(text) - 1;
  for (uint8_t index = 0; index < sourceLength; ++index) {
    text[index] = displaySafeChar(ssid[index]);
  }

  const uint8_t textLength = sourceLength == 0 ? 4 : sourceLength;
  if (sourceLength == 0) {
    snprintf(text, sizeof(text), "NONE");
  }

  for (uint8_t offset = 0;
       offset < textLength && pageCount < kMaxDisplayPages;
       offset += 8) {
    copyPageRow(pages[pageCount], 0, "SSid");
    copyPageRow(pages[pageCount], 1, text + offset);
    copyPageRow(pages[pageCount], 2, offset + 4 < textLength ? text + offset + 4 : "");
    ++pageCount;
  }

  return pageCount;
}

bool parseIpOctets(const String& ip, uint8_t octets[4]) {
  unsigned int parsed[4] = {};
  if (sscanf(ip.c_str(), "%u.%u.%u.%u",
             &parsed[0], &parsed[1], &parsed[2], &parsed[3]) != 4) {
    return false;
  }

  for (uint8_t index = 0; index < 4; ++index) {
    if (parsed[index] > 255) {
      return false;
    }
    octets[index] = static_cast<uint8_t>(parsed[index]);
  }
  return true;
}

void formatIpOctet(char row[kDisplayRowChars + 1], uint8_t octet) {
  snprintf(row, kDisplayRowChars + 1, "%4u", octet);
}

uint8_t appendIpPages(DisplayPage* pages, uint8_t pageCount, const String& ip) {
  if (pageCount >= kMaxDisplayPages) {
    return pageCount;
  }

  uint8_t octets[4] = {};
  if (!parseIpOctets(ip, octets)) {
    copyPageRow(pages[pageCount], 0, "IP");
    copyPageRow(pages[pageCount], 1, "ERR");
    copyPageRow(pages[pageCount], 2, "");
    return pageCount + 1;
  }

  copyPageRow(pages[pageCount], 0, "IP");
  formatIpOctet(pages[pageCount].rows[1], octets[0]);
  formatIpOctet(pages[pageCount].rows[2], octets[1]);
  ++pageCount;

  if (pageCount >= kMaxDisplayPages) {
    return pageCount;
  }

  copyPageRow(pages[pageCount], 0, "IP");
  formatIpOctet(pages[pageCount].rows[1], octets[2]);
  formatIpOctet(pages[pageCount].rows[2], octets[3]);
  return pageCount + 1;
}

}  // namespace

void PageManager::showSsid(const String& ssid) {
  DisplayPage pages[kMaxDisplayPages];
  const uint8_t pageCount = appendSsidPages(pages, 0, ssid);
  displayManager.showPages(pages, pageCount, kNetworkInfoPageMs);
}

void PageManager::showIpAddress(const String& ip) {
  DisplayPage pages[kMaxDisplayPages];
  const uint8_t pageCount = appendIpPages(pages, 0, ip);
  displayManager.showPages(pages, pageCount, kNetworkInfoPageMs);
}
