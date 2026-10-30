#include <cassert>
#include "../src/activities/reader/FormattingNeutralStep.h"
using FormattingNeutralStep::next;
int main() {
  assert(next(0, 1, 4, 11) == 5);
  assert(next(5, -1, 4, 11) == 0);
  assert(next(0, -1, 4, 11) == 4);
  assert(next(4, 1, 4, 11) == 0);
  assert(next(6, -1, 4, 11) == 5);
  assert(next(3, 1, 4, 11) == 4);
  assert(next(1, -1, 4, 11) == 1);
  assert(next(11, 1, 4, 11) == 11);
  assert(next(-1, 1, 4, 11) == -1);
  assert(next(0, 0, 4, 11) == 0);
}
