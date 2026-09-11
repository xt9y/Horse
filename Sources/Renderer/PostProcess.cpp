#include "Renderer/PostProcess.hpp"

namespace Renderer::PostProcess {

bool Pipeline::process(Frame& frame)
{
    for (const std::unique_ptr<Pass>& pass : passes_) {
        if (pass && !pass->process(frame)) return false;
    }
    return true;
}

void Pipeline::shutdown()
{
    for (auto iterator = passes_.rbegin(); iterator != passes_.rend(); ++iterator) {
        if (*iterator) (*iterator)->shutdown();
    }
}

void Pipeline::clear()
{
    shutdown();
    passes_.clear();
}

} // namespace Renderer::PostProcess
