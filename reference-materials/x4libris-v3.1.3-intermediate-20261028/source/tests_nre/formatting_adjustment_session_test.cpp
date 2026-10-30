#include <cassert>
#include "../src/activities/reader/FormattingAdjustmentSession.h"
using FormattingAdjustmentSession::StepOffset;
int main() {
  StepOffset publisher(0);
  assert(publisher.atOrigin() && publisher.valueAtOrigin() == 0);
  assert(publisher.advance(1));
  assert(publisher.advance(1));
  assert(publisher.advance(-1));
  assert(publisher.advance(-1));
  assert(publisher.atOrigin() && publisher.valueAtOrigin() == 0);
  StepOffset inherited(-1);
  assert(inherited.advance(1));
  assert(inherited.advance(-1));
  assert(inherited.atOrigin() && inherited.valueAtOrigin() == -1);
  StepOffset explicitValue(4);
  assert(explicitValue.advance(-1));
  assert(explicitValue.advance(1));
  assert(explicitValue.atOrigin() && explicitValue.valueAtOrigin() == 4);
  assert(!explicitValue.advance(0));
  for (int i = 0; i < 127; ++i) assert(explicitValue.advance(1));
  assert(!explicitValue.advance(1));
  explicitValue.reset();
  assert(explicitValue.atOrigin());
}
