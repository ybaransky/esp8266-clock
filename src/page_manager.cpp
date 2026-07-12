#include "page_manager.h"

#include "display_manager.h"

namespace {

constexpr uint16_t kNetworkInfoPageMs = 3000;

void copyPagePanel(DisplayPage& page, uint8_t panel, const char* text) {
  snprintf(page.panels[panel], sizeof(page.panels[panel]), "%-4.4s", text);
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
    copyPagePanel(pages[pageCount], 0, "SSid");
    copyPagePanel(pages[pageCount], 1, text + offset);
    copyPagePanel(pages[pageCount], 2, offset + 4 < textLength ? text + offset + 4 : "");
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

void formatIpOctet(char panel[kDisplayPanelChars + 1], uint8_t octet) {
  snprintf(panel, kDisplayPanelChars + 1, "%4u", octet);
}

uint8_t appendIpPages(DisplayPage* pages, uint8_t pageCount, const String& ip) {
  if (pageCount >= kMaxDisplayPages) {
    return pageCount;
  }

  uint8_t octets[4] = {};
  if (!parseIpOctets(ip, octets)) {
    copyPagePanel(pages[pageCount], 0, "IP");
    copyPagePanel(pages[pageCount], 1, "ERR");
    copyPagePanel(pages[pageCount], 2, "");
    return pageCount + 1;
  }

  copyPagePanel(pages[pageCount], 0, "IP");
  formatIpOctet(pages[pageCount].panels[1], octets[0]);
  formatIpOctet(pages[pageCount].panels[2], octets[1]);
  ++pageCount;

  if (pageCount >= kMaxDisplayPages) {
    return pageCount;
  }

  copyPagePanel(pages[pageCount], 0, "IP");
  formatIpOctet(pages[pageCount].panels[1], octets[2]);
  formatIpOctet(pages[pageCount].panels[2], octets[3]);
  return pageCount + 1;
}

}  // namespace

// -----------------------------------------------------------------------------
// PageManager
// -----------------------------------------------------------------------------

void PageManager::showSsid(const String& ssid) {
  DisplayPage pages[kMaxDisplayPages];
  const uint8_t pageCount = appendSsidPages(pages, 0, ssid);
  displayManager_.showPages(pages, pageCount, kNetworkInfoPageMs);
}

void PageManager::showIpAddress(const String& ip) {
  DisplayPage pages[kMaxDisplayPages];
  const uint8_t pageCount = appendIpPages(pages, 0, ip);
  displayManager_.showPages(pages, pageCount, kNetworkInfoPageMs);
}
