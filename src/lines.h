// Newline-separated lists, as cars.cpp and drivers.cpp store them in Preferences.
#pragma once
#include <Arduino.h>

// Next line of `all` starting at `pos` (start with 0). False when no line is left;
// an empty string is one empty line.
inline bool nextLine(const String& all, int& pos, String& line) {
  if (pos > (int)all.length()) return false;
  int end = all.indexOf('\n', pos);
  if (end < 0) end = all.length();
  line = all.substring(pos, end);
  pos = end + 1;
  return true;
}

// The first `max` lines of `all` into out[]; missing lines stay empty.
inline void splitLines(const String& all, String* out, int max) {
  int pos = 0;
  for (int i = 0; i < max; i++) if (!nextLine(all, pos, out[i])) out[i] = "";
}
