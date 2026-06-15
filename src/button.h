#pragma once

enum class ButtonEvent {
  NONE,
  SHOW_MENU_OR_RECENTER_HISTOGRAM,
  SHOW_NETWORK_INFO,
  SHOW_I2C_SCAN,
  SHOW_RTC_STATUS,
  TOGGLE_PRIMARY_PANEL,
  RESET_CURRENT_PANEL_DATA,
};

void buttonBegin();
void buttonTick();
void buttonLedTick(unsigned long now);
bool buttonHasEvent();
ButtonEvent buttonNextEvent();
