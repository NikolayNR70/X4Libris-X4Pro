#pragma once

// Tracks an individual setting edit session without changing the persistent
// representation used by Witchhunt v1.  The original EPUB style is encoded by
// a zero persisted choice; -1 (global inheritance) is intentionally different.
// A session started with an existing explicit choice restores that choice if
// the user undoes all adjustments.  A session started at publisher CSS stays
// at publisher CSS when returning to offset zero, regardless of which CSS
// values appear in different paragraphs.
namespace FormattingAdjustmentSession {
class StepOffset {
 public:
  explicit StepOffset(int persistedChoice) : initialChoice_(persistedChoice) {}

  // Signed steps around the original state; the UI can display the actual
  // temporary choice only when a valid list is available.  No CSS measurement
  // or numeric substitute for publisher CSS is assumed here.
  int offset() const { return offset_; }
  int initialChoice() const { return initialChoice_; }
  int valueAtOrigin() const { return initialChoice_; }
  bool atOrigin() const { return offset_ == 0; }

  bool advance(int direction) {
    if (direction != 1 && direction != -1) return false;
    if ((offset_ == 127 && direction > 0) || (offset_ == -127 && direction < 0)) return false;
    offset_ += direction;
    return true;
  }

  void reset() { offset_ = 0; }

 private:
  int initialChoice_;
  int offset_ = 0;
};
}  // namespace FormattingAdjustmentSession
