#include <gtest/gtest.h>

#include <string>

#include "activities/apps/AppInputMode.h"
#include "activities/apps/TodaySleepScreen.h"
#include "activities/notes/NotesCore.h"
#include "activities/notes/NotesNavigation.h"

namespace {

TEST(TodaySleepScreen, GeneralSelectorOmitsNotesButKeepsToday) {
  EXPECT_EQ(today_sleep::selectorIndexForMode(today_sleep::kNoteMode), UINT8_MAX);
  const uint8_t todayIndex = today_sleep::selectorIndexForMode(today_sleep::kTodayMode);
  ASSERT_LT(todayIndex, today_sleep::kSelectorModeCount);
  EXPECT_EQ(today_sleep::kSelectorModes[todayIndex], today_sleep::kTodayMode);
  EXPECT_EQ(today_sleep::kSelectorModeCount, 10);
}

TEST(AppInputMode, ButtonNavigationWorksWithoutTouchOrWhenTouchIsDisabled) {
  EXPECT_TRUE(app_input::useButtonNavigation(false, true));
  EXPECT_TRUE(app_input::useButtonNavigation(true, false));
  EXPECT_FALSE(app_input::useButtonNavigation(true, true));
}

TEST(NotesCore, ParsesAndTogglesOnlyTheTaskMarkerByte) {
  std::string document = "  - [ ] tea  \nKeep two spaces.\n* [X] milk\n";
  const auto lines = notes::parse(document);
  ASSERT_EQ(lines.size(), 4u);  // The trailing newline is retained as an empty final line.
  ASSERT_TRUE(lines[0].isTask);
  const std::string before = document;
  auto task = lines[0];
  ASSERT_TRUE(notes::toggle(document, task));
  EXPECT_EQ(document.size(), before.size());
  EXPECT_EQ(document[task.markAt], 'x');
  EXPECT_EQ(document.substr(0, task.markAt), before.substr(0, task.markAt));
  EXPECT_EQ(document.substr(task.markAt + 1), before.substr(task.markAt + 1));
}

TEST(NotesCore, CoercesPlainLinesIntoChecklistAndClearsCheckedRows) {
  std::string document = "Tea\n- [x] Milk\n\nBread\n";
  EXPECT_TRUE(notes::coerceToList(document));
  EXPECT_EQ(document, "- [ ] Tea\n- [x] Milk\n\n- [ ] Bread\n");
  const auto removed = notes::clearChecked(document);
  ASSERT_EQ(removed.size(), 1u);
  EXPECT_EQ(removed.front(), "Milk");
  EXPECT_EQ(document, "- [ ] Tea\n\n- [ ] Bread\n");
}

TEST(NotesCore, SleepScreenChoiceRoundTripsNoteAndPreviousSettings) {
  notes::AsleepChoice choice;
  choice.name = "Evening note";
  choice.previousMode = 4;
  choice.previousQuickResume = 1;
  notes::AsleepChoice decoded;
  ASSERT_TRUE(notes::parseAsleep(notes::formatAsleep(choice), decoded));
  EXPECT_EQ(decoded.name, choice.name);
  EXPECT_EQ(decoded.previousMode, choice.previousMode);
  EXPECT_EQ(decoded.previousQuickResume, choice.previousQuickResume);
}

TEST(NotesCore, SleepScreenChoiceRejectsEmptyNameAndToleratesLegacyRecord) {
  notes::AsleepChoice decoded;
  EXPECT_FALSE(notes::parseAsleep("\n4\n1\n", decoded));
  ASSERT_TRUE(notes::parseAsleep("Old note\n", decoded));
  EXPECT_EQ(decoded.name, "Old note");
  EXPECT_EQ(decoded.previousMode, -1);
  EXPECT_EQ(decoded.previousQuickResume, -1);
}

TEST(NotesNavigation, BackUnwindsNoteAndDeckThenExitsToParentFolder) {
  using notes::navigation::View;
  auto result = notes::navigation::previous(View::Note, true);
  EXPECT_EQ(result.view, View::Deck);
  EXPECT_FALSE(result.exitActivity);
  result = notes::navigation::previous(result.view, false);
  EXPECT_TRUE(result.exitActivity);
  EXPECT_EQ(result.view, View::Deck);
}

TEST(NotesNavigation, BackFromMenuAndConfirmationReturnsToOpenNote) {
  using notes::navigation::View;
  EXPECT_EQ(notes::navigation::previous(View::Menu, true).view, View::Note);
  EXPECT_EQ(notes::navigation::previous(View::Confirm, true).view, View::Note);
}

TEST(NotesNavigation, NoticeReturnsToItsOriginWithoutCycling) {
  using notes::navigation::View;
  EXPECT_EQ(notes::navigation::previous(View::Notice, true).view, View::Note);
  const auto result = notes::navigation::previous(View::Notice, false);
  EXPECT_EQ(result.view, View::Deck);
  EXPECT_FALSE(result.exitActivity);
}

}  // namespace
