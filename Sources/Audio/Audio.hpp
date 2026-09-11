#ifndef HORSE_AUDIO_AUDIO_HPP
#define HORSE_AUDIO_AUDIO_HPP

#include "Ecs/Ecs.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Audio {

using ClipHandle = std::uint32_t;
constexpr ClipHandle INVALID_CLIP = UINT32_MAX;

struct Clip {
    std::uint32_t sample_rate = 0u;
    std::uint16_t channels = 0u;
    std::vector<float> samples;

    std::size_t frameCount() const
    {
        return channels == 0u ? 0u : samples.size() / channels;
    }
};

ClipHandle registerClip(Clip clip);
ClipHandle loadWav(const std::string& path, std::string *error = nullptr);
const Clip *clip(ClipHandle handle);
void clearClips();

struct ListenerComponent {
    float gain = 1.0f;
};

struct SourceComponent {
    ClipHandle clip = INVALID_CLIP;
    float gain = 1.0f;
    float pitch = 1.0f;
    float min_distance = 1.0f;
    float max_distance = 100.0f;
    float rolloff = 1.0f;
    bool loop = false;
    bool spatial = true;
    bool playing = false;
    double frame = 0.0;
};

void play(SourceComponent& source, bool restart = true);
void pause(SourceComponent& source);
void stop(SourceComponent& source);

class Mixer {
public:
    explicit Mixer(std::uint32_t sample_rate = 48000u, std::uint16_t channels = 2u);

    void configure(std::uint32_t sample_rate, std::uint16_t channels = 2u);
    std::uint32_t sampleRate() const { return sample_rate_; }
    std::uint16_t channels() const { return channels_; }

    void mix(Ecs::World& world, float *output, std::size_t frames) const;
    std::vector<float> mix(Ecs::World& world, std::size_t frames) const;

private:
    std::uint32_t sample_rate_ = 48000u;
    std::uint16_t channels_ = 2u;
};

} // namespace Audio

#endif
