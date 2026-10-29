#pragma once

#include <I18n.h>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"

// Settings > System > Keyboard: typing helpers and how the screen refreshes while typing.
class KeyboardSettingsActivity final : public UiListActivity {
 public:
  KeyboardSettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("KeyboardSettings", renderer, input) {}

 private:
  static constexpr int ROW_COUNT = 4;

  freeink::ui::ListItem rows[ROW_COUNT]{};

  int listCount() const override { return ROW_COUNT; }
  const char* headerTitle() const override { return tr(STR_KEYBOARD); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
};
