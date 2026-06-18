#pragma once

struct ZipcodeLocation {
  char zipcode[6];
  float latitude;
  float longitude;
};

bool zipcodeLookupLocation(const char* zipcode, ZipcodeLocation* location,
                           const char* path = "/zipcodes.txt");
bool isValidZipcode(const char* zipcode);
