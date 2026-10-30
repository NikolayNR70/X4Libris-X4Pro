#include <initializer_list>
#include <cassert>
#include "../src/activities/reader/FormattingNeutralStep.h"

int main() {
  using FormattingNeutralStep::next;
  using FormattingNeutralStep::restoreIfOriginalChoice;
  assert(restoreIfOriginalChoice(4, 4) == 0);
  assert(restoreIfOriginalChoice(4, 5) == 4);
  assert(restoreIfOriginalChoice(4, -1) == 4);
  assert(restoreIfOriginalChoice(1, 1) == 0);
  assert(restoreIfOriginalChoice(0, 4) == 0);
  assert(restoreIfOriginalChoice(-1, 4) == -1);
  for (int neutral = 1; neutral < 10; ++neutral) {
    const int max = 11;
    assert(next(0, +1, neutral, max) == neutral + 1);
    assert(next(0, -1, neutral, max) == neutral);
    assert(next(neutral, +1, neutral, max) == 0);
    assert(next(neutral + 1, -1, neutral, max) == 0);
    assert(next(-1, +1, neutral, max) == -1);
    for (int n = 1; n <= max; ++n) {
      for (int dir : {-1, 1}) {
        int result = next(n, dir, neutral, max);
        assert(result >= 0 && result <= max);
      }
    }
  }
}
