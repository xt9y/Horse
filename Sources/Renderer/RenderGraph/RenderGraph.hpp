#ifndef HORSE_RENDERER_RENDER_GRAPH_HPP
#define HORSE_RENDERER_RENDER_GRAPH_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Renderer::RenderGraph {

using Resource = std::uint32_t;
using Pass = std::uint32_t;

constexpr Resource InvalidResource = UINT32_MAX;
constexpr Pass InvalidPass = UINT32_MAX;

struct Lifetime {
    std::size_t first = 0u;
    std::size_t last = 0u;
    bool valid = false;
};

class Graph {
public:
    using Callback = std::function<bool()>;

    Resource resource(const char *name, bool external = false);
    Pass pass(const char *name, Callback callback);

    void read(Pass pass, Resource resource);
    void write(Pass pass, Resource resource);
    void depends(Pass pass, Pass dependency);

    bool compile(std::string *error = nullptr);
    bool execute(std::string *error = nullptr);

    const std::vector<Pass>& order() const { return order_; }
    Lifetime lifetime(Resource resource) const;

    void clear();

private:
    struct ResourceNode {
        std::string name;
        bool external = false;
    };

    struct PassNode {
        std::string name;
        Callback callback;
        std::vector<Resource> reads;
        std::vector<Resource> writes;
        std::vector<Pass> dependencies;
    };

    std::vector<ResourceNode> resources_;
    std::vector<PassNode> passes_;
    std::vector<Pass> order_;
    std::vector<Lifetime> lifetimes_;
    bool compiled_ = false;
};

} // namespace Renderer::RenderGraph

#endif
