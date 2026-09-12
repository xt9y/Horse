#ifndef HORSE_WINDOW_HPP
#define HORSE_WINDOW_HPP

#include <cstdint>

namespace Window {

struct Settings {
    const char *title = "Horse";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool high_pixel_density = true;
};

bool create(const Settings& settings = {});
void destroy();
bool poll();

bool created();
bool closeRequested();
int width();
int height();
void setTitle(const char *title);
void *native();

} // namespace Window

#endif
