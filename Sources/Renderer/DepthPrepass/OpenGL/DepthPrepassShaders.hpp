#ifndef HORSE_RENDERER_DEPTH_PREPASS_OPENGL_SHADERS_HPP
#define HORSE_RENDERER_DEPTH_PREPASS_OPENGL_SHADERS_HPP

namespace Renderer::DepthPrepass::OpenGLShaders {

inline constexpr const char *vertex = R"GLSL(
#version 120

varying vec2 vUv;

void main()
{
    vUv = gl_MultiTexCoord0.xy;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)GLSL";

inline constexpr const char *fragment = R"GLSL(
#version 120

uniform sampler2D uDiffuse;
uniform int uHasTexture;
uniform float uBaseAlpha;
uniform float uAlphaCutoff;

varying vec2 vUv;

void main()
{
    float texture_alpha = uHasTexture != 0 ? texture2D(uDiffuse, vUv).a : 1.0;
    if (clamp(uBaseAlpha * texture_alpha, 0.0, 1.0) < uAlphaCutoff) discard;
    gl_FragColor = vec4(0.0);
}
)GLSL";

} // namespace Renderer::DepthPrepass::OpenGLShaders

#endif
