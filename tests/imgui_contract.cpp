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
    const std::string ui = read("Sources/UI/UI.hpp");
    const std::string ui_source = read("Sources/UI/UI.cpp");
    const std::string selector = read("Sources/UI/RendererSelector.hpp");
    const std::string selector_source = read("Sources/UI/RendererSelector.cpp");
    const std::string gl = read("Sources/UI/OpenGL/UIOpenGL.cpp");
    const std::string metal = read("Sources/UI/Metal/UIMetal.mm");
    const std::string renderer = read("Sources/Renderer/Renderer.cpp");
    const std::string build = read("build.c");

    assert(ui.find("bool init()") != std::string::npos);
    assert(ui.find("void beginFrame()") != std::string::npos);
    assert(ui.find("void shutdown()") != std::string::npos);
    assert(ui.find("bool wantsMouse()") != std::string::npos);
    assert(ui.find("bool wantsKeyboard()") != std::string::npos);

    assert(selector.find("enum class RendererChoice") != std::string::npos);
    assert(selector.find("struct RendererAvailability") != std::string::npos);
    assert(selector.find("rendererSelector") != std::string::npos);
    assert(selector_source.find("ImGui::RadioButton") != std::string::npos);

    assert(ui_source.find("ImGui_ImplGlfw_InitForOther") != std::string::npos);
    assert(ui_source.find("ImGui_ImplGlfw_NewFrame") != std::string::npos);
    assert(ui_source.find("ImGui::NewFrame") != std::string::npos);
    assert(renderer.find("UI::Internal::render") != std::string::npos);

    assert(gl.find("ImGui_ImplOpenGL") != std::string::npos);
    assert(metal.find("ImGui_ImplMetal") != std::string::npos);
    assert(metal.find("nativeCommandBuffer") != std::string::npos);
    assert(metal.find("nativeRenderEncoder") != std::string::npos);
    assert(metal.find("nativeRenderPassDescriptor") != std::string::npos);

    assert(build.find("https://github.com/ocornut/imgui.git") != std::string::npos);
    assert(build.find("v1.92.9b") != std::string::npos);
    assert(build.find("c_dep_source(imgui)") != std::string::npos);
    assert(build.find("imgui.cpp") != std::string::npos);
    assert(build.find("imgui_draw.cpp") != std::string::npos);
    assert(build.find("imgui_tables.cpp") != std::string::npos);
    assert(build.find("imgui_widgets.cpp") != std::string::npos);
    assert(build.find("imgui_impl_glfw.cpp") != std::string::npos);
    assert(build.find("imgui_impl_metal.mm") != std::string::npos);
    assert(build.find("Sources/UI/OpenGL/UIOpenGL.cpp") != std::string::npos);
    assert(build.find("Sources/UI/Metal/UIMetal.mm") != std::string::npos);
    return 0;
}
