#pragma once

#include <cstdint>

namespace notes::navigation {

enum class View : uint8_t { Deck, Note, Menu, Confirm, Notice };

struct BackResult {
  View view;
  bool exitActivity;
};

// One Back press removes exactly one Notes screen. Leaving the deck pops the
// Notes activity, returning to Applications; the Applications folder handles
// its own next Back to Home.
inline BackResult previous(View view, const bool hasOpenNote) {
  switch (view) {
    case View::Deck:
      return {View::Deck, true};
    case View::Note:
      return {View::Deck, false};
    case View::Menu:
    case View::Confirm:
      return {View::Note, false};
    case View::Notice:
      return {hasOpenNote ? View::Note : View::Deck, false};
  }
  return {View::Deck, true};
}

}  // namespace notes::navigation
