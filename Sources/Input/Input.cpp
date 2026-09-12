#include "Input.hpp"

#include "Window/Internal.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cctype>
#include <string>
#include <vector>

namespace Input {
namespace {

struct State {
    std::vector<unsigned char> keys;
    std::vector<unsigned char> previous_keys;
    std::vector<unsigned char> buttons;
    std::vector<unsigned char> previous_buttons;
    Pointer pointer{};
};

State& state()
{
    static State value;
    return value;
}

bool valueAt(const std::vector<unsigned char>& values, int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < values.size() && values[static_cast<std::size_t>(index)] != 0u;
}

void resizeStates(State& input)
{
    const std::size_t keys = static_cast<std::size_t>(SDL_SCANCODE_COUNT);
    constexpr std::size_t buttons = 6u;
    if (input.keys.size() != keys) {
        input.keys.assign(keys, 0u);
        input.previous_keys.assign(keys, 0u);
    }
    if (input.buttons.size() != buttons) {
        input.buttons.assign(buttons, 0u);
        input.previous_buttons.assign(buttons, 0u);
    }
}

std::string lower(const char *name)
{
    std::string value = name ? name : "";
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

void poll()
{
    State& input = state();
    resizeStates(input);

    input.previous_keys = input.keys;
    input.previous_buttons = input.buttons;

    SDL_PumpEvents();
    int key_count = 0;
    const bool *keyboard = SDL_GetKeyboardState(&key_count);
    const std::size_t available = static_cast<std::size_t>(std::max(key_count, 0));
    for (std::size_t index = 0u; index < input.keys.size(); ++index)
        input.keys[index] = keyboard && index < available && keyboard[index] ? 1u : 0u;

    float x = 0.0f;
    float y = 0.0f;
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(&x, &y);
    for (std::size_t index = 1u; index < input.buttons.size(); ++index)
        input.buttons[index] = (mouse & SDL_BUTTON_MASK(index)) != 0u ? 1u : 0u;

    float dx = 0.0f;
    float dy = 0.0f;
    SDL_GetRelativeMouseState(&dx, &dy);
    input.pointer.x = static_cast<int>(x);
    input.pointer.y = static_cast<int>(y);
    input.pointer.dx = static_cast<int>(dx);
    input.pointer.dy = static_cast<int>(dy);
    input.pointer.wheel = 0;
    for (const SDL_Event& event : Window::Internal::events()) {
        if (event.type == SDL_EVENT_MOUSE_WHEEL)
            input.pointer.wheel += event.wheel.integer_y;
    }
    SDL_Window *window = Window::Internal::window();
    input.pointer.captured = window && SDL_GetWindowRelativeMouseMode(window);
}

void reset()
{
    State& input = state();
    input.keys.clear();
    input.previous_keys.clear();
    input.buttons.clear();
    input.previous_buttons.clear();
    input.pointer = {};
}

Key key(const char *name)
{
    if (!name) return InvalidKey;
    const SDL_Scancode value = SDL_GetScancodeFromName(name);
    return value == SDL_SCANCODE_UNKNOWN ? InvalidKey : static_cast<Key>(value);
}

Button button(const char *name)
{
    if (!name) return InvalidButton;
    const std::string value = lower(name);
    if (value == "left" || value == "button0" || value == "0") return SDL_BUTTON_LEFT;
    if (value == "middle" || value == "button2" || value == "2") return SDL_BUTTON_MIDDLE;
    if (value == "right" || value == "button1" || value == "1") return SDL_BUTTON_RIGHT;
    if (value == "x1" || value == "button3" || value == "3") return SDL_BUTTON_X1;
    if (value == "x2" || value == "button4" || value == "4") return SDL_BUTTON_X2;
    return InvalidButton;
}

bool keyDown(Key value)
{
    return valueAt(state().keys, value);
}

bool keyPressed(Key value)
{
    const State& input = state();
    return valueAt(input.keys, value) && !valueAt(input.previous_keys, value);
}

bool keyReleased(Key value)
{
    const State& input = state();
    return !valueAt(input.keys, value) && valueAt(input.previous_keys, value);
}

bool buttonDown(Button value)
{
    return valueAt(state().buttons, value);
}

bool buttonPressed(Button value)
{
    const State& input = state();
    return valueAt(input.buttons, value) && !valueAt(input.previous_buttons, value);
}

bool buttonReleased(Button value)
{
    const State& input = state();
    return !valueAt(input.buttons, value) && valueAt(input.previous_buttons, value);
}

Pointer pointer()
{
    return state().pointer;
}

void setPointerCaptured(bool captured)
{
    SDL_Window *window = Window::Internal::window();
    if (!window) return;
    if (SDL_SetWindowRelativeMouseMode(window, captured))
        state().pointer.captured = captured;
}

} // namespace Input
