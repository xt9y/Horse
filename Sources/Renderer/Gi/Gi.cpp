#include "Renderer/Gi/Gi.hpp"

#include "Models/Core/Texture.hpp"
#include "Models/Models.hpp"
#include "Renderer/Components.hpp"

#include <lwcgl/gl11_compat.h>
#include <lwcgl/glmodern.h>
#include <lwcgl/lwcgl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 0x8CE1
#endif
#ifndef GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT2 0x8CE2
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F 0x8CAC
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace Renderer {
namespace {

using Mat4 = std::array<float, 16>;

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kLeafBit = 0x80000000u;
constexpr std::uint32_t kLeafSize = 8u;
constexpr int kShadowSize = 2048;
constexpr std::size_t kMaximumTriangles = 600000u;

Vec3f add(const Vec3f& a, const Vec3f& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3f subtract(const Vec3f& a, const Vec3f& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f scale(const Vec3f& value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

float dot(const Vec3f& a, const Vec3f& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f cross(const Vec3f& a, const Vec3f& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3f normalize(const Vec3f& value)
{
    const float length_squared = dot(value, value);
    if (length_squared <= 1.0e-20f) return {0.0f, 1.0f, 0.0f};
    return scale(value, 1.0f / std::sqrt(length_squared));
}

Vec3f minVec(const Vec3f& a, const Vec3f& b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

Vec3f maxVec(const Vec3f& a, const Vec3f& b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

float component(const Vec3f& value, int axis)
{
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

Mat4 identityMatrix()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
            }
        }
    }
    return result;
}

Mat4 translation(float x, float y, float z)
{
    Mat4 result = identityMatrix();
    result[12] = x;
    result[13] = y;
    result[14] = z;
    return result;
}

Mat4 scaling(float x, float y, float z)
{
    Mat4 result{};
    result[0] = x;
    result[5] = y;
    result[10] = z;
    result[15] = 1.0f;
    return result;
}

Mat4 rotationX(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[5] = c;
    result[6] = s;
    result[9] = -s;
    result[10] = c;
    return result;
}

Mat4 rotationY(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[2] = -s;
    result[8] = s;
    result[10] = c;
    return result;
}

Mat4 rotationZ(float degrees)
{
    const float radians = degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 result = identityMatrix();
    result[0] = c;
    result[1] = s;
    result[4] = -s;
    result[5] = c;
    return result;
}

Mat4 modelMatrix(const Transform& transform)
{
    return multiply(
        multiply(
            multiply(
                multiply(
                    translation(transform.position.x, transform.position.y, transform.position.z),
                    rotationX(transform.rotation.x)
                ),
                rotationY(transform.rotation.y)
            ),
            rotationZ(transform.rotation.z)
        ),
        scaling(transform.scale.x, transform.scale.y, transform.scale.z)
    );
}

Mat4 inverseModelMatrix(const Transform& transform)
{
    const float sx = std::abs(transform.scale.x) > 1.0e-8f ? 1.0f / transform.scale.x : 0.0f;
    const float sy = std::abs(transform.scale.y) > 1.0e-8f ? 1.0f / transform.scale.y : 0.0f;
    const float sz = std::abs(transform.scale.z) > 1.0e-8f ? 1.0f / transform.scale.z : 0.0f;

    return multiply(
        multiply(
            multiply(
                multiply(
                    scaling(sx, sy, sz),
                    rotationZ(-transform.rotation.z)
                ),
                rotationY(-transform.rotation.y)
            ),
            rotationX(-transform.rotation.x)
        ),
        translation(-transform.position.x, -transform.position.y, -transform.position.z)
    );
}

Vec3f transformPoint(const Mat4& matrix, const Models::Vec3& value)
{
    return {
        matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12],
        matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13],
        matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14],
    };
}

Vec3f transformPoint(const Mat4& matrix, const Vec3f& value)
{
    return {
        matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z + matrix[12],
        matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z + matrix[13],
        matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z + matrix[14],
    };
}

Vec3f transformNormal(const Mat4& inverse_model, const Models::Vec3& value)
{
    return normalize({
        inverse_model[0] * value.x + inverse_model[1] * value.y + inverse_model[2] * value.z,
        inverse_model[4] * value.x + inverse_model[5] * value.y + inverse_model[6] * value.z,
        inverse_model[8] * value.x + inverse_model[9] * value.y + inverse_model[10] * value.z,
    });
}

Mat4 lookAt(const Vec3f& eye, const Vec3f& center, const Vec3f& up_hint)
{
    const Vec3f forward = normalize(subtract(center, eye));
    const Vec3f side = normalize(cross(forward, up_hint));
    const Vec3f up = cross(side, forward);

    return {
        side.x, up.x, -forward.x, 0.0f,
        side.y, up.y, -forward.y, 0.0f,
        side.z, up.z, -forward.z, 0.0f,
        -dot(side, eye), -dot(up, eye), dot(forward, eye), 1.0f,
    };
}

Mat4 orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane)
{
    Mat4 result = identityMatrix();
    result[0] = 2.0f / (right - left);
    result[5] = 2.0f / (top - bottom);
    result[10] = -2.0f / (far_plane - near_plane);
    result[12] = -(right + left) / (right - left);
    result[13] = -(top + bottom) / (top - bottom);
    result[14] = -(far_plane + near_plane) / (far_plane - near_plane);
    return result;
}

bool inverseMatrix(const Mat4& input, Mat4& output)
{
    double augmented[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            augmented[row][column] = static_cast<double>(input[column * 4 + row]);
        }
        augmented[row][row + 4] = 1.0;
    }

    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row) {
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
        }
        if (std::abs(augmented[pivot][column]) < 1.0e-12) return false;
        if (pivot != column) {
            for (int k = 0; k < 8; ++k) std::swap(augmented[pivot][k], augmented[column][k]);
        }

        const double divisor = augmented[column][column];
        for (int k = 0; k < 8; ++k) augmented[column][k] /= divisor;
        for (int row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = augmented[row][column];
            for (int k = 0; k < 8; ++k) augmented[row][k] -= factor * augmented[column][k];
        }
    }

    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            output[column * 4 + row] = static_cast<float>(augmented[row][column + 4]);
        }
    }
    return true;
}

GLuint createTexture2D(int width, int height, GLint internal_format, GLenum format, GLenum type, bool linear)
{
    const GLuint texture = lwcgl_glGenTexture();
    if (texture == 0u) return 0u;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, nullptr);
    return texture;
}

void deleteTexture(GLuint& texture)
{
    if (texture != 0u) lwcgl_glDeleteTexture(texture);
    texture = 0u;
}

void bindTextureUnit(int unit, GLuint texture)
{
    GLModern.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, texture);
}

GLuint compileShader(GLenum stage, const char *source)
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
    std::fprintf(stderr, "[GI]: shader compile failed: %s\n", log.data());
    GL20.glDeleteShader(shader);
    return 0u;
}

GLuint linkProgram(std::initializer_list<GLuint> shaders)
{
    const GLuint program = GL20.glCreateProgram();
    if (program == 0u) return 0u;
    for (GLuint shader : shaders) GL20.glAttachShader(program, shader);
    GL20.glLinkProgram(program);

    GLint status = 0;
    GL20.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        for (GLuint shader : shaders) GL20.glDetachShader(program, shader);
        return program;
    }

    GLint length = 0;
    GL20.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<char> log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    GL20.glGetProgramInfoLog(program, length, nullptr, log.data());
    std::fprintf(stderr, "[GI]: program link failed: %s\n", log.data());
    GL20.glDeleteProgram(program);
    return 0u;
}

GLuint createGraphicsProgram(const char *vertex_source, const char *fragment_source)
{
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertex_source);
    if (vertex == 0u) return 0u;
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment == 0u) {
        GL20.glDeleteShader(vertex);
        return 0u;
    }
    const GLuint program = linkProgram({vertex, fragment});
    GL20.glDeleteShader(vertex);
    GL20.glDeleteShader(fragment);
    return program;
}

GLuint createComputeProgram(const char *source)
{
    const GLuint shader = compileShader(GL_COMPUTE_SHADER, source);
    if (shader == 0u) return 0u;
    const GLuint program = linkProgram({shader});
    GL20.glDeleteShader(shader);
    return program;
}

void setInt(GLuint program, const char *name, int value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform1i(location, value);
}

void setFloat(GLuint program, const char *name, float value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform1f(location, value);
}

void setSize(GLuint program, const char *name, int width, int height)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform2f(location, static_cast<float>(width), static_cast<float>(height));
}

void setVec3(GLuint program, const char *name, const Vec3f& value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniform3f(location, value.x, value.y, value.z);
}

void setMatrix(GLuint program, const char *name, const Mat4& value)
{
    const GLint location = GL20.glGetUniformLocation(program, name);
    if (location >= 0) GL20.glUniformMatrix4fv(location, 1, GL_FALSE, value.data());
}

const char *kGBufferVertexShader = R"GLSL(
#version 430 compatibility
uniform mat4 uInverseView;
uniform mat4 uPreviousViewProjection;
out vec3 vWorldNormal;
out vec2 vUv;
out vec4 vColor;
out vec4 vCurrentClip;
out vec4 vPreviousClip;
void main() {
    vec4 viewPosition = gl_ModelViewMatrix * gl_Vertex;
    vec4 worldPosition = uInverseView * viewPosition;
    vWorldNormal = normalize(mat3(uInverseView) * normalize(gl_NormalMatrix * gl_Normal));
    vUv = gl_MultiTexCoord0.xy;
    vColor = gl_Color;
    vCurrentClip = gl_ModelViewProjectionMatrix * gl_Vertex;
    vPreviousClip = uPreviousViewProjection * worldPosition;
    gl_Position = vCurrentClip;
}
)GLSL";

const char *kGBufferFragmentShader = R"GLSL(
#version 430 compatibility
uniform sampler2D uDiffuse;
uniform int uHasTexture;
in vec3 vWorldNormal;
in vec2 vUv;
in vec4 vColor;
in vec4 vCurrentClip;
in vec4 vPreviousClip;
layout(location=0) out vec4 outAlbedo;
layout(location=1) out vec4 outNormal;
layout(location=2) out vec2 outVelocity;
void main() {
    vec4 base = vColor;
    if (uHasTexture != 0) base *= texture(uDiffuse, vUv);
    if (base.a < 0.5) discard;
    vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 1e-6);
    vec2 previousNdc = vPreviousClip.xy / max(abs(vPreviousClip.w), 1e-6);
    outAlbedo = base;
    outNormal = vec4(normalize(vWorldNormal) * 0.5 + 0.5, 1.0);
    outVelocity = (currentNdc - previousNdc) * 0.5;
}
)GLSL";

const char *kShadowVertexShader = R"GLSL(
#version 430 compatibility
uniform mat4 uLightViewProjection;
uniform mat4 uModel;
void main() { gl_Position = uLightViewProjection * uModel * gl_Vertex; }
)GLSL";

const char *kShadowFragmentShader = R"GLSL(
#version 430 compatibility
void main() {}
)GLSL";

const char *kTraceShader = R"GLSL(
#version 430
layout(local_size_x=8, local_size_y=8) in;
struct BvhNode { vec3 bmin; uint left; vec3 bmax; uint meta; };
struct Triangle { vec4 p0; vec4 p1; vec4 p2; vec4 n0; vec4 n1; vec4 n2; vec4 color; };
layout(std430,binding=0) readonly buffer Nodes { BvhNode nodes[]; };
layout(std430,binding=1) readonly buffer Triangles { Triangle triangles[]; };
uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uDepth;
uniform sampler2D uShadow;
uniform mat4 uViewProjection;
uniform mat4 uInverseViewProjection;
uniform mat4 uLightViewProjection;
uniform vec3 uLightDirection;
uniform vec2 uOutputSize;
uniform int uFrame;
uniform int uNodeCount;
uniform int uUseScreen;
uniform int uUseBvh;
uniform int uRaysPerPixel;
layout(rgba16f,binding=0) writeonly uniform image2D uRaw;
const uint LEAF_BIT=0x80000000u;
const vec2 POISSON[12]=vec2[12](
    vec2(-0.326,-0.406),vec2(-0.840,-0.074),vec2(-0.696,0.457),vec2(-0.203,0.621),
    vec2(0.962,-0.195),vec2(0.473,-0.480),vec2(0.519,0.767),vec2(0.185,-0.893),
    vec2(0.507,0.064),vec2(0.896,0.412),vec2(-0.322,-0.933),vec2(-0.792,-0.598)
);
uint hashUint(uint v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v;}
float randomFloat(inout uint s){s=hashUint(s);return float(s)*(1.0/4294967296.0);}
vec3 cosineHemisphere(vec3 n,inout uint s){
    float r1=randomFloat(s),r2=randomFloat(s),phi=6.28318530718*r1,r=sqrt(r2);
    vec3 l=vec3(r*cos(phi),r*sin(phi),sqrt(max(0.0,1.0-r2)));
    vec3 h=abs(n.z)<0.999?vec3(0,0,1):vec3(1,0,0);
    vec3 t=normalize(cross(h,n)),b=cross(n,t);
    return normalize(t*l.x+b*l.y+n*l.z);
}
vec3 inverseDirection(vec3 d){return vec3(abs(d.x)>1e-8?1.0/d.x:1e30,abs(d.y)>1e-8?1.0/d.y:1e30,abs(d.z)>1e-8?1.0/d.z:1e30);}
bool hitAabb(vec3 o,vec3 id,vec3 mn,vec3 mx,float maxD){vec3 t0=(mn-o)*id,t1=(mx-o)*id,n=min(t0,t1),f=max(t0,t1);float a=max(max(n.x,n.y),max(n.z,0.0));float z=min(min(f.x,f.y),f.z);return z>=a&&a<maxD;}
bool hitTriangle(vec3 o,vec3 d,Triangle tri,inout float dist,out vec3 n,out vec3 color){
    vec3 e1=tri.p1.xyz-tri.p0.xyz,e2=tri.p2.xyz-tri.p0.xyz,p=cross(d,e2);float det=dot(e1,p);if(abs(det)<1e-8)return false;
    float inv=1.0/det;vec3 s=o-tri.p0.xyz;float u=dot(s,p)*inv;if(u<0.0||u>1.0)return false;vec3 q=cross(s,e1);float v=dot(d,q)*inv;if(v<0.0||u+v>1.0)return false;float t=dot(e2,q)*inv;if(t<=0.01||t>=dist)return false;
    dist=t;float w=1.0-u-v;n=normalize(tri.n0.xyz*w+tri.n1.xyz*u+tri.n2.xyz*v);if(dot(n,d)>0.0)n=-n;color=tri.color.rgb;return true;
}
bool traceScene(vec3 o,vec3 d,out float dist,out vec3 n,out vec3 color){
    dist=1e20;n=vec3(0,1,0);color=vec3(1);if(uUseBvh==0||uNodeCount<=0)return false;vec3 id=inverseDirection(d);uint stack[48];int sp=0,steps=0;stack[sp++]=0u;bool found=false;
    while(sp>0&&steps++<2048){uint ni=stack[--sp];if(ni>=uint(uNodeCount))continue;BvhNode node=nodes[ni];if(!hitAabb(o,id,node.bmin,node.bmax,dist))continue;
        if((node.meta&LEAF_BIT)!=0u){uint count=node.meta&~LEAF_BIT;for(uint i=0u;i<count;++i){vec3 cn,cc;if(hitTriangle(o,d,triangles[node.left+i],dist,cn,cc)){n=cn;color=cc;found=true;}}}
        else if(sp<=45){stack[sp++]=node.meta;stack[sp++]=node.left;}
    }return found;
}
vec3 reconstructWorld(vec2 uv,float depth){vec4 c=vec4(uv*2.0-1.0,depth*2.0-1.0,1);vec4 w=uInverseViewProjection*c;return w.xyz/max(abs(w.w),1e-8);}
float shadowVisibility(vec3 p,vec3 n){
    float ndl=max(dot(n,uLightDirection),0.0);if(ndl<=0.0)return 0.0;
    vec4 c=uLightViewProjection*vec4(p+n*0.025,1.0);if(c.w<=0.0)return 1.0;
    vec3 q=c.xyz/c.w;vec2 uv=q.xy*0.5+0.5;float z=q.z*0.5+0.5;
    if(z<=0.0||z>=1.0||any(lessThan(uv,vec2(0.0)))||any(greaterThan(uv,vec2(1.0))))return 1.0;
    vec2 texel=1.0/vec2(textureSize(uShadow,0));
    float bias=0.00045+(1.0-ndl)*0.0018;
    float blockers=0.0,blockerDepth=0.0;
    float searchRadius=3.5;
    for(int i=0;i<12;++i){float d=texture(uShadow,uv+POISSON[i]*texel*searchRadius).r;if(d<z-bias){blockerDepth+=d;blockers+=1.0;}}
    if(blockers<0.5)return 1.0;
    blockerDepth/=blockers;
    float separation=max(z-blockerDepth,0.0);
    float penumbra=clamp(0.9+separation*420.0,0.9,7.0)*(1.0+(1.0-ndl)*0.25);
    float visibility=0.0;
    for(int i=0;i<12;++i)visibility+=(z-bias<=texture(uShadow,uv+POISSON[i]*texel*penumbra).r)?1.0:0.0;
    return visibility/12.0;
}
bool screenTrace(vec3 o,vec3 d,out vec2 hitUv){
    for(int step=0;step<16;++step){float t=0.18+float(step*step)*0.055;vec4 c=uViewProjection*vec4(o+d*t,1);if(c.w<=0.0)continue;vec3 ndc=c.xyz/c.w;vec2 uv=ndc.xy*0.5+0.5;if(any(lessThan(uv,vec2(0.002)))||any(greaterThan(uv,vec2(0.998))))return false;float scene=texture(uDepth,uv).r,ray=ndc.z*0.5+0.5,diff=ray-scene;if(scene<0.999999&&diff>0.0006&&diff<0.025){hitUv=uv;return true;}}
    return false;
}
vec3 skyRadiance(vec3 d){float e=clamp(d.y*0.5+0.5,0.0,1.0);return mix(vec3(0.035,0.045,0.065),vec3(0.18,0.23,0.32),e);}
vec3 localFill(vec3 n){float up=clamp(n.y*0.5+0.5,0.0,1.0);return mix(vec3(0.012,0.015,0.022),vec3(0.040,0.052,0.070),up);}
void main(){
    ivec2 size=ivec2(uOutputSize),pixel=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(pixel,size)))return;vec2 uv=(vec2(pixel)+0.5)/uOutputSize;float depth=texture(uDepth,uv).r;if(depth>=0.999999){imageStore(uRaw,pixel,vec4(0));return;}
    vec3 p=reconstructWorld(uv,depth),n=normalize(texture(uNormal,uv).xyz*2.0-1.0),indirect=vec3(0);int rays=clamp(uRaysPerPixel,1,2);
    for(int s=0;s<rays;++s){uint state=uint(pixel.x)*1973u+uint(pixel.y)*9277u+uint(max(uFrame,0))*26699u+uint(s)*31847u+1u;vec3 ro=p+n*0.04,rd=cosineHemisphere(n,state),radiance=vec3(0);vec2 hitUv;
        if(uUseScreen!=0&&screenTrace(ro,rd,hitUv)){float hd=texture(uDepth,hitUv).r;vec3 hp=reconstructWorld(hitUv,hd),hn=normalize(texture(uNormal,hitUv).xyz*2.0-1.0),hc=texture(uAlbedo,hitUv).rgb;float direct=max(dot(hn,uLightDirection),0.0)*shadowVisibility(hp,hn);radiance=hc*(direct*1.15+localFill(hn));}
        else{float dist;vec3 hn,hc;if(traceScene(ro,rd,dist,hn,hc)){vec3 hp=ro+rd*dist;float direct=max(dot(hn,uLightDirection),0.0)*shadowVisibility(hp,hn);radiance=hc*(direct*1.15+localFill(hn));}else radiance=skyRadiance(rd);}
        indirect+=radiance;
    }
    imageStore(uRaw,pixel,vec4(max(indirect/float(rays),vec3(0)),1));
}
)GLSL";

const char *kTemporalShader = R"GLSL(
#version 430
layout(local_size_x=8,local_size_y=8) in;
uniform sampler2D uRaw;
uniform sampler2D uPrevious;
uniform sampler2D uPreviousGeometry;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform sampler2D uVelocity;
uniform vec2 uOutputSize;
uniform float uAlpha;
uniform float uDepthReject;
uniform float uNormalReject;
uniform int uHasHistory;
layout(rgba16f,binding=0) writeonly uniform image2D uHistory;
layout(rgba16f,binding=1) writeonly uniform image2D uGeometry;
void main(){
    ivec2 size=ivec2(uOutputSize),p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,size)))return;vec2 uv=(vec2(p)+0.5)/uOutputSize;vec3 current=texture(uRaw,uv).rgb;float depth=texture(uDepth,uv).r;vec3 normal=normalize(texture(uNormal,uv).xyz*2.0-1.0);vec2 prevUv=uv-texture(uVelocity,uv).xy;
    bool valid=uHasHistory!=0&&all(greaterThanEqual(prevUv,vec2(0)))&&all(lessThanEqual(prevUv,vec2(1)));if(valid){vec4 g=texture(uPreviousGeometry,prevUv);valid=abs(g.w-depth)<=uDepthReject&&dot(normalize(g.xyz),normal)>=uNormalReject;}
    vec3 result=valid?mix(texture(uPrevious,prevUv).rgb,current,clamp(uAlpha,0.04,1.0)):current;imageStore(uHistory,p,vec4(result,1));imageStore(uGeometry,p,vec4(normal,depth));
}
)GLSL";

const char *kDenoiseShader = R"GLSL(
#version 430
layout(local_size_x=8,local_size_y=8) in;
uniform sampler2D uInput;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform vec2 uOutputSize;
uniform int uStep;
layout(rgba16f,binding=0) writeonly uniform image2D uOutput;
void main(){
    ivec2 size=ivec2(uOutputSize),p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,size)))return;vec2 uv=(vec2(p)+0.5)/uOutputSize;vec3 center=texture(uInput,uv).rgb;float cd=texture(uDepth,uv).r;vec3 cn=normalize(texture(uNormal,uv).xyz*2.0-1.0);vec3 sum=vec3(0);float ws=0.0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 q=clamp(p+ivec2(x,y)*uStep,ivec2(0),size-ivec2(1));vec2 suv=(vec2(q)+0.5)/uOutputSize;vec3 c=texture(uInput,suv).rgb;float d=texture(uDepth,suv).r;vec3 n=normalize(texture(uNormal,suv).xyz*2.0-1.0);float w=pow(max(dot(cn,n),0.0),24.0)*exp(-abs(d-cd)*120.0/float(max(uStep,1)))*exp(-length(c-center)*2.0);sum+=c*w;ws+=w;}
    imageStore(uOutput,p,vec4(sum/max(ws,1e-6),1));
}
)GLSL";

const char *kComposeVertexShader = R"GLSL(
#version 430 compatibility
out vec2 vUv;
void main(){gl_Position=vec4(gl_Vertex.xy,0,1);vUv=gl_Vertex.xy*0.5+0.5;}
)GLSL";

const char *kComposeFragmentShader = R"GLSL(
#version 430 compatibility
uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uDepth;
uniform sampler2D uIndirect;
uniform sampler2D uShadow;
uniform mat4 uInverseViewProjection;
uniform mat4 uLightViewProjection;
uniform vec3 uLightDirection;
in vec2 vUv;
layout(location=0) out vec4 outColor;
const vec2 POISSON[12]=vec2[12](
    vec2(-0.326,-0.406),vec2(-0.840,-0.074),vec2(-0.696,0.457),vec2(-0.203,0.621),
    vec2(0.962,-0.195),vec2(0.473,-0.480),vec2(0.519,0.767),vec2(0.185,-0.893),
    vec2(0.507,0.064),vec2(0.896,0.412),vec2(-0.322,-0.933),vec2(-0.792,-0.598)
);
vec3 reconstructWorld(vec2 uv,float depth){vec4 c=vec4(uv*2.0-1.0,depth*2.0-1.0,1);vec4 w=uInverseViewProjection*c;return w.xyz/max(abs(w.w),1e-8);}
float shadowVisibility(vec3 p,vec3 n){
    float ndl=max(dot(n,uLightDirection),0.0);if(ndl<=0.0)return 0.0;
    vec4 c=uLightViewProjection*vec4(p+n*0.025,1.0);if(c.w<=0.0)return 1.0;
    vec3 q=c.xyz/c.w;vec2 uv=q.xy*0.5+0.5;float z=q.z*0.5+0.5;
    if(z<=0.0||z>=1.0||any(lessThan(uv,vec2(0.0)))||any(greaterThan(uv,vec2(1.0))))return 1.0;
    vec2 texel=1.0/vec2(textureSize(uShadow,0));
    float bias=0.00045+(1.0-ndl)*0.0018;
    float blockers=0.0,blockerDepth=0.0;
    for(int i=0;i<12;++i){float d=texture(uShadow,uv+POISSON[i]*texel*3.5).r;if(d<z-bias){blockerDepth+=d;blockers+=1.0;}}
    if(blockers<0.5)return 1.0;
    blockerDepth/=blockers;
    float penumbra=clamp(0.9+max(z-blockerDepth,0.0)*420.0,0.9,7.0)*(1.0+(1.0-ndl)*0.25);
    float visibility=0.0;
    for(int i=0;i<12;++i)visibility+=(z-bias<=texture(uShadow,uv+POISSON[i]*texel*penumbra).r)?1.0:0.0;
    return visibility/12.0;
}
vec3 environmentFill(vec3 n){float up=clamp(n.y*0.5+0.5,0.0,1.0);return mix(vec3(0.014,0.017,0.024),vec3(0.050,0.065,0.090),up);}
void main(){
    float depth=texture(uDepth,vUv).r;
    if(depth>=0.999999){outColor=vec4(0.035,0.035,0.045,1);return;}
    vec4 albedo=texture(uAlbedo,vUv);
    vec3 n=normalize(texture(uNormal,vUv).xyz*2.0-1.0);
    vec3 p=reconstructWorld(vUv,depth);
    float ndl=max(dot(n,uLightDirection),0.0);
    float visibility=shadowVisibility(p,n);
    vec3 direct=vec3(ndl*visibility*1.15);
    vec3 indirect=max(texture(uIndirect,vUv).rgb,vec3(0.0))*1.45;
    vec3 lighting=environmentFill(n)+direct+indirect;
    vec3 color=max(albedo.rgb*lighting,vec3(0.0));
    color=color/(vec3(1.0)+color);
    color=pow(color,vec3(1.0/2.2));
    outColor=vec4(color,albedo.a);
}
)GLSL";

} // namespace

struct GI::Impl {
    struct alignas(16) BvhNode {
        float min_x = 0.0f;
        float min_y = 0.0f;
        float min_z = 0.0f;
        std::uint32_t left = 0u;
        float max_x = 0.0f;
        float max_y = 0.0f;
        float max_z = 0.0f;
        std::uint32_t meta = 0u;
    };

    struct alignas(16) GpuTriangle {
        std::array<float, 4> p0{};
        std::array<float, 4> p1{};
        std::array<float, 4> p2{};
        std::array<float, 4> n0{};
        std::array<float, 4> n1{};
        std::array<float, 4> n2{};
        std::array<float, 4> color{};
    };

    GiSettings settings{};
    bool initialized = false;
    bool geometry_ready = false;
    bool shadow_dirty = true;
    int width = 1;
    int height = 1;
    int gi_width = 1;
    int gi_height = 1;
    int history_index = 0;
    std::uint64_t frame = 0u;
    std::uint64_t geometry_hash = 0u;

    Mat4 current_view_projection = identityMatrix();
    Mat4 previous_view_projection = identityMatrix();
    Mat4 inverse_view_projection = identityMatrix();
    Mat4 inverse_view = identityMatrix();
    Mat4 light_view_projection = identityMatrix();
    Vec3f world_min{};
    Vec3f world_max{};
    Vec3f light_direction = normalize({-0.35f, 0.8f, 0.45f});

    GLuint gbuffer_framebuffer = 0u;
    GLuint gbuffer_albedo = 0u;
    GLuint gbuffer_normal = 0u;
    GLuint gbuffer_velocity = 0u;
    GLuint gbuffer_depth = 0u;
    GLuint shadow_framebuffer = 0u;
    GLuint shadow_depth = 0u;
    GLuint raw = 0u;
    std::array<GLuint, 2> history{};
    std::array<GLuint, 2> geometry{};
    std::array<GLuint, 2> denoise{};
    GLuint node_buffer = 0u;
    GLuint triangle_buffer = 0u;
    GLuint gbuffer_program = 0u;
    GLuint shadow_program = 0u;
    GLuint trace_program = 0u;
    GLuint temporal_program = 0u;
    GLuint denoise_program = 0u;
    GLuint compose_program = 0u;
    std::vector<BvhNode> nodes;
    std::vector<GpuTriangle> triangles;

    bool active() const { return initialized && settings.enabled; }

    void updateResolution()
    {
        const int divisor = std::clamp(settings.resolution_divisor, 4, 16);
        gi_width = std::max(width / divisor, 1);
        gi_height = std::max(height / divisor, 1);
    }

    static void hashValue(std::uint64_t& hash, std::uint32_t value)
    {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }

    static void hashFloat(std::uint64_t& hash, float value)
    {
        hashValue(hash, std::bit_cast<std::uint32_t>(value));
    }

    std::uint64_t geometryHash(const Ecs::World& world) const
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh || !transform) continue;
            hashValue(hash, entity);
            hashValue(hash, mesh->mesh);
            hashValue(hash, mesh->material);
            hashFloat(hash, transform->position.x);
            hashFloat(hash, transform->position.y);
            hashFloat(hash, transform->position.z);
            hashFloat(hash, transform->rotation.x);
            hashFloat(hash, transform->rotation.y);
            hashFloat(hash, transform->rotation.z);
            hashFloat(hash, transform->scale.x);
            hashFloat(hash, transform->scale.y);
            hashFloat(hash, transform->scale.z);
        }
        return hash;
    }

    Vec3f materialColor(std::uint32_t handle, std::unordered_map<std::uint32_t, Vec3f>& cache) const
    {
        const auto found = cache.find(handle);
        if (found != cache.end()) return found->second;

        const Models::MaterialData *material = Models::material(handle);
        Vec3f result = material
            ? Vec3f{material->color.x, material->color.y, material->color.z}
            : Vec3f{1.0f, 1.0f, 1.0f};

        if (material && material->diffuse_texture != Models::INVALID_TEXTURE) {
            const Models::TextureAsset *texture = Models::texture(material->diffuse_texture);
            if (texture && texture->image.rgba.size() >= 4u) {
                const std::size_t pixels = texture->image.rgba.size() / 4u;
                const std::size_t stride = std::max<std::size_t>(pixels / 2048u, 1u);
                std::uint64_t r = 0u, g = 0u, b = 0u;
                std::size_t samples = 0u;
                for (std::size_t pixel = 0u; pixel < pixels; pixel += stride) {
                    const std::size_t offset = pixel * 4u;
                    r += texture->image.rgba[offset + 0u];
                    g += texture->image.rgba[offset + 1u];
                    b += texture->image.rgba[offset + 2u];
                    ++samples;
                }
                if (samples != 0u) {
                    const float inv = 1.0f / (255.0f * static_cast<float>(samples));
                    result.x *= static_cast<float>(r) * inv;
                    result.y *= static_cast<float>(g) * inv;
                    result.z *= static_cast<float>(b) * inv;
                }
            }
        }
        cache.emplace(handle, result);
        return result;
    }

    static Vec3f triangleCentroid(const GpuTriangle& triangle)
    {
        return {
            (triangle.p0[0] + triangle.p1[0] + triangle.p2[0]) / 3.0f,
            (triangle.p0[1] + triangle.p1[1] + triangle.p2[1]) / 3.0f,
            (triangle.p0[2] + triangle.p1[2] + triangle.p2[2]) / 3.0f,
        };
    }

    std::uint32_t buildNode(std::uint32_t start, std::uint32_t count)
    {
        const float infinity = std::numeric_limits<float>::infinity();
        Vec3f bounds_min{infinity, infinity, infinity};
        Vec3f bounds_max{-infinity, -infinity, -infinity};
        Vec3f centroid_min{infinity, infinity, infinity};
        Vec3f centroid_max{-infinity, -infinity, -infinity};

        for (std::uint32_t i = 0u; i < count; ++i) {
            const GpuTriangle& triangle = triangles[start + i];
            const Vec3f p0{triangle.p0[0], triangle.p0[1], triangle.p0[2]};
            const Vec3f p1{triangle.p1[0], triangle.p1[1], triangle.p1[2]};
            const Vec3f p2{triangle.p2[0], triangle.p2[1], triangle.p2[2]};
            bounds_min = minVec(bounds_min, minVec(p0, minVec(p1, p2)));
            bounds_max = maxVec(bounds_max, maxVec(p0, maxVec(p1, p2)));
            const Vec3f centroid = triangleCentroid(triangle);
            centroid_min = minVec(centroid_min, centroid);
            centroid_max = maxVec(centroid_max, centroid);
        }

        const std::uint32_t index = static_cast<std::uint32_t>(nodes.size());
        nodes.push_back({bounds_min.x, bounds_min.y, bounds_min.z, 0u, bounds_max.x, bounds_max.y, bounds_max.z, 0u});
        const Vec3f extent = subtract(centroid_max, centroid_min);
        int axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > component(extent, axis)) axis = 2;

        if (count <= kLeafSize || component(extent, axis) <= 1.0e-6f) {
            nodes[index].left = start;
            nodes[index].meta = kLeafBit | count;
            return index;
        }

        const std::uint32_t left_count = count / 2u;
        const std::uint32_t middle = start + left_count;
        std::nth_element(
            triangles.begin() + start,
            triangles.begin() + middle,
            triangles.begin() + start + count,
            [axis](const GpuTriangle& a, const GpuTriangle& b) {
                return component(triangleCentroid(a), axis) < component(triangleCentroid(b), axis);
            }
        );
        const std::uint32_t left = buildNode(start, left_count);
        const std::uint32_t right = buildNode(middle, count - left_count);
        nodes[index].left = left;
        nodes[index].meta = right;
        return index;
    }

    bool uploadGeometry()
    {
        if (node_buffer == 0u) GL15.glGenBuffers(1, &node_buffer);
        if (triangle_buffer == 0u) GL15.glGenBuffers(1, &triangle_buffer);
        if (node_buffer == 0u || triangle_buffer == 0u) return false;

        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, node_buffer);
        GL15.glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<LWCGLsizeiptr>(nodes.size() * sizeof(BvhNode)), nodes.empty() ? nullptr : nodes.data(), GL_STATIC_DRAW);
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangle_buffer);
        GL15.glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<LWCGLsizeiptr>(triangles.size() * sizeof(GpuTriangle)), triangles.empty() ? nullptr : triangles.data(), GL_STATIC_DRAW);
        GL15.glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
        return true;
    }

    void buildLightMatrix()
    {
        const Vec3f center = scale(add(world_min, world_max), 0.5f);
        const Vec3f extent = subtract(world_max, world_min);
        const float radius = std::max(std::max(extent.x, extent.y), std::max(extent.z, 1.0f)) * 0.75f + 2.0f;
        const Vec3f eye = add(center, scale(light_direction, radius * 2.0f));
        const Vec3f up_hint = std::abs(light_direction.y) > 0.95f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{0.0f, 1.0f, 0.0f};
        const Mat4 light_view = lookAt(eye, center, up_hint);

        Vec3f light_min{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        Vec3f light_max{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
        for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < 2; ++z) {
            const Vec3f corner{x == 0 ? world_min.x : world_max.x, y == 0 ? world_min.y : world_max.y, z == 0 ? world_min.z : world_max.z};
            const Vec3f light_space = transformPoint(light_view, corner);
            light_min = minVec(light_min, light_space);
            light_max = maxVec(light_max, light_space);
        }
        const float padding = radius * 0.04f + 0.5f;
        const float near_plane = std::max(0.1f, -light_max.z - padding);
        const float far_plane = std::max(near_plane + 1.0f, -light_min.z + padding);
        light_view_projection = multiply(
            orthographic(light_min.x - padding, light_max.x + padding, light_min.y - padding, light_max.y + padding, near_plane, far_plane),
            light_view
        );
    }

    bool rebuildGeometry(const Ecs::World& world)
    {
        const std::uint64_t next_hash = geometryHash(world);
        if (geometry_ready && next_hash == geometry_hash) return true;

        const auto start_time = std::chrono::steady_clock::now();
        geometry_hash = next_hash;
        geometry_ready = false;
        shadow_dirty = true;
        nodes.clear();
        triangles.clear();

        const float infinity = std::numeric_limits<float>::infinity();
        world_min = {infinity, infinity, infinity};
        world_max = {-infinity, -infinity, -infinity};
        std::unordered_map<std::uint32_t, Vec3f> material_cache;
        bool limit_hit = false;

        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;

            const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
            if (!mesh || mesh->indices.size() < 3u) continue;
            const Models::MaterialData *material = Models::material(mesh_component->material);
            if (material && material->opacity < 0.5f) continue;

            const Vec3f color = materialColor(mesh_component->material, material_cache);
            const Mat4 model = modelMatrix(*transform);
            const Mat4 inverse_model = inverseModelMatrix(*transform);

            for (std::size_t i = 0u; i + 2u < mesh->indices.size(); i += 3u) {
                if (triangles.size() >= kMaximumTriangles) { limit_hit = true; break; }
                const std::uint32_t i0 = mesh->indices[i + 0u], i1 = mesh->indices[i + 1u], i2 = mesh->indices[i + 2u];
                if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size()) continue;
                const Models::Vertex& v0 = mesh->vertices[i0];
                const Models::Vertex& v1 = mesh->vertices[i1];
                const Models::Vertex& v2 = mesh->vertices[i2];
                const Vec3f p0 = transformPoint(model, v0.position), p1 = transformPoint(model, v1.position), p2 = transformPoint(model, v2.position);
                const Vec3f n0 = transformNormal(inverse_model, v0.normal), n1 = transformNormal(inverse_model, v1.normal), n2 = transformNormal(inverse_model, v2.normal);
                GpuTriangle triangle{};
                triangle.p0 = {p0.x, p0.y, p0.z, 0.0f};
                triangle.p1 = {p1.x, p1.y, p1.z, 0.0f};
                triangle.p2 = {p2.x, p2.y, p2.z, 0.0f};
                triangle.n0 = {n0.x, n0.y, n0.z, 0.0f};
                triangle.n1 = {n1.x, n1.y, n1.z, 0.0f};
                triangle.n2 = {n2.x, n2.y, n2.z, 0.0f};
                triangle.color = {color.x, color.y, color.z, material ? std::clamp(material->opacity, 0.0f, 1.0f) : 1.0f};
                triangles.push_back(triangle);
                world_min = minVec(world_min, minVec(p0, minVec(p1, p2)));
                world_max = maxVec(world_max, maxVec(p0, maxVec(p1, p2)));
            }
            if (limit_hit) break;
        }

        if (triangles.empty()) {
            world_min = {-1.0f, -1.0f, -1.0f};
            world_max = {1.0f, 1.0f, 1.0f};
        }
        if (limit_hit) {
            std::fprintf(stderr, "[GI]: scene exceeds %zu ray-tracing triangles; using screen-space GI only\n", kMaximumTriangles);
            triangles.clear();
            settings.bvh_fallback = false;
        } else if (settings.bvh_fallback && !triangles.empty()) {
            buildNode(0u, static_cast<std::uint32_t>(triangles.size()));
        }

        if (!uploadGeometry()) return false;
        buildLightMatrix();
        geometry_ready = true;
        history_index = 0;
        frame = 0u;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
        std::fprintf(stderr, "[GI]: geometry %zu triangles, %zu BVH nodes, %.1f ms build\n", triangles.size(), nodes.size(), ms);
        return true;
    }

    bool createPrograms()
    {
        gbuffer_program = createGraphicsProgram(kGBufferVertexShader, kGBufferFragmentShader);
        shadow_program = createGraphicsProgram(kShadowVertexShader, kShadowFragmentShader);
        trace_program = createComputeProgram(kTraceShader);
        temporal_program = createComputeProgram(kTemporalShader);
        denoise_program = createComputeProgram(kDenoiseShader);
        compose_program = createGraphicsProgram(kComposeVertexShader, kComposeFragmentShader);
        return gbuffer_program && shadow_program && trace_program && temporal_program && denoise_program && compose_program;
    }

    void destroyPrograms()
    {
        if (!GL20.glDeleteProgram) return;
        GLuint *programs[] = {&gbuffer_program, &shadow_program, &trace_program, &temporal_program, &denoise_program, &compose_program};
        for (GLuint *program : programs) {
            if (*program != 0u) GL20.glDeleteProgram(*program);
            *program = 0u;
        }
    }

    bool createGBuffer()
    {
        gbuffer_albedo = createTexture2D(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false);
        gbuffer_normal = createTexture2D(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, false);
        gbuffer_velocity = createTexture2D(width, height, GL_RG16F, GL_RG, GL_FLOAT, false);
        gbuffer_depth = createTexture2D(width, height, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, false);
        if (!gbuffer_albedo || !gbuffer_normal || !gbuffer_velocity || !gbuffer_depth) return false;

        GL30.glGenFramebuffers(1, &gbuffer_framebuffer);
        if (!gbuffer_framebuffer) return false;
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_framebuffer);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gbuffer_albedo, 0);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gbuffer_normal, 0);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gbuffer_velocity, 0);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gbuffer_depth, 0);
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        GL20.glDrawBuffers(3, attachments);
        const GLenum status = GL30.glCheckFramebufferStatus(GL_FRAMEBUFFER);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        return status == GL_FRAMEBUFFER_COMPLETE;
    }

    void destroyGBuffer()
    {
        if (gbuffer_framebuffer && GL30.glDeleteFramebuffers) GL30.glDeleteFramebuffers(1, &gbuffer_framebuffer);
        gbuffer_framebuffer = 0u;
        deleteTexture(gbuffer_albedo);
        deleteTexture(gbuffer_normal);
        deleteTexture(gbuffer_velocity);
        deleteTexture(gbuffer_depth);
    }

    bool createShadow()
    {
        shadow_depth = createTexture2D(kShadowSize, kShadowSize, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, true);
        if (!shadow_depth) return false;
        GL30.glGenFramebuffers(1, &shadow_framebuffer);
        if (!shadow_framebuffer) return false;
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer);
        GL30.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_depth, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const GLenum status = GL30.glCheckFramebufferStatus(GL_FRAMEBUFFER);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        shadow_dirty = true;
        return status == GL_FRAMEBUFFER_COMPLETE;
    }

    void destroyShadow()
    {
        if (shadow_framebuffer && GL30.glDeleteFramebuffers) GL30.glDeleteFramebuffers(1, &shadow_framebuffer);
        shadow_framebuffer = 0u;
        deleteTexture(shadow_depth);
    }

    bool createGiBuffers()
    {
        raw = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
        for (std::size_t i = 0; i < 2u; ++i) {
            history[i] = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
            geometry[i] = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, false);
            denoise[i] = createTexture2D(gi_width, gi_height, GL_RGBA16F, GL_RGBA, GL_FLOAT, true);
        }
        return raw && history[0] && history[1] && geometry[0] && geometry[1] && denoise[0] && denoise[1];
    }

    void destroyGiBuffers()
    {
        deleteTexture(raw);
        for (std::size_t i = 0; i < 2u; ++i) {
            deleteTexture(history[i]);
            deleteTexture(geometry[i]);
            deleteTexture(denoise[i]);
        }
    }

    void renderShadow(const Ecs::World& world)
    {
        if (!shadow_dirty) return;
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, shadow_framebuffer);
        glViewport(0, 0, kShadowSize, kShadowSize);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 3.0f);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        GL20.glUseProgram(shadow_program);
        setMatrix(shadow_program, "uLightViewProjection", light_view_projection);

        for (const Ecs::Entity entity : world.entities()) {
            const RenderableComponent *renderable = world.get<RenderableComponent>(entity);
            const MeshComponent *mesh_component = world.get<MeshComponent>(entity);
            const Transform *transform = world.get<Transform>(entity);
            if (!renderable || !renderable->visible || !mesh_component || !transform) continue;
            const Models::MaterialData *material = Models::material(mesh_component->material);
            if (material && material->opacity < 0.5f) continue;
            const Models::MeshData *mesh = Models::mesh(mesh_component->mesh);
            if (!mesh || mesh->indices.empty()) continue;
            setMatrix(shadow_program, "uModel", modelMatrix(*transform));
            glBegin(GL_TRIANGLES);
            for (std::uint32_t index : mesh->indices) {
                if (index >= mesh->vertices.size()) continue;
                const Models::Vertex& vertex = mesh->vertices[index];
                glVertex3f(vertex.position.x, vertex.position.y, vertex.position.z);
            }
            glEnd();
        }

        GL20.glUseProgram(0u);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(GL_BACK);
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        glViewport(0, 0, width, height);
        shadow_dirty = false;
    }

    void beginGBuffer()
    {
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, gbuffer_framebuffer);
        const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        GL20.glDrawBuffers(3, attachments);
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        GL20.glUseProgram(gbuffer_program);
        setInt(gbuffer_program, "uDiffuse", 0);
        setMatrix(gbuffer_program, "uInverseView", inverse_view);
        setMatrix(gbuffer_program, "uPreviousViewProjection", previous_view_projection);
    }

    void endGBuffer()
    {
        GL20.glUseProgram(0u);
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        GL42.glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glClearColor(0.035f, 0.035f, 0.045f, 1.0f);
    }

    void trace()
    {
        GL20.glUseProgram(trace_program);
        bindTextureUnit(0, gbuffer_albedo);
        bindTextureUnit(1, gbuffer_normal);
        bindTextureUnit(2, gbuffer_depth);
        bindTextureUnit(3, shadow_depth);
        setInt(trace_program, "uAlbedo", 0);
        setInt(trace_program, "uNormal", 1);
        setInt(trace_program, "uDepth", 2);
        setInt(trace_program, "uShadow", 3);
        setMatrix(trace_program, "uViewProjection", current_view_projection);
        setMatrix(trace_program, "uInverseViewProjection", inverse_view_projection);
        setMatrix(trace_program, "uLightViewProjection", light_view_projection);
        setVec3(trace_program, "uLightDirection", light_direction);
        setSize(trace_program, "uOutputSize", gi_width, gi_height);
        setInt(trace_program, "uFrame", static_cast<int>(frame & 0x7fffffffu));
        setInt(trace_program, "uNodeCount", static_cast<int>(nodes.size()));
        setInt(trace_program, "uUseScreen", settings.screen_space_first ? 1 : 0);
        setInt(trace_program, "uUseBvh", settings.bvh_fallback && !nodes.empty() ? 1 : 0);
        setInt(trace_program, "uRaysPerPixel", std::clamp(settings.rays_per_pixel, 1, 2));
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0u, node_buffer);
        GL30.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1u, triangle_buffer);
        GL42.glBindImageTexture(0u, raw, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL43.glDispatchCompute(static_cast<GLuint>((gi_width + 7) / 8), static_cast<GLuint>((gi_height + 7) / 8), 1u);
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    GLuint temporal()
    {
        const int next = history_index ^ 1;
        GL20.glUseProgram(temporal_program);
        bindTextureUnit(0, raw);
        bindTextureUnit(1, history[history_index]);
        bindTextureUnit(2, geometry[history_index]);
        bindTextureUnit(3, gbuffer_depth);
        bindTextureUnit(4, gbuffer_normal);
        bindTextureUnit(5, gbuffer_velocity);
        setInt(temporal_program, "uRaw", 0);
        setInt(temporal_program, "uPrevious", 1);
        setInt(temporal_program, "uPreviousGeometry", 2);
        setInt(temporal_program, "uDepth", 3);
        setInt(temporal_program, "uNormal", 4);
        setInt(temporal_program, "uVelocity", 5);
        setSize(temporal_program, "uOutputSize", gi_width, gi_height);
        setFloat(temporal_program, "uAlpha", settings.temporal_alpha);
        setFloat(temporal_program, "uDepthReject", settings.depth_rejection);
        setFloat(temporal_program, "uNormalReject", settings.normal_rejection);
        setInt(temporal_program, "uHasHistory", frame > 0u ? 1 : 0);
        GL42.glBindImageTexture(0u, history[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL42.glBindImageTexture(1u, geometry[next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        GL43.glDispatchCompute(static_cast<GLuint>((gi_width + 7) / 8), static_cast<GLuint>((gi_height + 7) / 8), 1u);
        GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        return history[next];
    }

    GLuint filter(GLuint source)
    {
        GLuint input = source;
        const int iterations = std::clamp(settings.denoise_iterations, 1, 3);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            const GLuint output = denoise[static_cast<std::size_t>(iteration & 1)];
            GL20.glUseProgram(denoise_program);
            bindTextureUnit(0, input);
            bindTextureUnit(1, gbuffer_depth);
            bindTextureUnit(2, gbuffer_normal);
            setInt(denoise_program, "uInput", 0);
            setInt(denoise_program, "uDepth", 1);
            setInt(denoise_program, "uNormal", 2);
            setSize(denoise_program, "uOutputSize", gi_width, gi_height);
            setInt(denoise_program, "uStep", 1 << iteration);
            GL42.glBindImageTexture(0u, output, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            GL43.glDispatchCompute(static_cast<GLuint>((gi_width + 7) / 8), static_cast<GLuint>((gi_height + 7) / 8), 1u);
            GL42.glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            input = output;
        }
        return input;
    }

    void compose(GLuint indirect)
    {
        GL30.glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        GL20.glUseProgram(compose_program);
        bindTextureUnit(0, gbuffer_albedo);
        bindTextureUnit(1, gbuffer_normal);
        bindTextureUnit(2, gbuffer_depth);
        bindTextureUnit(3, indirect);
        bindTextureUnit(4, shadow_depth);
        setInt(compose_program, "uAlbedo", 0);
        setInt(compose_program, "uNormal", 1);
        setInt(compose_program, "uDepth", 2);
        setInt(compose_program, "uIndirect", 3);
        setInt(compose_program, "uShadow", 4);
        setMatrix(compose_program, "uInverseViewProjection", inverse_view_projection);
        setMatrix(compose_program, "uLightViewProjection", light_view_projection);
        setVec3(compose_program, "uLightDirection", light_direction);
        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f(3.0f, -1.0f);
        glVertex2f(-1.0f, 3.0f);
        glEnd();
        GL20.glUseProgram(0u);
        GLModern.glActiveTexture(GL_TEXTURE0);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
    }

    void destroyGeometryBuffers()
    {
        if (!GL15.glDeleteBuffers) return;
        if (node_buffer) GL15.glDeleteBuffers(1, &node_buffer);
        if (triangle_buffer) GL15.glDeleteBuffers(1, &triangle_buffer);
        node_buffer = 0u;
        triangle_buffer = 0u;
    }
};

GI::GI() : impl_(new Impl) {}

GI::~GI()
{
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool GI::init(int width, int height)
{
    if (impl_->initialized) return true;
    impl_->width = std::max(width, 1);
    impl_->height = std::max(height, 1);
    impl_->updateResolution();

    if (!lwcglModernGLAvailable() && lwcglLoadModernGL() != 0) {
        std::fprintf(stderr, "[GI]: modern OpenGL unavailable\n");
        return false;
    }
    const int major = lwcglModernGLMajorVersion();
    const int minor = lwcglModernGLMinorVersion();
    if (major < 4 || (major == 4 && minor < 3) || !GL43.glDispatchCompute || !GL42.glBindImageTexture || !GL30.glBindBufferBase) {
        std::fprintf(stderr, "[GI]: OpenGL 4.3 compatibility context required; found %d.%d\n", major, minor);
        return false;
    }

    if (!impl_->createPrograms() || !impl_->createGBuffer() || !impl_->createShadow() || !impl_->createGiBuffers()) {
        shutdown();
        return false;
    }
    impl_->history_index = 0;
    impl_->frame = 0u;
    impl_->geometry_ready = false;
    impl_->initialized = true;
    std::fprintf(stderr, "[GI]: OpenGL %d.%d, framebuffer %dx%d, GI %dx%d, shadow %dx%d\n", major, minor, impl_->width, impl_->height, impl_->gi_width, impl_->gi_height, kShadowSize, kShadowSize);
    return true;
}

void GI::resize(int width, int height)
{
    const int new_width = std::max(width, 1);
    const int new_height = std::max(height, 1);
    if (new_width == impl_->width && new_height == impl_->height) return;
    impl_->width = new_width;
    impl_->height = new_height;
    impl_->updateResolution();
    if (!impl_->initialized) return;
    impl_->destroyGBuffer();
    impl_->destroyGiBuffers();
    if (!impl_->createGBuffer() || !impl_->createGiBuffers()) {
        shutdown();
        return;
    }
    impl_->history_index = 0;
    impl_->frame = 0u;
}

void GI::begin(const Ecs::World& world)
{
    if (!impl_->active()) return;
    Mat4 projection{};
    Mat4 view{};
    glGetFloatv(GL_PROJECTION_MATRIX, projection.data());
    glGetFloatv(GL_MODELVIEW_MATRIX, view.data());
    const Mat4 captured = multiply(projection, view);
    impl_->previous_view_projection = impl_->frame == 0u ? captured : impl_->current_view_projection;
    impl_->current_view_projection = captured;
    if (!inverseMatrix(captured, impl_->inverse_view_projection)) impl_->inverse_view_projection = identityMatrix();
    if (!inverseMatrix(view, impl_->inverse_view)) impl_->inverse_view = identityMatrix();
    if (!impl_->rebuildGeometry(world)) {
        std::fprintf(stderr, "[GI]: failed to build scene acceleration data\n");
        impl_->settings.enabled = false;
        return;
    }
    impl_->renderShadow(world);
    impl_->beginGBuffer();
}

void GI::bindMaterial(unsigned int texture_id)
{
    if (!impl_->active()) return;
    GL20.glUseProgram(impl_->gbuffer_program);
    bindTextureUnit(0, static_cast<GLuint>(texture_id));
    setInt(impl_->gbuffer_program, "uDiffuse", 0);
    setInt(impl_->gbuffer_program, "uHasTexture", texture_id != 0u ? 1 : 0);
}

void GI::end(const Ecs::World& world)
{
    (void)world;
    if (!impl_->active()) return;
    impl_->endGBuffer();
    impl_->trace();
    GLuint result = impl_->raw;
    if (impl_->settings.temporal_reuse) result = impl_->temporal();
    if (impl_->settings.denoise) result = impl_->filter(result);
    impl_->compose(result);
    if (impl_->settings.temporal_reuse) impl_->history_index ^= 1;
    ++impl_->frame;
}

void GI::shutdown()
{
    if (!impl_) return;
    if (GL20.glUseProgram) GL20.glUseProgram(0u);
    impl_->destroyPrograms();
    impl_->destroyGiBuffers();
    impl_->destroyShadow();
    impl_->destroyGBuffer();
    impl_->destroyGeometryBuffers();
    impl_->nodes.clear();
    impl_->triangles.clear();
    impl_->initialized = false;
    impl_->geometry_ready = false;
    impl_->shadow_dirty = true;
    impl_->geometry_hash = 0u;
    impl_->history_index = 0;
    impl_->frame = 0u;
}

bool GI::initialized() const { return impl_->initialized; }
bool GI::enabled() const { return impl_->settings.enabled; }
void GI::setEnabled(bool enabled) { impl_->settings.enabled = enabled; }
GiSettings& GI::settings() { return impl_->settings; }
const GiSettings& GI::settings() const { return impl_->settings; }

} // namespace Renderer
