#include "Renderer/RenderGraph/RenderGraph.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>

namespace Renderer::RenderGraph {
namespace {

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

} // namespace

Resource Graph::resource(const char *name, bool external)
{
    const Resource id = static_cast<Resource>(resources_.size());
    resources_.push_back(ResourceNode{name ? name : "", external});
    compiled_ = false;
    return id;
}

Pass Graph::pass(const char *name, Callback callback)
{
    const Pass id = static_cast<Pass>(passes_.size());
    passes_.push_back(PassNode{name ? name : "", std::move(callback), {}, {}, {}});
    compiled_ = false;
    return id;
}

void Graph::read(Pass pass_id, Resource resource_id)
{
    if (pass_id >= passes_.size() || resource_id >= resources_.size()) return;
    auto& values = passes_[pass_id].reads;
    if (std::find(values.begin(), values.end(), resource_id) == values.end())
        values.push_back(resource_id);
    compiled_ = false;
}

void Graph::write(Pass pass_id, Resource resource_id)
{
    if (pass_id >= passes_.size() || resource_id >= resources_.size()) return;
    auto& values = passes_[pass_id].writes;
    if (std::find(values.begin(), values.end(), resource_id) == values.end())
        values.push_back(resource_id);
    compiled_ = false;
}

void Graph::depends(Pass pass_id, Pass dependency)
{
    if (pass_id >= passes_.size() || dependency >= passes_.size() || pass_id == dependency)
        return;
    auto& values = passes_[pass_id].dependencies;
    if (std::find(values.begin(), values.end(), dependency) == values.end())
        values.push_back(dependency);
    compiled_ = false;
}

bool Graph::compile(std::string *error)
{
    if (error) error->clear();
    order_.clear();
    lifetimes_.assign(resources_.size(), Lifetime{});

    const std::size_t pass_count = passes_.size();
    std::vector<Pass> writers(resources_.size(), InvalidPass);
    std::vector<std::vector<Pass>> readers(resources_.size());

    for (Pass pass_id = 0u; pass_id < pass_count; ++pass_id) {
        const PassNode& node = passes_[pass_id];
        for (const Resource resource_id : node.writes) {
            if (resource_id >= resources_.size())
                return fail(error, "render graph pass references invalid write resource");
            if (writers[resource_id] != InvalidPass && writers[resource_id] != pass_id)
                return fail(
                    error,
                    "render graph resource has multiple writers: " + resources_[resource_id].name
                );
            writers[resource_id] = pass_id;
        }
        for (const Resource resource_id : node.reads) {
            if (resource_id >= resources_.size())
                return fail(error, "render graph pass references invalid read resource");
            readers[resource_id].push_back(pass_id);
        }
    }

    for (Resource resource_id = 0u; resource_id < resources_.size(); ++resource_id) {
        if (!resources_[resource_id].external &&
            writers[resource_id] == InvalidPass &&
            !readers[resource_id].empty())
        {
            return fail(
                error,
                "render graph transient resource has no writer: " + resources_[resource_id].name
            );
        }
    }

    std::vector<std::vector<Pass>> edges(pass_count);
    std::vector<std::uint32_t> indegree(pass_count, 0u);
    const auto add_edge = [&](Pass before, Pass after) {
        if (before == after || before >= pass_count || after >= pass_count) return;
        auto& values = edges[before];
        if (std::find(values.begin(), values.end(), after) != values.end()) return;
        values.push_back(after);
        ++indegree[after];
    };

    for (Pass pass_id = 0u; pass_id < pass_count; ++pass_id) {
        for (const Pass dependency : passes_[pass_id].dependencies)
            add_edge(dependency, pass_id);
    }

    for (Resource resource_id = 0u; resource_id < resources_.size(); ++resource_id) {
        const Pass writer = writers[resource_id];
        if (writer == InvalidPass) continue;
        for (const Pass reader : readers[resource_id])
            add_edge(writer, reader);
    }

    std::priority_queue<Pass, std::vector<Pass>, std::greater<Pass>> ready;
    for (Pass pass_id = 0u; pass_id < pass_count; ++pass_id) {
        if (indegree[pass_id] == 0u) ready.push(pass_id);
    }

    while (!ready.empty()) {
        const Pass pass_id = ready.top();
        ready.pop();
        order_.push_back(pass_id);
        for (const Pass next : edges[pass_id]) {
            if (--indegree[next] == 0u) ready.push(next);
        }
    }

    if (order_.size() != pass_count) {
        order_.clear();
        return fail(error, "render graph contains a dependency cycle");
    }

    std::vector<std::size_t> positions(pass_count, 0u);
    for (std::size_t position = 0u; position < order_.size(); ++position)
        positions[order_[position]] = position;

    for (Resource resource_id = 0u; resource_id < resources_.size(); ++resource_id) {
        Lifetime lifetime;
        lifetime.first = std::numeric_limits<std::size_t>::max();
        lifetime.last = 0u;

        const auto touch = [&](Pass pass_id) {
            if (pass_id == InvalidPass || pass_id >= pass_count) return;
            const std::size_t position = positions[pass_id];
            lifetime.first = std::min(lifetime.first, position);
            lifetime.last = std::max(lifetime.last, position);
            lifetime.valid = true;
        };

        touch(writers[resource_id]);
        for (const Pass reader : readers[resource_id]) touch(reader);
        if (!lifetime.valid) lifetime.first = 0u;
        lifetimes_[resource_id] = lifetime;
    }

    compiled_ = true;
    return true;
}

bool Graph::execute(std::string *error)
{
    if (error) error->clear();
    if (!compiled_ && !compile(error)) return false;

    for (const Pass pass_id : order_) {
        if (pass_id >= passes_.size())
            return fail(error, "render graph contains an invalid compiled pass");
        const PassNode& node = passes_[pass_id];
        if (node.callback && !node.callback())
            return fail(error, "render pass failed: " + node.name);
    }
    return true;
}

Lifetime Graph::lifetime(Resource resource_id) const
{
    if (resource_id >= lifetimes_.size()) return {};
    return lifetimes_[resource_id];
}

void Graph::clear()
{
    resources_.clear();
    passes_.clear();
    order_.clear();
    lifetimes_.clear();
    compiled_ = false;
}

} // namespace Renderer::RenderGraph
