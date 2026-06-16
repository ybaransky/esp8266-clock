#pragma once

#include <Arduino.h>

void webBegin(const char *ssid, const char *password);
void webHandleClients();
void networkGetInfo(String &ssid, String &ip);
