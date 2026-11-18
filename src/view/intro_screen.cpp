// Loads the title and main menu art and animates their reveals.

#include "game_ui.h"

#include "asset_manager.h"
#include "display_size.h"
#include "logger.h"
#include "scene_transition.h"

#include <algorithm>
#include <numeric>
#include <random>

namespace wars {
namespace {

constexpr int width = platform::screen_width;
constexpr int height = platform::screen_height;
constexpr int tile_size = 4;
constexpr int tile_columns = (width + tile_size - 1) / tile_size;
constexpr int tile_rows = (height + tile_size - 1) / tile_size;
constexpr std::uint32_t fade_duration_ms = 2000;
constexpr std::uint32_t home_reveal_ms = 1000;

struct HomeArtwork {
    const char* filename;
    int menu_x;
    int menu_y;
};

// Each concept positions its menu just below its title and tagline.
constexpr std::array home_artwork{
    HomeArtwork{"01-after-the-rain.png", 3, 63},
    HomeArtwork{"02-opening-bell.png", 204, 63},
    HomeArtwork{"03-last-flight-out.png", 3, 58},
    HomeArtwork{"04-harbor-market.png", 3, 53},
    HomeArtwork{"05-the-city-is-a-circuit.png", 206, 63},
};

} // namespace

// Try title images in random order and prepare a shuffled tile order for the first decodable image.
bool GameUi::load_intro(const app::AssetManager& assets) {
    constexpr std::size_t max_directory_entries = 64;
    const std::filesystem::path intro_dir = "images/intro";
    const auto directory = assets.resolve(intro_dir);
    std::vector<std::filesystem::path> images;
    std::error_code error;
    std::size_t entries = 0;
    for (std::filesystem::directory_iterator item(directory, error), end;
         !error && item != end && entries < max_directory_entries; item.increment(error), ++entries) {
        std::error_code status_error;
        if (item->path().extension() == ".png" && item->is_regular_file(status_error)) {
            auto path = assets.resolve(intro_dir / item->path().filename());
            if (!path.empty()) images.push_back(std::move(path));
        }
    }
    if (images.empty()) {
        LOG_WARN("No AI Wars intro images found");
        return false;
    }

    std::mt19937 random(std::random_device{}());
    std::shuffle(images.begin(), images.end(), random);
    DrawBuffer image(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB888, LV_STRIDE_AUTO),
                     lv_draw_buf_destroy);
    DrawBuffer frame(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB888, LV_STRIDE_AUTO),
                     lv_draw_buf_destroy);
    if (!image || !frame) {
        LOG_WARN("Could not allocate AI Wars intro buffers");
        return false;
    }
    for (const auto& path : images) {
        if (!app::read_screen_png(path, *image)) {
            LOG_WARN("Skipping unreadable or incorrectly sized intro image: {}", path.filename().string());
            continue;
        }
        intro_tiles_.resize(tile_columns * tile_rows);
        std::iota(intro_tiles_.begin(), intro_tiles_.end(), 0);
        std::shuffle(intro_tiles_.begin(), intro_tiles_.end(), random);
        intro_image_ = std::move(image);
        intro_frame_ = std::move(frame);
        LOG_INFO("AI Wars intro: {}", path.filename().string());
        return true;
    }
    return false;
}

// Create an empty frame for the reveal and place the start prompt above it.
void GameUi::draw_intro() {
    lv_draw_buf_clear(intro_frame_.get(), nullptr);
    intro_canvas_ = lv_canvas_create(screen_);
    lv_canvas_set_draw_buf(intro_canvas_, intro_frame_.get());
    lv_obj_set_pos(intro_canvas_, 0, 0);

    constexpr int prompt_width = 190;
    constexpr int prompt_height = 22;
    auto* backdrop = box((width - prompt_width) / 2, height - prompt_height - 6,
                         prompt_width, prompt_height, 0x000000);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_80, LV_PART_MAIN);
    const auto* font = &lv_font_montserrat_14;
    label("Press Any Key To Start", 0, (prompt_height - lv_font_get_line_height(font)) / 2,
          prompt_width, font, 0xffffff, LV_TEXT_ALIGN_CENTER, false, backdrop);
    clickable(backdrop, LV_KEY_ENTER);
    intro_started_ = lv_tick_get();
}

// Copy just the newly visible tiles to the frame, then release the source once fully revealed.
void GameUi::update_intro() {
    if (!intro_image_) return;

    const auto elapsed = std::min(lv_tick_elaps(intro_started_), fade_duration_ms);
    const auto visible = intro_tiles_.size() * elapsed / fade_duration_ms;
    if (visible == intro_revealed_) return;

    for (; intro_revealed_ < visible; ++intro_revealed_) {
        const int tile = intro_tiles_[intro_revealed_];
        const int x = tile % tile_columns * tile_size;
        const int y = tile / tile_columns * tile_size;
        const lv_area_t area{x, y, std::min(x + tile_size, width) - 1,
                                   std::min(y + tile_size, height) - 1};
        lv_draw_buf_copy(intro_frame_.get(), &area, intro_image_.get(), &area);
    }
    lv_draw_buf_flush_cache(intro_frame_.get(), nullptr);
    lv_obj_invalidate(intro_canvas_);
    if (elapsed == fade_duration_ms) intro_image_.reset();
}

// Pick valid menu art and keep its button coordinates together with the image.
void GameUi::load_home_artwork(bool reveal) {
    auto choices = home_artwork;
    std::mt19937 random(std::random_device{}());
    std::shuffle(choices.begin(), choices.end(), random);
    DrawBuffer image(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB888, LV_STRIDE_AUTO),
                     lv_draw_buf_destroy);
    if (!image) return;

    for (const auto& choice : choices) {
        const auto path = assets_.resolve(std::filesystem::path("images/start-screen") / choice.filename);
        if (!app::read_screen_png(path, *image)) continue;
        home_.emplace(HomeScene{std::move(image), choice.menu_x, choice.menu_y});
        if (reveal) {
            // Start the menu reveal with an opaque black cover that the shared fade makes transparent.
            home_->veil.reset(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO));
            if (home_->veil) {
                const lv_area_t area{0, 0, width - 1, height - 1};
                lv_draw_buf_clear(home_->veil.get(), &area);
                for (int y = 0; y < height; ++y) {
                    auto* row = home_->veil->data + y * home_->veil->header.stride;
                    for (int x = 0; x < width; ++x) row[x * 4 + 3] = LV_OPA_COVER;
                }
                lv_draw_buf_flush_cache(home_->veil.get(), nullptr);
                home_->reveal_started = lv_tick_get();
            }
        }
        LOG_INFO("AI Wars start screen: {}", choice.filename);
        return;
    }
    LOG_WARN("No usable AI Wars start-screen art found");
}

// Fade the menu cover and delete its canvas before releasing its draw buffer.
void GameUi::update_home_reveal() {
    auto& scene = *home_;
    const auto elapsed = lv_tick_elaps(scene.reveal_started);
    if (elapsed >= home_reveal_ms) {
        lv_obj_delete(scene.veil_canvas);
        scene.veil_canvas = nullptr;
        scene.veil.reset();
        return;
    }
    scene_transition::pixel_fade(scene.veil_canvas, *scene.veil, elapsed, scene.fade_position,
                                 home_reveal_ms);
}

} // namespace wars
