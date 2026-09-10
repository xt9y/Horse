#ifndef HORSE_UI_UI_HPP
#define HORSE_UI_UI_HPP

#include "UI/EnginePanels.hpp"
#include "UI/RendererSelector.hpp"

namespace UI {

bool init();
void shutdown();
bool beginFrame();
bool initialized();
bool wantsMouse();
bool wantsKeyboard();

} // namespace UI

#endif
