#pragma once
#include "activities/MenuListActivity.h"
#include "RecentBooksStore.h"
#include <array>

class ReaderFormattingActivity final : public MenuListActivity {
 public:
  ReaderFormattingActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& path);
  void render(RenderLock&&) override;
 private:
  enum Field { Font, Size, Hyphens, Align, Line, Indent, Gap, HeadingAlign, HeadingSize, Margin,
               Bionic, AA, Normalize, Dots, Images, Notes, Orientation, Darkness, FieldCount };
  struct Binding {
    ReaderFormattingActivity* owner = nullptr;
    Field field = Font;
    std::vector<int> values;
    std::vector<std::string> labels;
  };
  std::array<Binding, FieldCount> bindings;
  RecentBook book;
  // The quick formatting screen edits shared reader settings only.
  bool global = true;
  void buildItems();
  int get(Field field) const;
  void set(Field field, int value);
  void save();
  SettingInfo row(Field field, StrId label, std::vector<int> values, std::vector<std::string> labels);
  void onActionSelected(int index) override;
  void openSubmenu(const SettingInfo& entry) override;
};
