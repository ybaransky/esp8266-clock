"""Applies C++-only compiler flags.

platformio.ini's `build_flags` reaches both the C and C++ compilers, so a
C++-only warning switch put there makes cc1 emit "valid for C++/ObjC++ but not
for C" on every C translation unit. Appending to CXXFLAGS here scopes it
correctly, which keeps a clean build genuinely clean.

Currently one flag:

  -Wno-deprecated-copy
      RTClib declares DateTime's copy constructor but not its assignment
      operator, so every `someDateTime = other` in this project trips
      -Wdeprecated-copy. Eleven instances of a third-party defect we cannot fix
      would drown the warnings that are ours, which is the point of -Wall.
"""

CXX_ONLY_FLAGS = ["-Wno-deprecated-copy"]

try:
    Import("env")  # noqa: F821
    env.Append(CXXFLAGS=CXX_ONLY_FLAGS)  # noqa: F821
except NameError:
    pass
