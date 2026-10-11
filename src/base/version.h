// LoopMaxPro - comparing two versions: "1.0.1" after "1.0.0", "1.0.0" after "1.0.0-dev".
// A firmware carries two kinds of versions, the one of LoopMaxPro and the one of each module,
// and an update is offered when either is newer: both are compared with this.
#pragma once
#include <stdlib.h>
#include <string.h>

namespace lmp {

// Below zero, zero, above zero: a before b, the same, a after b. Up to four numbers separated
// by dots; what follows them ("-dev") makes a version older than the same numbers alone, and
// two of them are compared as text. A text that is not a version counts as 0.
inline int versionCompare(const char* a, const char* b) {
  if (!a) a = "";
  if (!b) b = "";
  for (int field = 0; field < 4; ++field) {
    char* endA = nullptr;
    char* endB = nullptr;
    const long x = (*a >= '0' && *a <= '9') ? strtol(a, &endA, 10) : 0;
    const long y = (*b >= '0' && *b <= '9') ? strtol(b, &endB, 10) : 0;
    if (x != y) return x < y ? -1 : 1;
    if (endA) a = endA;
    if (endB) b = endB;
    const bool moreA = *a == '.';
    const bool moreB = *b == '.';
    if (moreA) ++a;
    if (moreB) ++b;
    if (!moreA && !moreB) break;
  }
  // What is left: nothing is a release, something is what comes before it.
  if (!*a && !*b) return 0;
  if (!*a) return 1;
  if (!*b) return -1;
  const int text = strcmp(a, b);
  return text < 0 ? -1 : text > 0 ? 1 : 0;
}

inline bool versionNewer(const char* candidate, const char* installed) {
  return versionCompare(candidate, installed) > 0;
}

}  // namespace lmp
