#include <Renderer/RenderGraph/RenderGraph.hpp>

#include <cassert>
#include <string>
#include <vector>

int main()
{
    using Renderer::RenderGraph::Graph;

    Graph graph;
    const auto depth = graph.resource("Depth");
    const auto hi_z = graph.resource("Hi-Z");
    const auto visibility = graph.resource("Visibility");
    const auto lighting = graph.resource("Lighting", true);
    const auto opaque = graph.resource("Opaque");

    std::vector<std::string> executed;

    const auto opaque_pass = graph.pass("Opaque", [&] {
        executed.emplace_back("Opaque");
        return true;
    });
    graph.read(opaque_pass, visibility);
    graph.read(opaque_pass, lighting);
    graph.write(opaque_pass, opaque);

    const auto visibility_pass = graph.pass("Visibility", [&] {
        executed.emplace_back("Visibility");
        return true;
    });
    graph.read(visibility_pass, hi_z);
    graph.write(visibility_pass, visibility);

    const auto depth_pass = graph.pass("Depth", [&] {
        executed.emplace_back("Depth");
        return true;
    });
    graph.write(depth_pass, depth);

    const auto hi_z_pass = graph.pass("Hi-Z", [&] {
        executed.emplace_back("Hi-Z");
        return true;
    });
    graph.read(hi_z_pass, depth);
    graph.write(hi_z_pass, hi_z);

    std::string error;
    assert(graph.compile(&error));
    assert(error.empty());
    assert(graph.order().size() == 4u);
    assert(graph.order()[0] == depth_pass);
    assert(graph.order()[1] == hi_z_pass);
    assert(graph.order()[2] == visibility_pass);
    assert(graph.order()[3] == opaque_pass);

    const auto depth_lifetime = graph.lifetime(depth);
    assert(depth_lifetime.valid);
    assert(depth_lifetime.first == 0u);
    assert(depth_lifetime.last == 1u);

    const auto lighting_lifetime = graph.lifetime(lighting);
    assert(lighting_lifetime.valid);
    assert(lighting_lifetime.first == 3u);
    assert(lighting_lifetime.last == 3u);

    assert(graph.execute(&error));
    assert((executed == std::vector<std::string>{"Depth", "Hi-Z", "Visibility", "Opaque"}));

    Graph dependency;
    const auto first = dependency.pass("First", [] { return true; });
    const auto second = dependency.pass("Second", [] { return true; });
    dependency.depends(first, second);
    assert(dependency.compile(&error));
    assert(dependency.order()[0] == second);
    assert(dependency.order()[1] == first);

    Graph cycle;
    const auto cycle_a = cycle.pass("A", [] { return true; });
    const auto cycle_b = cycle.pass("B", [] { return true; });
    cycle.depends(cycle_a, cycle_b);
    cycle.depends(cycle_b, cycle_a);
    assert(!cycle.compile(&error));
    assert(!error.empty());

    Graph writers;
    const auto shared = writers.resource("Shared");
    const auto writer_a = writers.pass("Writer A", [] { return true; });
    const auto writer_b = writers.pass("Writer B", [] { return true; });
    writers.write(writer_a, shared);
    writers.write(writer_b, shared);
    assert(!writers.compile(&error));
    assert(!error.empty());

    Graph missing;
    const auto transient = missing.resource("Transient");
    const auto reader = missing.pass("Reader", [] { return true; });
    missing.read(reader, transient);
    assert(!missing.compile(&error));
    assert(!error.empty());

    Graph failed;
    const auto fail = failed.pass("Failure", [] { return false; });
    (void)fail;
    assert(failed.compile(&error));
    assert(!failed.execute(&error));
    assert(error.find("Failure") != std::string::npos);

    return 0;
}
