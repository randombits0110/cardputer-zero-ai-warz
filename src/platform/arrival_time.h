// Provides the clock and destination lighting selection used by arrival artwork.

#pragma once

#include <ctime>
#include <optional>
#include <string_view>

namespace platform {

// Read the current instant from the device's preferred clock.
std::time_t arrival_clock();
// Convert an instant to minutes after midnight in the destination city, if its zone is available.
std::optional<int> destination_minute(std::string_view city, std::time_t instant);

// Map a local minute of the day to an arrival art directory.
constexpr std::string_view arrival_lighting(int minute) {
    if (minute >= 10 * 60 && minute <= 16 * 60) return "day";
    if (minute >= 5 * 60 + 30 && minute <= 20 * 60 + 30) return "dusk";
    return "night";
}

} // namespace platform
