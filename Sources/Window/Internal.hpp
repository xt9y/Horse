#ifndef HORSE_WINDOW_INTERNAL_HPP
#define HORSE_WINDOW_INTERNAL_HPP

#include <SDL3/SDL.h>

#include <vector>

namespace Window::Internal {

SDL_Window *window();
const std::vector<SDL_Event>& events();

} // namespace Window::Internal

#endif
