#pragma once

#include <filesystem>
#include <memory>

namespace app {

class IntroMusic {
public:
    IntroMusic();
    ~IntroMusic();

    void start(const std::filesystem::path& path);
    void stop();

private:
    struct Playback;
    std::unique_ptr<Playback> playback_;
};

} // namespace app
