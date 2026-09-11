#include "Input.hpp"

#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cstddef>
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
    const int key_count = Keyboard.isCreated() != LWCGL_FALSE ? std::max(Keyboard.getKeyCount(), 0) : 0;
    const int button_count = Mouse.isCreated() != LWCGL_FALSE ? std::max(Mouse.getButtonCount(), 0) : 0;

    const std::size_t keys = static_cast<std::size_t>(key_count);
    const std::size_t buttons = static_cast<std::size_t>(button_count);
    if (input.keys.size() != keys) {
        input.keys.assign(keys, 0u);
        input.previous_keys.assign(keys, 0u);
    }
    if (input.buttons.size() != buttons) {
        input.buttons.assign(buttons, 0u);
        input.previous_buttons.assign(buttons, 0u);
    }
}

} // namespace

void poll()
{
    State& input = state();
    resizeStates(input);

    input.previous_keys = input.keys;
    input.previous_buttons = input.buttons;

    if (Keyboard.isCreated() != LWCGL_FALSE) {
        Keyboard.poll();
        for (std::size_t index = 0u; index < input.keys.size(); ++index)
            input.keys[index] = Keyboard.isKeyDown(static_cast<int>(index)) != LWCGL_FALSE ? 1u : 0u;
    }

    if (Mouse.isCreated() != LWCGL_FALSE) {
        Mouse.poll();
        for (std::size_t index = 0u; index < input.buttons.size(); ++index)
            input.buttons[index] = Mouse.isButtonDown(static_cast<int>(index)) != LWCGL_FALSE ? 1u : 0u;

        input.pointer.x = Mouse.getX();
        input.pointer.y = Mouse.getY();
        input.pointer.dx = Mouse.getDX();
        input.pointer.dy = Mouse.getDY();
        input.pointer.wheel = Mouse.getDWheel();
        input.pointer.captured = Mouse.isGrabbed() != LWCGL_FALSE;
    } else {
        input.pointer = {};
    }
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
    return name ? Keyboard.getKeyIndex(name) : InvalidKey;
}

Button button(const char *name)
{
    return name ? Mouse.getButtonIndex(name) : InvalidButton;
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
    if (Mouse.isCreated() == LWCGL_FALSE) return;
    Mouse.setGrabbed(captured ? LWCGL_TRUE : LWCGL_FALSE);
    state().pointer.captured = captured;
}

} // namespace Input
