#include "ReaderFormattingActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <cstdio>

#include "ReaderFontSizes.h"
#include "ReaderOverrideToggle.h"
#include "SdCardFontGlobals.h"
#include "activities/settings/SettingsDashboardActivity.h"
#include "activities/settings/SettingsSubmenuActivity.h"
#include "components/UITheme.h"

ReaderFormattingActivity::ReaderFormattingActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                   const std::string& path)
    : MenuListActivity("ReaderFormatting", renderer, input), book(RECENT_BOOKS.getBookByPath(path)) {
  buildItems();
}

int ReaderFormattingActivity::get(Field f) const {
  if (global) {
    switch (f) {
      case Font: {
        const auto& fs = sdFontSystem.registry().getFamilies();
        for (size_t i = 0; i < fs.size(); ++i)
          if (fs[i].name == SETTINGS.sdFontFamilyName) return 2 + i;
        return SETTINGS.fontFamily;
      }
      case Size:
        return SETTINGS.fontPointSize;
      case Hyphens:
        return SETTINGS.hyphenationEnabled;
      case Align:
        return SETTINGS.paragraphAlignment;
      case Line:
        return SETTINGS.readerLineHeightChoice;
      case Indent:
        return SETTINGS.readerIndentChoice;
      case Gap:
        return SETTINGS.readerGapChoice;
      case HeadingAlign:
        return SETTINGS.headingAlignment;
      case HeadingSize:
        return SETTINGS.readerHeadingSizeChoice;
      case Margin:
        return SETTINGS.readerMarginChoice;
      case Bionic:
        return SETTINGS.bionicReading;
      case AA:
        return SETTINGS.textAntiAliasing;
      case Normalize:
        return SETTINGS.fontSizeNormalization;
      case Dots:
        return SETTINGS.guideDots;
      case Images:
        return SETTINGS.imageRendering;
      case Notes:
        return SETTINGS.inlineFootnotePreviews;
      default:
        break;
    }
  } else {
    switch (f) {
      case Font: {
        const auto& fs = sdFontSystem.registry().getFamilies();
        for (size_t i = 0; i < fs.size(); ++i)
          if (fs[i].name == book.sdFontFamilyOverride) return 2 + i;
        return book.fontFamilyOverride;
      }
      case Size:
        return book.fontSizeOverride;
      case Hyphens:
        return book.hyphenationOverride;
      case Align:
        return book.paragraphAlignmentOverride;
      case Line:
        return book.formatting.lineHeight;
      case Indent:
        return book.formatting.indent;
      case Gap:
        return book.formatting.gap;
      case HeadingAlign:
        return book.formatting.headingAlign;
      case HeadingSize:
        return book.formatting.headingSize;
      case Margin:
        return book.formatting.margin;
      case Bionic:
        return book.bionicReadingOverride;
      case AA:
        return book.textAntiAliasingOverride;
      case Normalize:
        return book.fontSizeNormalizationOverride;
      case Dots:
        return book.guideDotsOverride;
      case Images:
        return book.imageRenderingOverride;
      case Notes:
        return book.inlineFootnotePreviewsOverride;
      default:
        break;
    }
  }
  return f == Orientation ? SETTINGS.orientation : SETTINGS.textDarkness;
}

void ReaderFormattingActivity::set(Field f, int v) {
  if (f == Align || f == Line || f == Indent || f == Gap || f == HeadingAlign || f == HeadingSize || f == Margin)
    v = toggledReaderOverride(get(f), v, global, f == Align ? 4 : 0);
  if (f == Orientation)
    SETTINGS.orientation = v;
  else if (f == Darkness)
    SETTINGS.textDarkness = v;
  else if (global) {
    switch (f) {
      case Font: {
        const auto& fs = sdFontSystem.registry().getFamilies();
        SETTINGS.sdFontFamilyName[0] = '\0';
        if (v < 2)
          SETTINGS.fontFamily = v;
        else if (static_cast<size_t>(v - 2) < fs.size())
          snprintf(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), "%s", fs[v - 2].name.c_str());
        break;
      }
      case Size:
        SETTINGS.fontPointSize = v;
        break;
      case Hyphens:
        SETTINGS.hyphenationEnabled = v;
        break;
      case Align:
        SETTINGS.paragraphAlignment = v;
        break;
      case Line:
        SETTINGS.readerLineHeightChoice = v;
        break;
      case Indent:
        SETTINGS.readerIndentChoice = v;
        break;
      case Gap:
        SETTINGS.readerGapChoice = v;
        break;
      case HeadingAlign:
        SETTINGS.headingAlignment = v;
        break;
      case HeadingSize:
        SETTINGS.readerHeadingSizeChoice = v;
        break;
      case Margin:
        SETTINGS.readerMarginChoice = v;
        break;
      case Bionic:
        SETTINGS.bionicReading = v;
        break;
      case AA:
        SETTINGS.textAntiAliasing = v;
        break;
      case Normalize:
        SETTINGS.fontSizeNormalization = v;
        break;
      case Dots:
        SETTINGS.guideDots = v;
        break;
      case Images:
        SETTINGS.imageRendering = v;
        break;
      case Notes:
        SETTINGS.inlineFootnotePreviews = v;
        break;
      default:
        break;
    }
  } else {
    switch (f) {
      case Font: {
        const auto& fs = sdFontSystem.registry().getFamilies();
        book.sdFontFamilyOverride.clear();
        book.fontFamilyOverride = v < 2 ? v : -1;
        if (v >= 2 && static_cast<size_t>(v - 2) < fs.size()) book.sdFontFamilyOverride = fs[v - 2].name;
        break;
      }
      case Size:
        book.fontSizeOverride = v;
        break;
      case Hyphens:
        book.hyphenationOverride = v;
        break;
      case Align:
        book.paragraphAlignmentOverride = v;
        break;
      case Line:
        book.formatting.lineHeight = v;
        break;
      case Indent:
        book.formatting.indent = v;
        break;
      case Gap:
        book.formatting.gap = v;
        break;
      case HeadingAlign:
        book.formatting.headingAlign = v;
        break;
      case HeadingSize:
        book.formatting.headingSize = v;
        break;
      case Margin:
        book.formatting.margin = v;
        break;
      case Bionic:
        book.bionicReadingOverride = v;
        break;
      case AA:
        book.textAntiAliasingOverride = v;
        break;
      case Normalize:
        book.fontSizeNormalizationOverride = v;
        break;
      case Dots:
        book.guideDotsOverride = v;
        break;
      case Images:
        book.imageRenderingOverride = v;
        break;
      case Notes:
        book.inlineFootnotePreviewsOverride = v;
        break;
      default:
        break;
    }
  }
  save();
  if (f == Font) {
    auto& size = bindings[Size];
    size.values.clear();
    size.labels.clear();
    if (!global) {
      size.values.push_back(-1);
      size.labels.push_back(tr(STR_FORMAT_INHERIT));
    }
    const char* family =
        global ? SETTINGS.sdFontFamilyName : bookSdFontFamily(book.fontFamilyOverride, book.sdFontFamilyOverride);
    const auto sizes = sdFontSystem.sizeListFor(family);
    for (uint8_t i = 0; i < sizes.count; ++i) {
      size.values.push_back(sizes.points[i]);
      size.labels.push_back(fontPointSizeLabel(sizes.points[i]));
    }
  }
}

void ReaderFormattingActivity::save() {
  SETTINGS.saveToFile();
  RECENT_BOOKS.setReaderOverrides(
      book.path, book.embeddedStyleOverride, book.imageRenderingOverride, book.fontFamilyOverride,
      book.sdFontFamilyOverride, book.fontSizeOverride, book.bionicReadingOverride, book.paragraphAlignmentOverride,
      book.textAntiAliasingOverride, book.hyphenationOverride, book.fontSizeNormalizationOverride,
      book.guideDotsOverride, book.inlineFootnotePreviewsOverride);
  RECENT_BOOKS.setFormatting(book.path, book.formatting);
}

SettingInfo ReaderFormattingActivity::row(Field field, StrId label, std::vector<int> values,
                                          std::vector<std::string> labels) {
  auto& b = bindings[field];
  b.owner = this;
  b.field = field;
  const bool style = field == Align || field == Line || field == Indent || field == Gap || field == HeadingAlign ||
                     field == HeadingSize || field == Margin;
  if (!global && field != Orientation && field != Darkness && !style) {
    values.insert(values.begin(), -1);
    labels.insert(labels.begin(), tr(STR_FORMAT_INHERIT));
  }
  b.values = std::move(values);
  b.labels = std::move(labels);
  return SettingInfo::DynamicEnumCtx(
             label, {}, &b,
             [](const void* p) -> uint8_t {
               const auto& b = *static_cast<const Binding*>(p);
               const auto v = b.owner->get(b.field);
               auto i = std::find(b.values.begin(), b.values.end(), v);
               return i == b.values.end() ? UINT8_MAX : static_cast<uint8_t>(i - b.values.begin());
             },
             [](void* p, uint8_t i) {
               auto& b = *static_cast<Binding*>(p);
               if (i < b.values.size()) b.owner->set(b.field, b.values[i]);
             })
      .withDynamicOptions([](const void* p) -> uint8_t { return static_cast<const Binding*>(p)->values.size(); },
                          [](const void* p, uint8_t i) -> std::string {
                            const auto& b = *static_cast<const Binding*>(p);
                            return i < b.labels.size() ? b.labels[i] : "";
                          })
      .withDisplayGetter(
          [](void* p) -> std::string {
            const auto& b = *static_cast<const Binding*>(p);
            const auto i = std::find(b.values.begin(), b.values.end(), b.owner->get(b.field));
            return i == b.values.end() ? tr(STR_DEFAULT_VALUE) : b.labels[i - b.values.begin()];
          },
          &b)
      .withSelectorActivity();
}

void ReaderFormattingActivity::buildItems() {
  menuItems.clear();
  submenuData.clear();
  submenusPrepared = false;
  std::vector<int> fonts = {0, 1};
  std::vector<std::string> names = {tr(STR_BOOKERLY), tr(STR_NOTO_SANS)};
  const auto& fs = sdFontSystem.registry().getFamilies();
  for (size_t i = 0; i < fs.size(); ++i) {
    fonts.push_back(2 + i);
    names.push_back(fs[i].name);
  }
  menuItems.push_back(row(Font, StrId::STR_FONT_FAMILY, fonts, names).withSubmenu(StrId::STR_FORMAT_FONT));
  const char* family =
      global ? SETTINGS.sdFontFamilyName : bookSdFontFamily(book.fontFamilyOverride, book.sdFontFamilyOverride);
  const auto sizes = sdFontSystem.sizeListFor(family);
  std::vector<int> points;
  std::vector<std::string> labels;
  for (uint8_t i = 0; i < sizes.count; ++i) {
    points.push_back(sizes.points[i]);
    labels.push_back(fontPointSizeLabel(sizes.points[i]));
  }
  menuItems.push_back(row(Size, StrId::STR_FONT_SIZE, points, labels).withSubmenu(StrId::STR_FORMAT_FONT));
  menuItems.push_back(row(Hyphens, StrId::STR_HYPHENATION, {1, 0}, {tr(STR_STATE_ON), tr(STR_STATE_OFF)}));
  menuItems.push_back(row(Align, StrId::STR_PARA_ALIGNMENT, {0, 1, 2, 3},
                          {tr(STR_JUSTIFY), tr(STR_ALIGN_LEFT), tr(STR_CENTER), tr(STR_ALIGN_RIGHT)})
                          .withSubmenu(StrId::STR_FORMAT_PARAGRAPHS));
  auto numeric = [&](Field f, StrId label, int count, int start, int step, const char* unit, StrId group) {
    std::vector<int> v;
    std::vector<std::string> n;
    for (int i = 0; i < count; ++i) {
      v.push_back(i + 1);
      n.push_back(std::to_string(start + i * step) + unit);
    }
    menuItems.push_back(row(f, label, v, n).withSubmenu(group));
  };
  numeric(Line, StrId::STR_LINE_SPACING, 11, 100, 10, "%", StrId::STR_FORMAT_PARAGRAPHS);
  numeric(Indent, StrId::STR_PARA_INDENT_PERCENT, 11, 0, 25, "%", StrId::STR_FORMAT_PARAGRAPHS);
  numeric(Gap, StrId::STR_PARA_GAP_PERCENT, 7, 0, 25, "%", StrId::STR_FORMAT_PARAGRAPHS);
  menuItems.push_back(row(HeadingAlign, StrId::STR_HEADING_ALIGNMENT, {1, 2, 3, 4},
                          {tr(STR_CENTER), tr(STR_ALIGN_LEFT), tr(STR_ALIGN_RIGHT), tr(STR_JUSTIFY)})
                          .withSubmenu(StrId::STR_FORMAT_HEADINGS));
  numeric(HeadingSize, StrId::STR_HEADING_SIZE, 7, 80, 20, "%", StrId::STR_FORMAT_HEADINGS);
  numeric(Margin, StrId::STR_SCREEN_MARGIN, 9, 0, 5, "px", StrId::STR_FORMAT_PAGE);
  menuItems.push_back(row(Orientation, StrId::STR_FORMAT_GLOBAL_ORIENTATION, {0, 1, 2, 3},
                          {tr(STR_PORTRAIT), tr(STR_LANDSCAPE_CW), tr(STR_INVERTED), tr(STR_LANDSCAPE_CCW)})
                          .withSubmenu(StrId::STR_FORMAT_PAGE));
  for (auto f : {Bionic, AA, Normalize, Dots, Notes}) {
    const StrId ids[] = {StrId::STR_BIONIC_READING, StrId::STR_TEXT_AA, StrId::STR_FONT_SIZE_NORMALIZATION,
                         StrId::STR_GUIDE_DOTS, StrId::STR_INLINE_FOOTNOTE_PREVIEWS};
    const int i = f == Bionic ? 0 : f == AA ? 1 : f == Normalize ? 2 : f == Dots ? 3 : 4;
    menuItems.push_back(
        row(f, ids[i], {0, 1}, {tr(STR_STATE_OFF), tr(STR_STATE_ON)}).withSubmenu(StrId::STR_FORMAT_MORE));
  }
  menuItems.push_back(row(Images, StrId::STR_IMAGES, {0, 1, 2},
                          {tr(STR_IMAGES_DISPLAY), tr(STR_IMAGES_PLACEHOLDER), tr(STR_IMAGES_SUPPRESS)})
                          .withSubmenu(StrId::STR_FORMAT_MORE));
  menuItems.push_back(row(Darkness, StrId::STR_TEXT_DARKNESS, {0, 1, 2, 3, 4},
                          {tr(STR_NORMAL), tr(STR_DARK), tr(STR_EXTRA_DARK), tr(STR_MAX_DARK), tr(STR_LIGHTER)})
                          .withSubmenu(StrId::STR_FORMAT_MORE));
  menuItems.push_back(SettingInfo::Action(StrId::STR_ALL_READER_SETTINGS, SettingAction::None));
  prepareSubmenus();
  submenusPrepared = true;
  initMenuList();
  resetPublishedWindow();
}

void ReaderFormattingActivity::openSubmenu(const SettingInfo& entry) {
  const auto i = std::find_if(submenuData.begin(), submenuData.end(),
                              [&](const SettingInfo::SubmenuData& s) { return s.id == entry.nameId; });
  if (i == submenuData.end()) return;
  startActivityForResult(
      std::make_unique<SettingsSubmenuActivity>(renderer, mappedInput, entry.nameId, i->items,
                                                std::function<std::string(const SettingInfo&)>{}, false),
      [this](const ActivityResult&) {
        buildItems();
        requestUpdate();
      });
}
void ReaderFormattingActivity::onActionSelected(const int index) {
  // The quick-settings list contains one action: "All settings". Route that
  // row to the settings start page (the six-card dashboard), never directly to
  // a category such as Screen. Keeping this explicit also prevents a future
  // action added to this list from inheriting the same destination.
  if (index < 0 || index >= static_cast<int>(menuItems.size()) ||
      menuItems[static_cast<size_t>(index)].nameId != StrId::STR_ALL_READER_SETTINGS) {
    return;
  }

  startActivityForResult(std::make_unique<SettingsDashboardActivity>(renderer, mappedInput, true),
                         [this](const ActivityResult&) {
    buildItems();
    requestUpdate();
  });
}
void ReaderFormattingActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  const Rect r = listContentRect();
  GUI.drawHeader(renderer, listHeaderRect(), tr(STR_FORMAT_QUICK));
  const int top = r.y + m.topPadding + m.headerHeight + m.verticalSpacing;
  drawMenuList(Rect{r.x, top, r.width, r.y + r.height - top - m.verticalSpacing});
  drawListHints();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
