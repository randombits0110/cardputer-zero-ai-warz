// Animates travel and provides shared slide and pixel-fade effects for game scenes.

#include "game_ui.h"

#include "arrival_time.h"
#include "asset_manager.h"
#include "display_size.h"
#include "logger.h"
#include "scene_transition.h"

#include <algorithm>

namespace wars {
namespace {

constexpr int width = platform::screen_width;
constexpr int height = platform::screen_height;
constexpr int tile_size = 4;
constexpr int fade_spread = 36;
constexpr int tile_columns = (width + tile_size - 1) / tile_size;
constexpr int tile_count = tile_columns * ((height + tile_size - 1) / tile_size);
constexpr std::uint32_t hold_ms = 1000;
using scene_transition::flight_fade_ms;

// Derive a stable clear position for a staggered edge or a dissolve across the whole image.
int fade_threshold(int x, int y, scene_transition::FadePattern pattern) {
    auto tile = static_cast<std::uint32_t>((y / tile_size) * tile_columns + x / tile_size);
    tile *= 0x9e3779b9U;
    tile ^= tile >> 16;
    if (pattern == scene_transition::FadePattern::Random) return 1 + static_cast<int>(tile % tile_count);
    return x + 1 + static_cast<int>(tile % fade_spread);
}

} // namespace

// Use one screen width and the standard scene duration for incoming artwork.
int scene_transition::slide_x(std::uint32_t elapsed) {
    return slide_offset(width, elapsed, slide_ms);
}

// Calculate the remaining offset with a quadratic ease-out curve.
int scene_transition::slide_offset(int distance, std::uint32_t elapsed, std::uint32_t duration) {
    const auto remaining = duration - std::min(elapsed, duration);
    const auto duration_squared = duration * duration;
    return (distance * remaining * remaining + duration_squared - 1) / duration_squared;
}

// Erase tiles crossed by the fade since the last tick and invalidate just the affected screen area.
void scene_transition::pixel_fade(lv_obj_t* canvas, lv_draw_buf_t& image,
                                  std::uint32_t elapsed, int& previous_position, std::uint32_t duration,
                                  FadePattern pattern) {
    const bool random = pattern == FadePattern::Random;
    const int position = (random ? tile_count : width + fade_spread) * std::min(elapsed, duration) / duration;
    if (position <= previous_position) return;
    const int left = random ? 0 : std::max(0, (previous_position - fade_spread) / tile_size * tile_size);
    const int right = random ? width : std::min(width, position);
    for (int y = 0; y < height; y += tile_size) {
        for (int x = left; x < right; x += tile_size) {
            const int threshold = fade_threshold(x, y, pattern);
            if (threshold <= previous_position || threshold > position) continue;
            for (int row = y; row < std::min(y + tile_size, height); ++row) {
                auto* pixels = image.data + row * image.header.stride;
                for (int column = x; column < std::min(x + tile_size, width); ++column) {
                    // Clear the alpha byte so the canvas reveals the widgets underneath.
                    pixels[column * 4 + 3] = 0;
                }
            }
        }
    }
    const lv_area_t changed{left, 0, std::min(width, right + tile_size - 1) - 1, height - 1};
    lv_draw_buf_flush_cache(&image, &changed);
    lv_obj_invalidate_area(canvas, &changed);
    previous_position = position;
}

// Select the destination art by local time, with dusk as a fallback, then start the arrival slide.
void GameUi::begin_flight() {
    message_.clear();
    const auto& city = profile_.active->economy.cities[destination_];
    const auto minute = platform::destination_minute(city.id, platform::arrival_clock());
    const auto lighting = minute ? platform::arrival_lighting(*minute) : "dusk";
    if (!minute) LOG_WARN("Destination time unavailable for {}; using dusk", city.id);

    if (city.id.find_first_of("/\\:") != std::string::npos || city.id.find('\0') != std::string::npos) {
        if (commit_flight()) show_arrival();
        return;
    }
    const auto filename = (city.id == "new-york" ? "new-york-city" : city.id) + ".png";
    const auto root = std::filesystem::path("images/arrivals-v2");
    DrawBuffer image(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO),
                     lv_draw_buf_destroy);
    bool loaded = image && app::read_screen_png(assets_.resolve(root / lighting / filename), *image);
    if (image && !loaded && lighting != "dusk") {
        loaded = app::read_screen_png(assets_.resolve(root / "dusk" / filename), *image);
    }
    if (!loaded) {
        LOG_WARN("Could not load arrival image for {} ({})", city.id, lighting);
        if (commit_flight()) show_arrival();
        return;
    }

    flight_image_ = std::move(image);
    draw_flight(width);
    flight_started_ = lv_tick_get();
}

// Create a canvas that borrows the supplied draw buffer and place it at the given x-coordinate.
lv_obj_t* GameUi::draw_scene_image(lv_draw_buf_t& image, int x, lv_obj_t* parent) {
    auto* canvas = lv_canvas_create(parent ? parent : screen_);
    lv_canvas_set_draw_buf(canvas, &image);
    lv_obj_set_pos(canvas, x, 0);
    return canvas;
}

// Create the current flight canvas over the active screen.
void GameUi::draw_flight(int x) {
    flight_canvas_ = draw_scene_image(*flight_image_, x);
}

// Save travel after the slide, hold the landing image, and then fade it out.
void GameUi::update_flight() {
    if (!flight_landed_) {
        const auto elapsed = lv_tick_elaps(flight_started_);
        if (elapsed < scene_transition::slide_ms) {
            lv_obj_set_x(flight_canvas_, scene_transition::slide_x(elapsed));
            return;
        }

        if (!commit_flight()) {
            clear_flight();
            render();
            return;
        }
        // Rebuild the destination market while the image covers the screen. Pending
        // arrival events are opened after the market has been revealed.
        page_ = Page::Market;
        render();
        draw_flight(0);
        flight_landed_ = lv_tick_get();
        return;
    }

    const auto elapsed = lv_tick_elaps(*flight_landed_);
    if (elapsed <= hold_ms) return;
    if (elapsed >= hold_ms + flight_fade_ms) {
        finish_flight();
        return;
    }

    scene_transition::pixel_fade(flight_canvas_, *flight_image_, elapsed - hold_ms,
                                 flight_fade_position_, flight_fade_ms);
}

// Delete the borrowing canvas before releasing its image and resetting the flight state.
void GameUi::clear_flight() {
    lv_obj_delete(flight_canvas_);
    flight_canvas_ = nullptr;
    flight_image_.reset();
    flight_landed_.reset();
    flight_fade_position_ = 0;
}

// Save travel if the animation was skipped before landing, then show the next arrival screen.
void GameUi::finish_flight() {
    const bool arrived = flight_landed_.has_value() || commit_flight();
    clear_flight();
    if (arrived) show_arrival();
    render();
}

} // namespace wars
