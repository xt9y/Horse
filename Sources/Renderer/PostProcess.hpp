#ifndef HORSE_RENDERER_POST_PROCESS_HPP
#define HORSE_RENDERER_POST_PROCESS_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Renderer::PostProcess {

enum class GraphicsApi : std::uint8_t {
    OpenGL,
    Metal,
};

enum class DepthSource : std::uint8_t {
    None,
    Native,
    LinearTexture,
};

struct Frame {
    GraphicsApi api = GraphicsApi::OpenGL;
    DepthSource depth = DepthSource::None;
    int width = 1;
    int height = 1;
    void *command = nullptr;
    void *color_texture = nullptr;
    void *depth_texture = nullptr;
    void *velocity_texture = nullptr;
};

class Pass {
public:
    virtual ~Pass() = default;
    virtual bool process(Frame& frame) = 0;
    virtual void shutdown() {}
};

class Pipeline {
public:
    template <typename T, typename... Args>
    T& add(Args&&... args)
    {
        static_assert(std::is_base_of_v<Pass, T>);
        auto pass = std::make_unique<T>(std::forward<Args>(args)...);
        T& reference = *pass;
        passes_.push_back(std::move(pass));
        return reference;
    }

    bool process(Frame& frame);
    void shutdown();
    void clear();
    std::size_t count() const { return passes_.size(); }

private:
    std::vector<std::unique_ptr<Pass>> passes_;
};

} // namespace Renderer::PostProcess

#endif
