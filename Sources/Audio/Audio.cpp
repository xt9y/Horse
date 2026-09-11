#include "Audio/Audio.hpp"

#include "Renderer/Components.hpp"
#include "Renderer/Hierarchy.hpp"
#include "Renderer/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace Audio {
namespace {

using Renderer::Math::Mat4;
using Renderer::Vec3;

std::vector<Clip>& clips()
{
    static std::vector<Clip> values;
    return values;
}

bool fail(std::string *error, const std::string& message)
{
    if (error) *error = message;
    return false;
}

std::uint16_t u16le(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8u);
}

std::uint32_t u32le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8u) |
        (static_cast<std::uint32_t>(data[2]) << 16u) |
        (static_cast<std::uint32_t>(data[3]) << 24u);
}

bool fourcc(const std::uint8_t *data, const char *value)
{
    return data[0] == static_cast<std::uint8_t>(value[0]) &&
        data[1] == static_cast<std::uint8_t>(value[1]) &&
        data[2] == static_cast<std::uint8_t>(value[2]) &&
        data[3] == static_cast<std::uint8_t>(value[3]);
}

bool readFile(const std::string& path, std::vector<std::uint8_t> *bytes, std::string *error)
{
    if (!bytes) return fail(error, "null WAV byte output");
    std::ifstream input(path, std::ios::binary);
    if (!input) return fail(error, "failed to open WAV: " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max())
        return fail(error, "failed to size WAV: " + path);
    input.seekg(0, std::ios::beg);
    bytes->resize(static_cast<std::size_t>(length));
    if (!bytes->empty())
        input.read(reinterpret_cast<char *>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!input && !bytes->empty()) return fail(error, "failed to read WAV: " + path);
    return true;
}

float pcmSample(const std::uint8_t *data, std::uint16_t bits)
{
    switch (bits) {
        case 8:
            return (static_cast<float>(data[0]) - 128.0f) / 128.0f;
        case 16: {
            const std::int16_t value = static_cast<std::int16_t>(u16le(data));
            return std::max(-1.0f, static_cast<float>(value) / 32767.0f);
        }
        case 24: {
            std::int32_t value = static_cast<std::int32_t>(data[0]) |
                (static_cast<std::int32_t>(data[1]) << 8) |
                (static_cast<std::int32_t>(data[2]) << 16);
            if ((value & 0x00800000) != 0) value |= static_cast<std::int32_t>(0xff000000u);
            return std::max(-1.0f, static_cast<float>(value) / 8388607.0f);
        }
        case 32: {
            const std::int32_t value = static_cast<std::int32_t>(u32le(data));
            return std::max(-1.0f, static_cast<float>(static_cast<double>(value) / 2147483647.0));
        }
        default:
            return 0.0f;
    }
}

bool decodeWav(const std::vector<std::uint8_t>& bytes, Clip *output, std::string *error)
{
    if (!output || bytes.size() < 12u || !fourcc(bytes.data(), "RIFF") || !fourcc(bytes.data() + 8u, "WAVE"))
        return fail(error, "invalid RIFF/WAVE header");

    const std::uint64_t riff_size = static_cast<std::uint64_t>(u32le(bytes.data() + 4u)) + 8u;
    if (riff_size > bytes.size() || riff_size < 12u) return fail(error, "WAV RIFF size exceeds input");

    std::uint16_t format = 0u;
    std::uint16_t channels = 0u;
    std::uint32_t sample_rate = 0u;
    std::uint16_t block_align = 0u;
    std::uint16_t bits = 0u;
    const std::uint8_t *samples = nullptr;
    std::size_t sample_bytes = 0u;
    bool have_format = false;

    std::size_t cursor = 12u;
    const std::size_t end = static_cast<std::size_t>(riff_size);
    while (cursor + 8u <= end) {
        const std::uint8_t *header = bytes.data() + cursor;
        const std::uint32_t size = u32le(header + 4u);
        cursor += 8u;
        if (size > end - cursor) return fail(error, "WAV chunk exceeds RIFF bounds");
        const std::uint8_t *data = bytes.data() + cursor;

        if (fourcc(header, "fmt ")) {
            if (size < 16u) return fail(error, "truncated WAV fmt chunk");
            format = u16le(data + 0u);
            channels = u16le(data + 2u);
            sample_rate = u32le(data + 4u);
            block_align = u16le(data + 12u);
            bits = u16le(data + 14u);
            if (format == 0xfffeu) {
                if (size < 40u || u16le(data + 16u) < 22u)
                    return fail(error, "truncated WAVE_FORMAT_EXTENSIBLE data");
                format = u16le(data + 24u);
            }
            have_format = true;
        } else if (fourcc(header, "data")) {
            samples = data;
            sample_bytes = size;
        }

        cursor += size;
        if ((size & 1u) != 0u && cursor < end) ++cursor;
    }

    if (!have_format || !samples) return fail(error, "WAV is missing fmt or data chunk");
    if (format != 1u && format != 3u) return fail(error, "unsupported WAV format (only PCM and IEEE float are supported)");
    if (channels == 0u || sample_rate == 0u || channels > 32u)
        return fail(error, "invalid WAV channel count or sample rate");
    if (format == 1u && bits != 8u && bits != 16u && bits != 24u && bits != 32u)
        return fail(error, "unsupported PCM WAV bit depth");
    if (format == 3u && bits != 32u && bits != 64u)
        return fail(error, "unsupported floating-point WAV bit depth");

    const std::size_t bytes_per_sample = static_cast<std::size_t>(bits) / 8u;
    const std::size_t expected_align = static_cast<std::size_t>(channels) * bytes_per_sample;
    if (bytes_per_sample == 0u || block_align != expected_align || sample_bytes % block_align != 0u)
        return fail(error, "invalid WAV block alignment");
    const std::size_t sample_count = sample_bytes / bytes_per_sample;
    if (sample_count > output->samples.max_size()) return fail(error, "WAV sample count is too large");

    output->sample_rate = sample_rate;
    output->channels = channels;
    output->samples.resize(sample_count);
    for (std::size_t i = 0u; i < sample_count; ++i) {
        const std::uint8_t *source = samples + i * bytes_per_sample;
        float value = 0.0f;
        if (format == 1u) {
            value = pcmSample(source, bits);
        } else if (bits == 32u) {
            std::uint32_t raw = u32le(source);
            std::memcpy(&value, &raw, sizeof(value));
        } else {
            std::uint64_t raw = 0u;
            for (unsigned int byte = 0u; byte < 8u; ++byte)
                raw |= static_cast<std::uint64_t>(source[byte]) << (byte * 8u);
            double sample = 0.0;
            std::memcpy(&sample, &raw, sizeof(sample));
            value = static_cast<float>(sample);
        }
        output->samples[i] = std::clamp(value, -1.0f, 1.0f);
    }
    return true;
}

Vec3 subtract(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float length(Vec3 value)
{
    return std::sqrt(Renderer::Math::dot(value, value));
}

Vec3 position(const Ecs::World& world, Ecs::Entity entity)
{
    Mat4 matrix{};
    if (!Renderer::Hierarchy::worldMatrix(world, entity, &matrix)) return {};
    return {matrix[12], matrix[13], matrix[14]};
}

Vec3 right(const Ecs::World& world, Ecs::Entity entity)
{
    Mat4 matrix{};
    if (!Renderer::Hierarchy::worldMatrix(world, entity, &matrix)) return {1.0f, 0.0f, 0.0f};
    return Renderer::Math::normalize({matrix[0], matrix[1], matrix[2]});
}

float attenuation(const SourceComponent& source, float distance)
{
    const float minimum = std::max(source.min_distance, 0.0001f);
    const float maximum = std::max(source.max_distance, minimum);
    if (distance <= minimum) return 1.0f;
    if (distance >= maximum) return 0.0f;
    const float rolloff = std::max(source.rolloff, 0.0f);
    if (rolloff <= 0.0f) return 1.0f;
    const float inverse = minimum / (minimum + rolloff * (distance - minimum));
    const float fade = (maximum - distance) / (maximum - minimum);
    return std::clamp(inverse * std::min(1.0f, fade * 4.0f), 0.0f, 1.0f);
}

float channelSample(const Clip& value, std::size_t frame, std::uint16_t channel)
{
    if (value.channels == 0u || frame >= value.frameCount()) return 0.0f;
    const std::uint16_t source_channel = std::min<std::uint16_t>(channel, value.channels - 1u);
    return value.samples[frame * value.channels + source_channel];
}

float interpolated(const Clip& value, double frame, std::uint16_t channel, bool loop)
{
    const std::size_t count = value.frameCount();
    if (count == 0u) return 0.0f;
    double position = frame;
    if (loop) {
        position = std::fmod(position, static_cast<double>(count));
        if (position < 0.0) position += static_cast<double>(count);
    } else {
        position = std::clamp(position, 0.0, static_cast<double>(count - 1u));
    }
    const std::size_t first = static_cast<std::size_t>(position);
    const std::size_t second = loop ? (first + 1u) % count : std::min(first + 1u, count - 1u);
    const float t = static_cast<float>(position - static_cast<double>(first));
    const float a = channelSample(value, first, channel);
    const float b = channelSample(value, second, channel);
    return a + (b - a) * t;
}

struct ListenerState {
    bool valid = false;
    Ecs::Entity entity = Ecs::INVALID_ENTITY;
    Vec3 position{};
    Vec3 right {1.0f, 0.0f, 0.0f};
    float gain = 1.0f;
};

ListenerState listener(const Ecs::World& world)
{
    ListenerState result;
    world.each<ListenerComponent>(
        [&](Ecs::Entity entity, const ListenerComponent& component) {
            if (result.valid) return;
            result.valid = true;
            result.entity = entity;
            result.position = position(world, entity);
            result.right = right(world, entity);
            result.gain = std::max(component.gain, 0.0f);
        }
    );
    return result;
}

} // namespace

ClipHandle registerClip(Clip value)
{
    if (value.channels == 0u || value.sample_rate == 0u || value.samples.empty() ||
        value.samples.size() % value.channels != 0u)
        return INVALID_CLIP;
    const ClipHandle handle = static_cast<ClipHandle>(clips().size());
    clips().push_back(std::move(value));
    return handle;
}

ClipHandle loadWav(const std::string& path, std::string *error)
{
    if (error) error->clear();
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, &bytes, error)) return INVALID_CLIP;
    Clip value;
    if (!decodeWav(bytes, &value, error)) return INVALID_CLIP;
    const ClipHandle handle = registerClip(std::move(value));
    if (handle == INVALID_CLIP) fail(error, "invalid decoded WAV clip");
    return handle;
}

const Clip *clip(ClipHandle handle)
{
    return handle < clips().size() ? &clips()[handle] : nullptr;
}

void clearClips()
{
    clips().clear();
}

void play(SourceComponent& source, bool restart)
{
    if (restart) source.frame = 0.0;
    source.playing = source.clip != INVALID_CLIP;
}

void pause(SourceComponent& source)
{
    source.playing = false;
}

void stop(SourceComponent& source)
{
    source.playing = false;
    source.frame = 0.0;
}

Mixer::Mixer(std::uint32_t sample_rate, std::uint16_t channels)
{
    configure(sample_rate, channels);
}

void Mixer::configure(std::uint32_t sample_rate, std::uint16_t channels)
{
    sample_rate_ = sample_rate == 0u ? 48000u : sample_rate;
    channels_ = channels == 0u ? 2u : channels;
}

void Mixer::mix(Ecs::World& world, float *output, std::size_t frames) const
{
    if (!output || frames == 0u || channels_ == 0u) return;
    if (frames > std::numeric_limits<std::size_t>::max() / channels_) return;
    std::fill(output, output + frames * channels_, 0.0f);

    const ListenerState active_listener = listener(world);
    bool changed = false;

    world.each<SourceComponent>(
        [&](Ecs::Entity entity, SourceComponent& source) {
            if (!source.playing) return;
            const Clip *value = clip(source.clip);
            if (!value || value->frameCount() == 0u || value->sample_rate == 0u) {
                source.playing = false;
                source.frame = 0.0;
                changed = true;
                return;
            }

            float left_gain = std::max(source.gain, 0.0f);
            float right_gain = left_gain;
            if (source.spatial && active_listener.valid) {
                const Vec3 delta = subtract(position(world, entity), active_listener.position);
                const float distance = length(delta);
                const Vec3 direction = distance > 1.0e-6f
                    ? Renderer::Math::normalize(delta)
                    : Vec3{};
                const float pan = std::clamp(Renderer::Math::dot(direction, active_listener.right), -1.0f, 1.0f);
                const float distance_gain = attenuation(source, distance) * active_listener.gain;
                const float angle = (pan + 1.0f) * 0.78539816339744830962f;
                left_gain *= std::cos(angle) * 1.41421356237f * distance_gain;
                right_gain *= std::sin(angle) * 1.41421356237f * distance_gain;
            } else if (active_listener.valid) {
                left_gain *= active_listener.gain;
                right_gain *= active_listener.gain;
            }

            const double step = static_cast<double>(value->sample_rate) /
                static_cast<double>(sample_rate_) * std::max(static_cast<double>(source.pitch), 0.0001);
            const std::size_t source_frames = value->frameCount();

            for (std::size_t frame = 0u; frame < frames; ++frame) {
                if (source.frame >= static_cast<double>(source_frames)) {
                    if (source.loop) {
                        source.frame = std::fmod(source.frame, static_cast<double>(source_frames));
                    } else {
                        source.playing = false;
                        source.frame = static_cast<double>(source_frames);
                        changed = true;
                        break;
                    }
                }

                float left = 0.0f;
                float right_sample = 0.0f;
                if (source.spatial) {
                    float mono = 0.0f;
                    for (std::uint16_t channel = 0u; channel < value->channels; ++channel)
                        mono += interpolated(*value, source.frame, channel, source.loop);
                    mono /= static_cast<float>(value->channels);
                    left = mono * left_gain;
                    right_sample = mono * right_gain;
                } else {
                    left = interpolated(*value, source.frame, 0u, source.loop) * left_gain;
                    right_sample = interpolated(*value, source.frame, value->channels > 1u ? 1u : 0u, source.loop) * right_gain;
                }

                const std::size_t destination = frame * channels_;
                if (channels_ == 1u) {
                    output[destination] += 0.5f * (left + right_sample);
                } else {
                    output[destination] += left;
                    output[destination + 1u] += right_sample;
                    const float surround = 0.5f * (left + right_sample);
                    for (std::uint16_t channel = 2u; channel < channels_; ++channel)
                        output[destination + channel] += surround;
                }
                source.frame += step;
                changed = true;
            }
        }
    );

    for (std::size_t i = 0u; i < frames * channels_; ++i)
        output[i] = std::clamp(output[i], -1.0f, 1.0f);

    if (changed) world.markChanged(Ecs::ChangeKind::Audio);
}

std::vector<float> Mixer::mix(Ecs::World& world, std::size_t frames) const
{
    if (channels_ == 0u || frames > std::numeric_limits<std::size_t>::max() / channels_) return {};
    std::vector<float> output(frames * channels_, 0.0f);
    mix(world, output.data(), frames);
    return output;
}

} // namespace Audio
