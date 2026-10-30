#pragma once

// A button-driven numeric formatting control can have a neutral (publisher CSS)
// point without exposing an extra "Book style" selector. The persisted zero
// retains its v1 meaning: no user override. Positive values are legacy choice
// IDs, not physical percentages. This helper does not change stored formats.
//
// The neutral point lies BETWEEN positive choices: neutralChoice is the choice
// immediately below neutral, so Up moves to neutralChoice+1 and Down moves to
// neutralChoice. Crossing that boundary from either side clears the override.
namespace FormattingNeutralStep {
// The caller must provide a baseline from the actual computed style of this
// book. Never substitute a fixed 100% for varied EPUB publisher CSS.
inline int restoreIfOriginalChoice(int selectedChoice, int computedOriginalChoice) {
  if (computedOriginalChoice > 0 && selectedChoice == computedOriginalChoice) return 0;
  return selectedChoice;
}
inline int next(int storedChoice, int direction, int neutralChoice, int maxChoice) {
  if (direction != -1 && direction != 1) return storedChoice;
  if (neutralChoice < 1 || neutralChoice >= maxChoice) return storedChoice;
  if (storedChoice < 0 || storedChoice > maxChoice) return storedChoice;
  if (storedChoice == 0) return direction > 0 ? neutralChoice + 1 : neutralChoice;
  if (storedChoice == neutralChoice && direction > 0) return 0;
  if (storedChoice == neutralChoice + 1 && direction < 0) return 0;
  const int result = storedChoice + direction;
  return (result >= 1 && result <= maxChoice) ? result : storedChoice;
}
}  // namespace FormattingNeutralStep
