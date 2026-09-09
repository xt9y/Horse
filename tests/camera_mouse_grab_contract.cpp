#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

static std::string read(const char *path)
{
    std::ifstream file(path);
    assert(file.good());
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int main()
{
    const std::string camera = read("Sources/Camera.cpp");

    const std::size_t grabbed = camera.find("Mouse.isGrabbed()");
    const std::size_t mouse_block = camera.find("if (mouse_grabbed)");
    const std::size_t yaw = camera.find("transform->rotation.y -=");
    const std::size_t pitch = camera.find("transform->rotation.x +=");
    const std::size_t movement = camera.find("Keyboard.isKeyDown(Keyboard.KEY_W)");

    assert(grabbed != std::string::npos);
    assert(mouse_block != std::string::npos);
    assert(yaw != std::string::npos);
    assert(pitch != std::string::npos);
    assert(movement != std::string::npos);

    assert(mouse_block < yaw);
    assert(mouse_block < pitch);
    assert(yaw < movement);
    assert(pitch < movement);

    return 0;
}
