#pragma once

// Holds the coordinates parsed for a matching five-digit ZIP-code record.
struct ZipcodeLocation {
  char zipcode[6];  // Five-digit ZIP plus terminator.
  float latitude;   // ZIP centroid latitude.
  float longitude;  // ZIP centroid longitude.
};

bool zipcodeLookupLocation(const char* zipcode, ZipcodeLocation* location,
                           const char* path = "/zipcodes.txt");
bool isValidZipcode(const char* zipcode);
