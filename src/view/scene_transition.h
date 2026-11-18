// Declares shared slide and pixel-reveal effects for full-screen artwork and dialogue scenes.

#pragma once

#include "lvgl.h"

#include <cstdint>

namespace wars::scene_transition {

inline constexpr std::uint32_t slide_ms = 420;
inline constexpr std::uint32_t fade_ms = 700;
inline constexpr std::uint32_t flight_fade_ms = fade_ms + 750;
inline constexpr std::uint32_t character_ms = 25;
inline constexpr std::uint32_t blink_ms = 400;

// Return the screen-wide slide's horizontal offset at the elapsed time in milliseconds.
int slide_x(std::uint32_t elapsed);
// Ease the offset from the given pixel distance to zero over a duration in milliseconds.
int slide_offset(int distance, std::uint32_t elapsed, std::uint32_t duration);
enum class FadePattern { LeftToRight, Random };
// Clear newly reached tiles in an ARGB8888 screen buffer and update the caller's fade position.
void pixel_fade(lv_obj_t* canvas, lv_draw_buf_t& image, std::uint32_t elapsed,
                int& previous_position, std::uint32_t duration = fade_ms,
                FadePattern pattern = FadePattern::LeftToRight);

} // namespace wars::scene_transition
