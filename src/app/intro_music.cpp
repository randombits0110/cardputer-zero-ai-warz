#include "intro_music.h"

#include "logger.h"

#include <miniaudio.h>

#include <algorithm>
#include <utility>

namespace app {
namespace {

constexpr ma_uint32 channels = 2;
constexpr ma_uint32 sample_rate = 44100;
constexpr ma_uint64 loop_gap_frames = 2 * sample_rate;
constexpr std::uintmax_t max_music_bytes = 4 * 1024 * 1024;

} // namespace

struct IntroMusic::Playback {
    ma_decoder decoder{};
    ma_device device{};
    bool decoder_ready = false;
    bool device_ready = false;
    bool failed = false;
    bool decoded_loop = false;
    ma_uint64 silence_frames = 0;

    ~Playback() {
        if (device_ready) ma_device_uninit(&device);
        if (decoder_ready) ma_decoder_uninit(&decoder);
    }

    ma_result start(const std::filesystem::path& path) {
        auto decoder_config = ma_decoder_config_init(ma_format_f32, channels, sample_rate);
        decoder_config.encodingFormat = ma_encoding_format_mp3;
#ifdef _WIN32
        auto result = ma_decoder_init_file_w(path.c_str(), &decoder_config, &decoder);
#else
        auto result = ma_decoder_init_file(path.c_str(), &decoder_config, &decoder);
#endif
        if (result != MA_SUCCESS) return result;
        decoder_ready = true;

        auto device_config = ma_device_config_init(ma_device_type_playback);
        device_config.playback.format = ma_format_f32;
        device_config.playback.channels = channels;
        device_config.sampleRate = sample_rate;
        device_config.dataCallback = render;
        device_config.pUserData = this;
        result = ma_device_init(nullptr, &device_config, &device);
        if (result != MA_SUCCESS) return result;
        device_ready = true;
        return ma_device_start(&device);
    }

    static void render(ma_device* device, void* output, const void*, ma_uint32 frame_count) {
        auto& playback = *static_cast<Playback*>(device->pUserData);
        if (playback.failed) return;
        auto* samples = static_cast<float*>(output);
        ma_uint64 remaining = frame_count;
        while (remaining > 0) {
            if (playback.silence_frames > 0) {
                // Miniaudio supplies a pre-silenced buffer. Leave these frames untouched.
                const auto skipped = std::min(playback.silence_frames, remaining);
                playback.silence_frames -= skipped;
                remaining -= skipped;
                samples += skipped * channels;
                continue;
            }

            ma_uint64 read = 0;
            const auto result = ma_decoder_read_pcm_frames(&playback.decoder, samples, remaining, &read);
            remaining -= read;
            samples += read * channels;
            playback.decoded_loop = playback.decoded_loop || read != 0;
            if (result != MA_SUCCESS && result != MA_AT_END) {
                playback.failed = true;
                return;
            }
            if (result == MA_AT_END || remaining > 0) {
                if (!playback.decoded_loop || ma_decoder_seek_to_pcm_frame(&playback.decoder, 0) != MA_SUCCESS) {
                    playback.failed = true;
                    return;
                }
                playback.decoded_loop = false;
                playback.silence_frames = loop_gap_frames;
            }
        }
    }
};

IntroMusic::IntroMusic() = default;
IntroMusic::~IntroMusic() = default;

void IntroMusic::start(const std::filesystem::path& path) {
    if (playback_) return;
    if (path.empty()) {
        LOG_WARN("Intro music asset not found: audio/intro.mp3");
        return;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        LOG_WARN("Intro music must be a regular file");
        return;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > max_music_bytes) {
        LOG_WARN("Intro music file size is outside its limit");
        return;
    }

    auto playback = std::make_unique<Playback>();
    const auto result = playback->start(path);
    if (result != MA_SUCCESS) {
        LOG_WARN("Could not play intro music: {}", ma_result_description(result));
        return;
    }
    playback_ = std::move(playback);
}

void IntroMusic::stop() {
    playback_.reset();
}

} // namespace app
