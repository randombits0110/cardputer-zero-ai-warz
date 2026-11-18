// Displays officer encounters and shared illustrated story scenes.

#include "game_ui.h"

#include "asset_manager.h"
#include "display_size.h"
#include "scene_transition.h"

#include <algorithm>
#include <array>
#include <random>

namespace wars {
namespace {

constexpr int width = platform::screen_width;
constexpr int height = platform::screen_height;
using scene_transition::blink_ms;
using scene_transition::character_ms;
constexpr auto white = 0xffffff;
constexpr auto gray = 0xa3a3a3;
constexpr auto amber = 0xffd475;
constexpr auto prompt = "Ram Altman is trying to steal your warez! What will you do?";
constexpr auto unavailable = "You don't have any 0day Exploits!";
constexpr std::array officer_images{
    "01-alley-dumpster.png", "02-loading-dock.png", "03-parking-garage.png",
    "04-conference-backstage.png", "05-repair-shop.png",
};
const std::filesystem::path encounters = "images/encounters";
constexpr std::array stash_images{
    "01-failed-startup.png", "02-unmarked-container.png", "03-cancelled-conference.png",
};
constexpr std::array ai_wars_images{
    "01-exchange-in-chaos.png", "02-digital-masters.png", "03-worthless-silicon.png",
    "04-chip-rush.png", "05-worldwide-shock.png",
};
constexpr auto ai_wars_text =
    "The battle for AI supremacy is upon us! Corporations (or their digital masters...) are waging an all-out war "
    "for control of the world's AI resources. Markets are in absolute chaos, fortunes will be made, and entire "
    "empires will be reduced to worthless silicon! The AI Wars have begun!";
constexpr std::uint32_t stash_pause_ms = 300;
constexpr std::uint32_t family_pause_ms = 750;
constexpr std::uint32_t ai_wars_pause_ms = 3000;
constexpr std::uint32_t failed_escape_fade_ms = 1000;
constexpr std::uint32_t failed_escape_hold_ms = 1500;

// Find an image for a saved story by good ID and story text even if the current ordering differs.
std::string family_story_image(const std::filesystem::path& economy_file, const Good& good,
                               const FamilyStory& story) {
    try {
        const auto economy = load_economy(economy_file);
        const auto commodity = std::find_if(economy.goods.begin(), economy.goods.end(),
            [&](const Good& candidate) { return candidate.id == good.id; });
        if (commodity != economy.goods.end()) {
            const auto match = std::find_if(commodity->family_stories.begin(), commodity->family_stories.end(),
                [&](const FamilyStory& candidate) { return candidate.text == story.text; });
            if (match != commodity->family_stories.end()) return match->image;
        }
    } catch (const std::exception&) {
        // A missing or invalid economy file does not prevent the saved run from continuing.
    }
    return {};
}

} // namespace

// Allocate a full-screen canvas buffer and decode the artwork or fill an optional placeholder.
GameUi::DrawBuffer GameUi::load_encounter_image(const std::filesystem::path& path, bool use_placeholder) {
    DrawBuffer image(lv_draw_buf_create(width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO),
                     lv_draw_buf_destroy);
    if (image && !app::read_screen_png(assets_.resolve(path), *image)) {
        if (!use_placeholder) {
            image.reset();
            return image;
        }
        // Keep the dialogue and transitions playable when scene artwork is absent.
        for (int y = 0; y < height; ++y) {
            auto* row = image->data + y * image->header.stride;
            for (int x = 0; x < width; ++x, row += 4) {
                row[0] = 0x22;
                row[1] = 0x19;
                row[2] = 0x10;
                row[3] = 0xff;
            }
        }
        lv_draw_buf_flush_cache(image.get(), nullptr);
    }
    return image;
}

// Append the found lots to the story and choose a stash image to display over the market.
void GameUi::draw_stash() {
    const auto& session = *profile_.active;
    auto line = encounter_story(session);
    for (const auto& cargo : session.run.encounter.cargo) {
        line += "\n+" + std::to_string(cargo.lots) + " x " + session.economy.goods[cargo.good].name;
    }

    std::mt19937 random(std::random_device{}());
    const auto image = std::uniform_int_distribution<std::size_t>(0, stash_images.size() - 1)(random);
    encounter_image_ = load_encounter_image(encounters / "abandoned-stash" / stash_images[image]);
    draw_market();
    draw_story(std::move(line));
}

// Try saved, current, and bundled artwork before falling back to the generic report.
void GameUi::draw_family() {
    const auto& session = *profile_.active;
    const auto& encounter = session.run.encounter;
    const auto& good = session.economy.goods[encounter.cargo.front().good];
    const auto& story = good.family_stories[encounter.story];
    // Try a named family image so the caller can check whether a fallback lookup is needed.
    const auto load_artwork = [&](const std::string& image) {
        if (!image.empty()) encounter_image_ = load_encounter_image(encounters / "family-events" / image, false);
    };
    load_artwork(story.image);
    if (!encounter_image_) {
        // Older saves can omit the image, and their story order can differ.
        load_artwork(family_story_image(economy_file_, good, story));
    }
    if (!encounter_image_) {
        auto bundled_economy = assets_.resolve("economy.json");
        if (bundled_economy.empty()) bundled_economy = APP_SOURCE_ECONOMY_FILE;
        load_artwork(family_story_image(bundled_economy, good, story));
    }
    if (!encounter_image_) {
        page_ = Page::Encounter;
        draw_encounter();
        return;
    }

    draw_market();
    auto* layer = box(0, 0, width, height, 0x000000);
    lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, LV_PART_MAIN);
    draw_story(encounter_story(session), layer);
}

// Use the run seed to keep the same random artwork when the headline is resumed or recalled.
void GameUi::draw_ai_wars_news() {
    std::mt19937_64 random(profile_.active->run.seed);
    const auto image = std::uniform_int_distribution<std::size_t>(0, ai_wars_images.size() - 1)(random);
    encounter_image_ = load_encounter_image(std::filesystem::path("images/news/day-25") / ai_wars_images[image]);
    draw_market();
    draw_story(ai_wars_text);
}

// Size the panel for the full story and start with the artwork off-screen for new arrivals.
void GameUi::draw_story(std::string line, lv_obj_t* parent) {
    auto& scene = story_.emplace();
    scene.layer = parent;
    scene.line = std::move(line);
    const bool news = page_ == Page::News;
    scene.ready = news && !arrival_news_;
    if (encounter_image_) {
        scene.canvas = draw_scene_image(*encounter_image_, scene.ready ? 0 : width, parent);
        clickable(scene.canvas, LV_KEY_ENTER);
    }
    constexpr int inset = 8;
    const int line_height = lv_font_get_line_height(market_font);
    const int prompt_height = news ? line_height + 4 : 0;
    lv_point_t size{};
    lv_text_get_size(&size, (scene.line + (news ? "" : "_")).c_str(), market_font,
                     0, 0, width - 2 * inset, LV_TEXT_FLAG_NONE);
    const int panel_height = std::max(height / 2, size.y + 2 * inset + prompt_height);
    scene.panel = box(0, height - panel_height, width, panel_height, 0x000000, parent);
    lv_obj_set_style_bg_opa(scene.panel, LV_OPA_80, LV_PART_MAIN);
    if (!scene.ready) lv_obj_add_flag(scene.panel, LV_OBJ_FLAG_HIDDEN);
    clickable(scene.panel, LV_KEY_ENTER);
    scene.dialogue = label(scene.ready ? scene.line : "", inset, inset, width - 2 * inset, market_font, white,
                           LV_TEXT_ALIGN_LEFT, true, scene.panel);
    if (news) {
        scene.prompt = label("Press any key", inset, panel_height - inset - line_height,
                             width - 2 * inset, market_font, white, LV_TEXT_ALIGN_CENTER, false, scene.panel);
        if (!scene.ready) lv_obj_add_flag(scene.prompt, LV_OBJ_FLAG_HIDDEN);
        clickable(scene.prompt, LV_KEY_ENTER);
    }
    scene.started = lv_tick_get();
}

// Slide the art in, hold it in place, then type the story and signal when it is ready.
void GameUi::update_story() {
    auto& scene = *story_;
    if (scene.prompt && scene.ready) return;
    if (scene.fade_started) {
        const auto elapsed = lv_tick_elaps(*scene.fade_started);
        if (elapsed < scene_transition::flight_fade_ms) {
            scene_transition::pixel_fade(scene.canvas, *encounter_image_, elapsed, scene.fade_position,
                                         scene_transition::flight_fade_ms);
        } else {
            show_arrival();
            render();
        }
        return;
    }

    auto elapsed = lv_tick_elaps(scene.started);
    if (scene.canvas) lv_obj_set_x(scene.canvas, scene_transition::slide_x(elapsed));
    auto first_character_at = scene_transition::slide_ms + stash_pause_ms + character_ms;
    if (scene.layer || scene.prompt) {
        if (elapsed < scene_transition::slide_ms) return;
        if (!scene.landed) scene.landed = lv_tick_get();
        elapsed = lv_tick_elaps(*scene.landed);
        first_character_at = scene.prompt ? ai_wars_pause_ms : family_pause_ms + character_ms;
    }
    if (elapsed < first_character_at) return;

    lv_obj_remove_flag(scene.panel, LV_OBJ_FLAG_HIDDEN);
    const auto typed_for = elapsed - first_character_at + character_ms;
    // Advance by UTF-8 code point so the animated label always receives complete characters.
    const auto is_continuation = [](char byte) { return (static_cast<unsigned char>(byte) & 0xc0) == 0x80; };
    std::size_t bytes = 0;
    std::uint32_t visible = 0;
    while (visible < typed_for / character_ms && bytes < scene.line.size()) {
        ++visible;
        ++bytes;
        while (bytes < scene.line.size() && is_continuation(scene.line[bytes])) ++bytes;
    }
    scene.ready = bytes == scene.line.size();
    if (scene.ready && scene.prompt) lv_obj_remove_flag(scene.prompt, LV_OBJ_FLAG_HIDDEN);
    const bool cursor = scene.ready && !scene.prompt && ((typed_for - visible * character_ms) / blink_ms) % 2 == 0;
    const auto text = scene.line.substr(0, bytes) + (cursor ? "_" : "");
    if (text != lv_label_get_text(scene.dialogue)) lv_label_set_text(scene.dialogue, text.c_str());
}

// Reveal the typing headline on a key press, then acknowledge it on the next press.
void GameUi::continue_story() {
    auto& scene = *story_;
    if (scene.fade_started) return;
    if (!scene.ready) {
        if (scene.prompt && !lv_obj_has_flag(scene.panel, LV_OBJ_FLAG_HIDDEN)) {
            scene.ready = true;
            lv_label_set_text(scene.dialogue, scene.line.c_str());
            lv_obj_remove_flag(scene.prompt, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (scene.prompt) {
        leave_arrival();
        if (page_ == Page::News) {
            lv_label_set_text(scene.prompt, message_.c_str());
        } else {
            render();
        }
        return;
    }

    profile_.active->run.encounter = {};
    if (scene.layer) {
        // Flatten only the artwork and dialogue, so the same fade tiles erase both.
        DrawBuffer frame(lv_snapshot_take(scene.layer, LV_COLOR_FORMAT_ARGB8888), lv_draw_buf_destroy);
        if (frame) {
            lv_obj_delete(scene.layer);
            scene.layer = nullptr;
            encounter_image_ = std::move(frame);
            scene.canvas = draw_scene_image(*encounter_image_, 0);
            scene.fade_started = lv_tick_get();
            return;
        }
    }
    show_arrival();
    render();
}

// Choose matching officer artwork for the story and start the slide or fall back to dialogue.
void GameUi::draw_officer() {
    auto& scene = officer_.emplace();
    const auto& run = profile_.active->run;
    scene.filename = officer_images[run.encounter.story % officer_images.size()];
    scene.image = load_encounter_image(encounters / "officer-ram-altman" / scene.filename);
    if (!scene.image) {
        officer_line(OfficerPhase::Blurb, encounter_story(*profile_.active));
        return;
    }
    draw_market();
    scene.canvas = draw_scene_image(*scene.image, width);
    clickable(scene.canvas, LV_KEY_ENTER);
    scene.started = lv_tick_get();
}

// Replace the current panel and clear the dialogue pointer owned by the old panel.
void GameUi::officer_panel(int panel_height) {
    auto& scene = *officer_;
    if (scene.panel) lv_obj_delete(scene.panel);
    scene.panel = box(0, height - panel_height, width, panel_height, 0x000000);
    lv_obj_set_style_bg_opa(scene.panel, LV_OPA_80, LV_PART_MAIN);
    clickable(scene.panel, LV_KEY_ENTER);
    scene.dialogue = nullptr;
}

// Reset typewriter state and lay out the dialogue for the current phase and outcome.
void GameUi::officer_line(OfficerPhase phase, std::string line) {
    const bool escaped = phase == OfficerPhase::Result && profile_.active->run.encounter.type == EncounterType::Escape;
    officer_panel(escaped ? 88 : 74);
    auto& scene = *officer_;
    scene.phase = phase;
    scene.line = std::move(line);
    scene.started = lv_tick_get();
    scene.visible = 0;
    scene.cursor_visible = false;
    if (escaped) {
        label("YOU ESCAPED", 8, 3, width - 16, market_font, amber,
              LV_TEXT_ALIGN_LEFT, false, scene.panel);
        scene.dialogue = label("", 8, 21, width - 16, market_font, white,
                               LV_TEXT_ALIGN_LEFT, true, scene.panel);
        label("Enter Continue", 8, 70, width - 16, market_font, white,
              LV_TEXT_ALIGN_LEFT, false, scene.panel);
    } else {
        scene.dialogue = label("", 8, 8, width - 16, &lv_font_montserrat_14, white,
                               LV_TEXT_ALIGN_LEFT, true, scene.panel);
    }
}

// Show the action prompt and dim the attack option if the player has no usable exploit.
void GameUi::officer_choices() {
    officer_panel();
    auto& scene = *officer_;
    scene.phase = OfficerPhase::Choice;
    const auto& run = profile_.active->run;
    const bool armed = run.unused_exploits > 0 || run.exposed_exploits > 0;
    const auto* font = &lv_font_montserrat_14;
    lv_point_t prompt_size{};
    lv_text_get_size(&prompt_size, prompt, font, 0, 0, width - 16, LV_TEXT_FLAG_NONE);
    label(prompt, 8, 4, width - 16, &lv_font_montserrat_14, white,
          LV_TEXT_ALIGN_LEFT, true, scene.panel);
    // Bind each choice label to the key used by the officer scene.
    const auto option = [&](int y, const char* text, std::uint32_t color, std::uint32_t key) {
        auto* choice = label(text, 8, y, width - 16, font, color,
                             LV_TEXT_ALIGN_LEFT, false, scene.panel);
        clickable(choice, key);
    };
    const int first_option_y = 5 + prompt_size.y;
    option(first_option_y, "(a)ttack with 0day Exploit", armed ? white : gray, 'a');
    option(first_option_y + lv_font_get_line_height(font) + 1, "(r)un away", white, 'r');
}

// Fit the lost goods beneath the heading, continuing on another page only when needed.
void GameUi::officer_losses() {
    auto& scene = *officer_;
    const auto& session = *profile_.active;
    const auto& cargo = session.run.encounter.cargo;
    std::string line = "Ram Altman relieved you of:\n";
    const auto first = scene.cargo_next;
    lv_point_t size{};
    const int prompt_height = lv_font_get_line_height(market_font) + 4;
    const int available = height - 16 - prompt_height;
    while (scene.cargo_next < cargo.size()) {
        const auto& loss = cargo[scene.cargo_next];
        const auto next = line + "\n" + std::to_string(loss.lots) + " x " + session.economy.goods[loss.good].name;
        lv_text_get_size(&size, next.c_str(), market_font, 0, 0, width - 16, LV_TEXT_FLAG_NONE);
        if (size.y > available && scene.cargo_next > first) break;
        line = next;
        ++scene.cargo_next;
    }
    if (cargo.empty()) line += "\nNothing.";
    const bool more = scene.cargo_next < cargo.size();
    lv_text_get_size(&size, line.c_str(), market_font, 0, 0, width - 16, LV_TEXT_FLAG_NONE);
    const int panel_height = std::max(74, size.y + 16 + (more ? prompt_height : 0));
    officer_panel(panel_height);
    scene.phase = OfficerPhase::Losses;
    scene.dialogue = label(line, 8, 8, width - 16, market_font, white,
                           LV_TEXT_ALIGN_LEFT, true, scene.panel);
    if (more) {
        label("Press any key for more", 8, panel_height - prompt_height - 4, width - 16,
              market_font, white, LV_TEXT_ALIGN_CENTER, false, scene.panel);
    }
}

// Clear the current dialogue so its panel is gone when the next frame starts showing.
void GameUi::begin_officer_fade(OfficerPhase phase) {
    auto& scene = *officer_;
    lv_obj_delete(scene.panel);
    scene.panel = nullptr;
    scene.dialogue = nullptr;
    scene.phase = phase;
    scene.started = lv_tick_get();
    scene.fade_position = 0;
}

// Render the new page before releasing any buffers that the old scene's canvases borrowed.
void GameUi::leave_officer(Page page) {
    page_ = page;
    render();
    officer_.reset();
}

// Advance the dialogue or save and present the outcome of the player's choice.
void GameUi::handle_officer(std::uint32_t key) {
    auto& scene = *officer_;
    switch (scene.phase) {
        case OfficerPhase::Slide:
        case OfficerPhase::RevealResult:
        case OfficerPhase::HoldResult:
        case OfficerPhase::RevealMarket:
            return;
        case OfficerPhase::Blurb:
        case OfficerPhase::Notice:
            if (lv_tick_elaps(scene.started) >= scene.line.size() * character_ms) officer_choices();
            return;
        case OfficerPhase::Result: {
            if (profile_.active->run.encounter.type == EncounterType::Escape) {
                if (key != LV_KEY_ENTER && key != ' ' && key != LV_KEY_ESC) return;
            } else if (lv_tick_elaps(scene.started) < scene.line.size() * character_ms) {
                return;
            }
            if (profile_.active->run.encounter.type == EncounterType::Confiscation) {
                officer_losses();
                return;
            }
            profile_.active->run.encounter = {};
            if (!scene.canvas) {
                leave_officer(Page::Market);
                return;
            }
            // Redraw the market with the resolved equipment counts under the final frame.
            begin_officer_fade(OfficerPhase::RevealMarket);
            lv_obj_clean(screen_);
            draw_market();
            scene.canvas = draw_scene_image(*scene.image, 0);
            return;
        }
        case OfficerPhase::Losses:
            if (scene.cargo_next < profile_.active->run.encounter.cargo.size()) {
                officer_losses();
            } else {
                profile_.active->run.encounter = {};
                leave_officer(Page::Market);
            }
            return;
        case OfficerPhase::Choice:
            break;
    }

    if (key == LV_KEY_ESC) {
        leave_officer(Page::Home);
        return;
    }
    if (key != 'a' && key != 'r') return;

    Profile next = profile_;
    if (!resolve_officer(*next.active, key == 'a')) {
        officer_line(OfficerPhase::Notice, unavailable);
        return;
    }
    if (!commit(std::move(next))) {
        officer_line(OfficerPhase::Notice, message_);
        return;
    }
    const auto outcome = profile_.active->run.encounter.type;
    const auto* directory = outcome == EncounterType::Confiscation ? "officer-ram-altman-confiscation"
        : outcome == EncounterType::Escape ? "officer-ram-altman-escape" : "officer-ram-altman-success";
    scene.result_image = load_encounter_image(encounters / directory / scene.filename);
    if (scene.result_image && scene.canvas) {
        scene.result_canvas = draw_scene_image(*scene.result_image, 0);
        // Put the result canvas beneath the original image so clearing tiles reveals the matching art.
        lv_obj_move_to_index(scene.result_canvas, lv_obj_get_index(scene.canvas));
        clickable(scene.result_canvas, LV_KEY_ENTER);
        begin_officer_fade(OfficerPhase::RevealResult);
    } else {
        officer_line(OfficerPhase::Result, encounter_story(*profile_.active));
    }
}

// Advance slides and fades or update the visible dialogue for the active scene phase.
void GameUi::update_officer() {
    auto& scene = *officer_;
    const auto elapsed = lv_tick_elaps(scene.started);
    switch (scene.phase) {
        case OfficerPhase::Slide:
            lv_obj_set_x(scene.canvas, scene_transition::slide_x(elapsed));
            if (elapsed >= scene_transition::slide_ms) {
                officer_line(OfficerPhase::Blurb, encounter_story(*profile_.active));
            }
            return;
        case OfficerPhase::RevealResult:
        case OfficerPhase::RevealMarket: {
            const bool failed_escape = scene.phase == OfficerPhase::RevealResult
                && profile_.active->run.encounter.type == EncounterType::Confiscation;
            const auto duration = failed_escape ? failed_escape_fade_ms : scene_transition::fade_ms;
            const auto pattern = failed_escape ? scene_transition::FadePattern::Random
                                               : scene_transition::FadePattern::LeftToRight;
            if (elapsed < duration) {
                scene_transition::pixel_fade(scene.canvas, *scene.image, elapsed, scene.fade_position,
                                             duration, pattern);
            } else if (scene.phase == OfficerPhase::RevealResult) {
                lv_obj_delete(scene.canvas);
                scene.canvas = scene.result_canvas;
                scene.result_canvas = nullptr;
                scene.image = std::move(scene.result_image);
                if (failed_escape) {
                    scene.phase = OfficerPhase::HoldResult;
                    scene.started = lv_tick_get();
                } else {
                    officer_line(OfficerPhase::Result, encounter_story(*profile_.active));
                }
            } else {
                lv_obj_delete(scene.canvas);
                page_ = Page::Market;
                officer_.reset();
            }
            return;
        }
        case OfficerPhase::HoldResult:
            if (elapsed >= failed_escape_hold_ms) {
                officer_line(OfficerPhase::Result, encounter_story(*profile_.active));
            }
            return;
        case OfficerPhase::Choice:
        case OfficerPhase::Losses:
            return;
        case OfficerPhase::Blurb:
        case OfficerPhase::Notice:
        case OfficerPhase::Result:
            break;
    }

    const auto length = scene.line.size();
    const auto visible = std::min(length, static_cast<std::size_t>(elapsed / character_ms));
    const bool cursor = visible == length && ((elapsed - length * character_ms) / blink_ms) % 2 == 0;
    if (visible != scene.visible || cursor != scene.cursor_visible) {
        scene.visible = visible;
        scene.cursor_visible = cursor;
        const auto text = scene.line.substr(0, visible) + (cursor ? "_" : "");
        lv_label_set_text(scene.dialogue, text.c_str());
    }
}

} // namespace wars
