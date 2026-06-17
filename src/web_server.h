#pragma once

#include <Arduino.h>

void webBegin();
void webHandleClients();
void networkGetInfo(String &ssid, String &ip);
