#pragma once

#include <I18n.h>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"

// Settings > System > Keyboard: on/off switches for the notes keyboard.
class KeyboardSettingsActivity final : public UiListActivity {
 public:
  KeyboardSettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("KeyboardSettings", renderer, input) {}

 private:
  static constexpr int ROW_COUNT = 2;

  freeink::ui::ListItem rows[ROW_COUNT]{};

  int listCount() const override { return ROW_COUNT; }
  const char* headerTitle() const override { return tr(STR_KEYBOARD); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
};
