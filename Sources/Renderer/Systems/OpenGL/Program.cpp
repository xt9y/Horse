#include "Renderer/Systems/OpenGL/Program.hpp"

#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

namespace Renderer::Systems::OpenGL {
namespace {

GLuint compile(GLenum stage, const char *source, const char *label)
{
    const GLuint shader = GL20.glCreateShader(stage);
    if (shader == 0u) return 0u;

    GL20.glShaderSource(shader, 1, &source, nullptr);
    GL20.glCompileShader(shader);

    GLint status = 0;
    GL20.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_TRUE) return shader;

    GLint length = 0;
    GL20.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GL20.glGetShaderInfoLog(shader, length, nullptr, log.data());
    std::fprintf(stderr, "[%s]: shader compile failed: %s\n", label ? label : "Renderer", log.data());
    GL20.glDeleteShader(shader);
    return 0u;
}

GLuint link(const GLuint *shaders, int count, const char *label)
{
    const GLuint program = GL20.glCreateProgram();
    if (program == 0u) return 0u;
    for (int index = 0; index < count; ++index) GL20.glAttachShader(program, shaders[index]);
    GL20.glLinkProgram(program);

    GLint status = 0;
    GL20.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        for (int index = 0; index < count; ++index) GL20.glDetachShader(program, shaders[index]);
        return program;
    }

    GLint length = 0;
    GL20.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GL20.glGetProgramInfoLog(program, length, nullptr, log.data());
    std::fprintf(stderr, "[%s]: program link failed: %s\n", label ? label : "Renderer", log.data());
    GL20.glDeleteProgram(program);
    return 0u;
}

} // namespace

Program::~Program()
{
    destroy();
}

Program::Program(Program&& other) noexcept : id_(other.id_)
{
    other.id_ = 0u;
}

Program& Program::operator=(Program&& other) noexcept
{
    if (this == &other) return *this;
    destroy();
    id_ = other.id_;
    other.id_ = 0u;
    return *this;
}

bool Program::createGraphics(const char *vertex_source, const char *fragment_source, const char *label)
{
    destroy();
    const GLuint vertex = compile(GL_VERTEX_SHADER, vertex_source, label);
    if (vertex == 0u) return false;
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragment_source, label);
    if (fragment == 0u) {
        GL20.glDeleteShader(vertex);
        return false;
    }

    const GLuint shaders[] = {vertex, fragment};
    id_ = link(shaders, 2, label);
    GL20.glDeleteShader(vertex);
    GL20.glDeleteShader(fragment);
    return id_ != 0u;
}

bool Program::createCompute(const char *compute_source, const char *label)
{
    destroy();
    const GLuint shader = compile(GL_COMPUTE_SHADER, compute_source, label);
    if (shader == 0u) return false;
    id_ = link(&shader, 1, label);
    GL20.glDeleteShader(shader);
    return id_ != 0u;
}

void Program::destroy()
{
    if (id_ != 0u && GL20.glDeleteProgram) GL20.glDeleteProgram(id_);
    id_ = 0u;
}

void Program::use() const
{
    GL20.glUseProgram(id_);
}

int Program::uniform(const char *name) const
{
    if (id_ == 0u || !GL20.glGetUniformLocation) return -1;
    return GL20.glGetUniformLocation(id_, name);
}

void unbindProgram()
{
    if (GL20.glUseProgram) GL20.glUseProgram(0u);
}

} // namespace Renderer::Systems::OpenGL
