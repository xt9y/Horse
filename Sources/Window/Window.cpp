#include "Window.hpp"

#include "Window/Internal.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace Window {
namespace {

struct State {
    SDL_Window *window = nullptr;
    std::vector<SDL_Event> events;
    bool owns_sdl = false;
    bool close_requested = false;
};

State& state()
{
    static State value;
    return value;
}

} // namespace

bool create(const Settings& settings)
{
    State& value = state();
    if (value.window) return true;

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
            std::fprintf(stderr, "[Window]: SDL initialization failed: %s\n", SDL_GetError());
            return false;
        }
        value.owns_sdl = true;
    }

    SDL_WindowFlags flags = 0;
    if (settings.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (settings.high_pixel_density) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

    value.window = SDL_CreateWindow(
        settings.title ? settings.title : "Horse",
        std::max(settings.width, 1),
        std::max(settings.height, 1),
        flags
    );
    if (!value.window) {
        std::fprintf(stderr, "[Window]: creation failed: %s\n", SDL_GetError());
        if (value.owns_sdl) SDL_Quit();
        value.owns_sdl = false;
        return false;
    }

    value.close_requested = false;
    value.events.clear();
    return true;
}

void destroy()
{
    State& value = state();
    value.events.clear();
    if (value.window) SDL_DestroyWindow(value.window);
    value.window = nullptr;
    value.close_requested = false;
    if (value.owns_sdl) SDL_Quit();
    value.owns_sdl = false;
}

bool poll()
{
    State& value = state();
    value.events.clear();
    if (!value.window) return false;

    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        value.events.push_back(event);
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(value.window)))
        {
            value.close_requested = true;
        }
    }
    return !value.close_requested;
}

bool created()
{
    return state().window != nullptr;
}

bool closeRequested()
{
    return state().close_requested;
}

int width()
{
    int width = 0;
    int height = 0;
    if (state().window) SDL_GetWindowSizeInPixels(state().window, &width, &height);
    return std::max(width, 1);
}

int height()
{
    int width = 0;
    int height = 0;
    if (state().window) SDL_GetWindowSizeInPixels(state().window, &width, &height);
    return std::max(height, 1);
}

void setTitle(const char *title)
{
    if (state().window && title) SDL_SetWindowTitle(state().window, title);
}

void *native()
{
    return state().window;
}

namespace Internal {

SDL_Window *window()
{
    return state().window;
}

const std::vector<SDL_Event>& events()
{
    return state().events;
}

} // namespace Internal
} // namespace Window
