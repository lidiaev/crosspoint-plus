#include "KeyboardSettingsActivity.h"

#include <I18n.h>

#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// A switch, or a choice that a tap steps through when it has values.
struct Row {
  StrId label;
  uint8_t CrossPointSettings::* field;
  const StrId* values = nullptr;
  uint8_t count = 2;
};
constexpr StrId REFRESH_VALUES[] = {StrId::STR_KB_REFRESH_FAST, StrId::STR_KB_REFRESH_BALANCED,
                                    StrId::STR_KB_REFRESH_CLEAN};
constexpr StrId DELAY_VALUES[] = {StrId::STR_KB_DELAY_SHORT, StrId::STR_KB_DELAY_MEDIUM, StrId::STR_KB_DELAY_LONG};
constexpr Row ROWS[] = {
    {StrId::STR_KB_AUTO_CAPS, &CrossPointSettings::keyboardAutoCapitalize},
    {StrId::STR_KB_DOUBLE_SPACE_PERIOD, &CrossPointSettings::keyboardDoubleSpacePeriod},
    {StrId::STR_KB_TYPING_REFRESH, &CrossPointSettings::typingRefresh, REFRESH_VALUES, 3},
    {StrId::STR_KB_CLEAN_DELAY, &CrossPointSettings::typingCleanDelay, DELAY_VALUES, 3},
};
}  // namespace

void KeyboardSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  auto& value = SETTINGS.*ROWS[index].field;
  value = static_cast<uint8_t>((value + 1) % ROWS[index].count);
  SETTINGS.saveToFile();
  requestUpdate();
}

void KeyboardSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < ROW_COUNT; ++i) {
    rows[i].label = I18N.get(ROWS[i].label);
    const uint8_t value = SETTINGS.*ROWS[i].field;
    if (ROWS[i].values != nullptr) {
      rows[i].value = I18N.get(ROWS[i].values[value % ROWS[i].count]);
    } else {
      rows[i].value = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props;
  props.items = rows;
  props.count = ROW_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
