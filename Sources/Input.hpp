#ifndef HORSE_INPUT_HPP
#define HORSE_INPUT_HPP

namespace Input {

using Key = int;
using Button = int;
inline constexpr Key InvalidKey = -1;
inline constexpr Button InvalidButton = -1;

struct Pointer {
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;
    int wheel = 0;
    bool captured = false;
};

void poll();
void reset();

Key key(const char *name);
Button button(const char *name);

bool keyDown(Key value);
bool keyPressed(Key value);
bool keyReleased(Key value);
bool buttonDown(Button value);
bool buttonPressed(Button value);
bool buttonReleased(Button value);

Pointer pointer();
void setPointerCaptured(bool captured);

} // namespace Input

#endif
