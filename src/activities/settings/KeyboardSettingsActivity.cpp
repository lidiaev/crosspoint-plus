#include "KeyboardSettingsActivity.h"

#include <I18n.h>

#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
struct Row {
  StrId label;
  uint8_t CrossPointSettings::* field;
};
constexpr Row ROWS[] = {
    {StrId::STR_KB_AUTO_CAPS, &CrossPointSettings::keyboardAutoCapitalize},
    {StrId::STR_KB_DOUBLE_SPACE_PERIOD, &CrossPointSettings::keyboardDoubleSpacePeriod},
};
}  // namespace

void KeyboardSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  auto& value = SETTINGS.*ROWS[index].field;
  value = value ? 0 : 1;
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
    rows[i].value = SETTINGS.*ROWS[i].field ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
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
