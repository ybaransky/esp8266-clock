#pragma once

// Holds the coordinates read for a matching five-digit ZIP-code record. The ZIP
// itself is not repeated back: a lookup only ever succeeds on the code the
// caller asked for.
struct ZipcodeLocation {
  float latitude;   // ZIP centroid latitude, to 0.01 degrees.
  float longitude;  // ZIP centroid longitude, to 0.01 degrees.
};

bool zipcodeLookupLocation(const char* zipcode, ZipcodeLocation* location,
                           const char* path = "/zipcodes.bin");
bool isValidZipcode(const char* zipcode);
