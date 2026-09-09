#ifndef RW_ENGINE_RENDERER_SYSTEMS_PROGRESSIVE_STATE_HPP
#define RW_ENGINE_RENDERER_SYSTEMS_PROGRESSIVE_STATE_HPP

#include <algorithm>
#include <cstdint>

namespace Renderer::Systems {

class ProgressiveState {
public:
    void reset()
    {
        sample_count_ = 0u;
        frame_index_ = 0u;
        phase_count_ = 0u;
        reset_pending_ = true;
        camera_moving_ = false;
        was_camera_moving_ = false;
        camera_signature_ = 0u;
        light_signature_ = 0u;
        gi_signature_ = 0u;
    }

    void resetAccumulation()
    {
        sample_count_ = 0u;
        reset_pending_ = true;
    }

    bool updateCamera(std::uint64_t signature)
    {
        const bool changed = signature != camera_signature_;
        camera_moving_ = changed;
        if (changed) {
            camera_signature_ = signature;
            resetAccumulation();
        } else if (was_camera_moving_) {
            resetAccumulation();
        }
        was_camera_moving_ = changed;
        return changed;
    }

    bool updateLight(std::uint64_t signature)
    {
        if (signature == light_signature_) return false;
        light_signature_ = signature;
        resetAccumulation();
        return true;
    }

    bool updateGlobalIllumination(std::uint64_t signature)
    {
        if (signature == gi_signature_) return false;
        gi_signature_ = signature;
        resetAccumulation();
        return true;
    }

    void sceneChanged() { resetAccumulation(); }

    void advance(std::uint32_t samples)
    {
        sample_count_ += samples;
        ++frame_index_;
        phase_count_ = std::min<std::uint32_t>(phase_count_ + 1u, 4u);
        reset_pending_ = false;
    }

    std::uint32_t sampleCount() const { return sample_count_; }
    std::uint32_t frameIndex() const { return frame_index_; }
    std::uint32_t phaseCount() const { return phase_count_; }
    bool resetPending() const { return reset_pending_; }
    bool cameraMoving() const { return camera_moving_; }

private:
    std::uint32_t sample_count_ = 0u;
    std::uint32_t frame_index_ = 0u;
    std::uint32_t phase_count_ = 0u;
    bool reset_pending_ = true;
    bool camera_moving_ = false;
    bool was_camera_moving_ = false;
    std::uint64_t camera_signature_ = 0u;
    std::uint64_t light_signature_ = 0u;
    std::uint64_t gi_signature_ = 0u;
};

} // namespace Renderer::Systems

#endif
