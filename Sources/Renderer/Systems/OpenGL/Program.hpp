#ifndef RW_ENGINE_RENDERER_SYSTEMS_OPENGL_PROGRAM_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_OPENGL_PROGRAM_HPP

namespace Renderer::Systems::OpenGL {

class Program {
public:
    Program() = default;
    ~Program();

    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    Program(Program&& other) noexcept;
    Program& operator=(Program&& other) noexcept;

    bool createGraphics(const char *vertex_source, const char *fragment_source, const char *label);
    bool createCompute(const char *compute_source, const char *label);
    void destroy();

    void use() const;
    int uniform(const char *name) const;
    unsigned int id() const { return id_; }
    bool valid() const { return id_ != 0u; }

private:
    unsigned int id_ = 0u;
};

void unbindProgram();

} // namespace Renderer::Systems::OpenGL

#endif
