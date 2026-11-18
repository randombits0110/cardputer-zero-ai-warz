// Dissolves the final score into the player's ending artwork and types out its epilogue.

#include "game_ui.h"

#include "asset_manager.h"
#include "display_size.h"
#include "logger.h"
#include "scene_transition.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <random>

namespace wars {
namespace {

constexpr int width = platform::screen_width;
constexpr int height = platform::screen_height;
constexpr std::uint32_t dissolve_ms = 3000;
constexpr std::uint32_t artwork_hold_ms = 2000;
constexpr std::uint32_t prompt_hold_ms = 3000;
constexpr std::uint32_t prompt_fade_ms = 1500;
constexpr int text_inset = 8;
constexpr int panel_padding = 4;
constexpr int text_width = width - 2 * text_inset;
static_assert(width * height - 1 <= std::numeric_limits<std::uint16_t>::max());

struct Ending {
    const char* artwork;
    const char* title;
    const char* epilogue;
};

const Ending& ending_for(Money score) {
    static constexpr Ending apartment{
        "01-modest-apartment.png",
        "The (Mostly) Broke Visionary",
        "30 days trading cutting-edge AI technology, and you're still doing laundry on your balcony. "
        "Your landlord doesn't accept 'unrealized gains' as rent.",
    };
    static constexpr Ending suburb{
        "02-cape-cod-suburb.png",
        "The Moderately Wealthy",
        "Congratulations, you're a millionaire! Nice house, ocean view, "
        "and neighbors who still think GPU is a type of SUV.",
    };
    static constexpr Ending mansion{
        "03-tropical-megamansion.png",
        "The Disgustingly Rich",
        "Your yacht has a yacht. Your accountant has PTSD. "
        "You've officially become everything you used to complain about!",
    };
    static constexpr Ending station{
        "04-orbital-palace.png",
        "The Galactic Overlord",
        "You bought Earth. Ram Altman offered to buy it back with stock options "
        "and a free trial of consciousness. You declined.",
    };
    if (score <= 1'000'000) return apartment;
    if (score <= 10'000'000) return suburb;
    if (score <= 100'000'000) return mansion;
    return station;
}

} // namespace

void GameUi::begin_ending(Money score) {
    page_ = Page::Ending;
    auto& scene = ending_.emplace();
    const auto& ending = ending_for(score);
    scene.artwork = ending.artwork;
    scene.font = assets_.load_font("Montserrat-Medium.ttf", 13);
    if (!scene.font) scene.font = &lv_font_montserrat_12;
    const auto heading = "$" + format_number(score) + " - " + ending.title;
    lv_point_t heading_size{};
    lv_text_get_size(&heading_size, heading.c_str(), scene.font, 0, 0, text_width, LV_TEXT_FLAG_EXPAND);
    if (heading_size.x > text_width) scene.letter_space = -1;
    scene.text = heading + "\n" + ending.epilogue;
    scene.veil.reset(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO));
    if (!scene.veil) {
        scene.phase = EndingPhase::ArtworkHold;
        render();
    } else {
        scene.pixels.resize(width * height);
        std::iota(scene.pixels.begin(), scene.pixels.end(), std::uint16_t{0});
        std::mt19937 random(std::random_device{}());
        std::shuffle(scene.pixels.begin(), scene.pixels.end(), random);
        lv_draw_buf_clear(scene.veil.get(), nullptr);
        scene.veil_canvas = draw_scene_image(*scene.veil, 0);
    }
    scene.started = lv_tick_get();
}

// Replace the score widgets behind the opaque cover so the artwork starts at full black.
void GameUi::draw_ending() {
    auto& scene = *ending_;
    lv_obj_set_style_bg_color(screen_, lv_color_black(), LV_PART_MAIN);
    const auto path = assets_.resolve(std::filesystem::path("images/endgame") / scene.artwork);
    scene.image.reset(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB888, LV_STRIDE_AUTO));
    if (scene.image && app::read_screen_png(path, *scene.image)) {
        draw_scene_image(*scene.image, 0);
    } else {
        scene.image.reset();
        LOG_WARN("Could not load ending image: {}", scene.artwork);
    }
    if (scene.veil) scene.veil_canvas = draw_scene_image(*scene.veil, 0);
}

void GameUi::update_ending() {
    auto& scene = *ending_;
    const auto elapsed = lv_tick_elaps(scene.started);
    const auto next_phase = [&](EndingPhase phase) {
        scene.phase = phase;
        scene.started = lv_tick_get();
        scene.visible = 0;
    };
    const int prompt_y = height - panel_padding - lv_font_get_line_height(scene.font);

    switch (scene.phase) {
        case EndingPhase::FadeOut:
        case EndingPhase::FadeIn: {
            const bool fade_out = scene.phase == EndingPhase::FadeOut;
            const auto visible = scene.pixels.size() * std::min(elapsed, dissolve_ms) / dissolve_ms;
            if (scene.visible != visible) {
                const auto alpha = fade_out ? LV_OPA_COVER : LV_OPA_TRANSP;
                for (; scene.visible < visible; ++scene.visible) {
                    const int pixel = scene.pixels[scene.visible];
                    scene.veil->data[pixel / width * scene.veil->header.stride + pixel % width * 4 + 3] = alpha;
                }
                lv_draw_buf_flush_cache(scene.veil.get(), nullptr);
                lv_obj_invalidate(scene.veil_canvas);
            }
            if (elapsed < dissolve_ms) break;
            if (fade_out) {
                next_phase(EndingPhase::FadeIn);
                render();
                scene.started = lv_tick_get();
            } else {
                lv_obj_delete(scene.veil_canvas);
                scene.veil_canvas = nullptr;
                scene.veil.reset();
                next_phase(EndingPhase::ArtworkHold);
            }
            break;
        }
        case EndingPhase::ArtworkHold: {
            if (elapsed < artwork_hold_ms) break;
            lv_point_t size{};
            lv_text_get_size(&size, scene.text.c_str(), scene.font, scene.letter_space, 0, text_width, LV_TEXT_FLAG_NONE);
            const int dialogue_y = prompt_y - panel_padding - size.y;
            const int panel_y = dialogue_y - panel_padding;
            scene.panel = box(0, panel_y, width, height - panel_y, 0x000000);
            lv_obj_set_style_bg_opa(scene.panel, LV_OPA_80, LV_PART_MAIN);
            scene.dialogue = label("$", text_inset, dialogue_y, text_width, scene.font, 0xffffff,
                                   LV_TEXT_ALIGN_CENTER, true);
            lv_obj_set_style_text_letter_space(scene.dialogue, scene.letter_space, LV_PART_MAIN);
            next_phase(EndingPhase::Typing);
            scene.visible = 1;
            break;
        }
        case EndingPhase::Typing: {
            const auto visible = std::min(scene.text.size(), std::size_t{1} + elapsed / scene_transition::character_ms);
            if (visible != scene.visible) {
                lv_label_set_text(scene.dialogue, scene.text.substr(0, visible).c_str());
                scene.visible = visible;
            }
            if (visible == scene.text.size()) next_phase(EndingPhase::PromptHold);
            break;
        }
        case EndingPhase::PromptHold:
            if (elapsed >= prompt_hold_ms) {
                scene.prompt = label("Press any key", text_inset, prompt_y, text_width, scene.font,
                                     0xffffff, LV_TEXT_ALIGN_CENTER);
                lv_obj_set_style_text_opa(scene.prompt, LV_OPA_TRANSP, LV_PART_MAIN);
                clickable(scene.prompt, LV_KEY_ENTER);
                next_phase(EndingPhase::PromptFade);
            }
            break;
        case EndingPhase::PromptFade:
            lv_obj_set_style_text_opa(scene.prompt, LV_OPA_COVER * std::min(elapsed, prompt_fade_ms)
                                                   / prompt_fade_ms, LV_PART_MAIN);
            if (elapsed >= prompt_fade_ms) next_phase(EndingPhase::Ready);
            break;
        case EndingPhase::Ready:
        case EndingPhase::ArtworkView:
            break;
    }
}

void GameUi::advance_ending() {
    auto& scene = *ending_;
    if (scene.phase == EndingPhase::Ready) {
        lv_obj_delete(scene.dialogue);
        lv_obj_delete(scene.panel);
        scene.dialogue = nullptr;
        scene.panel = nullptr;
        auto* shadow = label(lv_label_get_text(scene.prompt), lv_obj_get_x(scene.prompt) + 1,
                             lv_obj_get_y(scene.prompt) + 1, text_width, scene.font, 0x000000,
                             LV_TEXT_ALIGN_CENTER);
        lv_obj_move_to_index(shadow, lv_obj_get_index(scene.prompt));
        scene.phase = EndingPhase::ArtworkView;
        keys_.clear();
        return;
    }
    if (scene.phase != EndingPhase::ArtworkView) return;
    page_ = Page::Home;
    home_row_ = 1;
    keys_.clear();
    render();
    if (profile_.sound_enabled) intro_music_queued_at_ = lv_tick_get();
}

} // namespace wars
