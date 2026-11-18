// Builds the game's LVGL screens, shared widgets, market panes, and Black Hat overlay.

#include "game_ui.h"
#include "asset_manager.h"
#include "display_size.h"
#include "scene_transition.h"

#include <algorithm>
#include <array>

namespace wars {
namespace {

constexpr int width = platform::screen_width;
constexpr int height = platform::screen_height;
constexpr std::uint32_t bg = 0x101922;
constexpr std::uint32_t panel = 0x192a38;
constexpr std::uint32_t focus = 0x2b4954;
constexpr std::uint32_t line = 0x304655;
constexpr std::uint32_t text = 0xe4f2f1;
constexpr std::uint32_t muted = 0xa9c1cb;
constexpr std::uint32_t amber = 0xffd475;
constexpr std::uint32_t red = 0xffa098;
constexpr auto market_bg = 0x15271e;
constexpr auto market_alt = 0x1e3529;
constexpr auto pane_focus = 0x345642;
constexpr auto pane_rule = 0x547459;
constexpr auto pane_text = 0xf0f9e8;
constexpr auto pane_muted = 0xbdcfba;
constexpr auto pane_accent = 0xb6f484;
constexpr std::uint32_t enter = LV_KEY_ENTER;
constexpr std::uint32_t escape = LV_KEY_ESC;
constexpr std::array<std::uint32_t, 3> podium{0xffd475, 0xcbd9ff, 0xf1ac85};
constexpr int jacket_width = 196;
constexpr int jacket_tab_width = 25;
constexpr int jacket_open_x = width - jacket_width;
constexpr int jacket_closed_x = width - jacket_tab_width;
constexpr int jacket_slide_ms = 500;
constexpr int market_name_x = 17;
constexpr int market_price_inset = 4;
constexpr int market_footer_y = 134;
constexpr int action_bar_y = 146;
constexpr int black_hat_panel_y = 59;
constexpr std::uint32_t black_hat_slide_ms = 750;
constexpr std::uint32_t black_hat_pause_ms = 750;
constexpr std::uint32_t black_hat_panel_ms = 500;

struct HelpPage {
    const char* heading;
    const char* body;
};

constexpr std::array help_pages{
    HelpPage{"Buy low. Jet. Sell high.",
             "Trade hardware and data across cities. Each flight costs a day. Finish with as much cash as you can.\n"
             "Watch the news for price swings. Sell above your jacket's Avg $."},
    HelpPage{"The market",
             "Up/Down or F/X moves. 1-5 picks a row.\n"
             "Left/Right or Z/C changes pages.\n"
             "B buys. S sells. Enter buys in Market or sells in Jacket.\n"
             "Space or I toggles your jacket.\n"
             "Tab changes sort. N recalls the news."},
    HelpPage{"Trade and fly",
             "Type lots or adjust with Up/Down (F/X).\n"
             "M sets Max. Backspace removes a digit.\n"
             "J opens Jet. Arrows or F/X/Z/C move.\n"
             "1-8 flies direct. Enter confirms a trade or flight.\n"
             "Enter, Space or Esc skips the flight."},
    HelpPage{"Arrival events",
             "Any key dismisses news or advances finished dialogue. Enter or Space closes reports. Left/Right or Z/C pages cargo.\n"
             "At the black hat, 1 buys a 0day, 2 buys a jacket upgrade and P passes.\n"
             "B buys a courier jacket. P or Enter passes."},
    HelpPage{"Escape and settle",
             "A attacks Ram Altman with a 0day.\n"
             "R runs. Enter or Space closes the escape screen.\n"
             "On the last day, sell your cargo before the discounted buyout. Press E to review your score. Press any key to settle."},
    HelpPage{"Menu and scores",
             "Arrows or F/X move. Enter selects.\n"
             "C continues. N starts. H opens scores.\n"
             "Type your name. Backspace edits, Del clears. Enter confirms or replaces.\n"
             "Highscores uses Up/Down, F/X or digits (0 for rank 10). Enter opens the score's QR."},
    HelpPage{"Shortcuts",
             "Press any key at the title screen.\n"
             "Tap Esc to go back. Hold Esc to quit.\n"
             "Print Screen saves a screenshot.\n"
             "In the simulator, click buttons and rows.\n"
             "Open this guide with G from the menu.\n"
             "Left/Right or Z/C turns guide pages."},
};

// Format a full dollar amount for display.
std::string cash(Money value) {
    return "$" + format_number(value);
}

// Shorten values of a million or more so they fit narrow columns, keeping one decimal digit.
std::string compact(Money value) {
    if (value < 1'000'000) return cash(value);
    constexpr std::array<Money, 4> units{1'000'000LL, 1'000'000'000LL,
                                        1'000'000'000'000LL, 1'000'000'000'000'000LL};
    constexpr std::array<char, 4> suffix{'M', 'B', 'T', 'Q'};
    for (int i = static_cast<int>(units.size()) - 1; i >= 0; --i) {
        if (value >= units[i]) {
            const auto tenths = value / (units[i] / 10);
            return "$" + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + suffix[i];
        }
    }
    return cash(value);
}

// Convert a price multiplier to the signed percentage change shown in news.
std::string change(int percent) {
    const int difference = percent - 100;
    return (difference >= 0 ? "+" : "") + std::to_string(difference) + "%";
}

// Measure a footer caption in pixels and add room for horizontal padding.
int button_width(const char* caption) {
    lv_point_t size{};
    lv_text_get_size(&size, caption, &lv_font_montserrat_10, 0, 0, 312, LV_TEXT_FLAG_NONE);
    return size.x + 10;
}

// Animate a widget between horizontal positions using LVGL's ease-out path.
void slide(lv_obj_t* object, int start, int end, std::uint32_t duration) {
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, start, end);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_exec_cb(&animation, [](void* item, int32_t x) {
        lv_obj_set_x(static_cast<lv_obj_t*>(item), x);
    });
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

} // namespace

// Delete the old widgets before releasing their buffers and drawing the page selected by input.
void GameUi::render() {
    lv_obj_clean(screen_);
    if (home_ && home_->veil) {
        home_->veil_canvas = nullptr;
        if (page_ != Page::Home) home_->veil.reset();
    }
    encounter_image_.reset();
    story_.reset();
    black_hat_.reset();
    if (page_ != Page::Ending) ending_.reset();
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, LV_PART_MAIN);
    if (page_ != Page::Market) jacket_slide_.reset();
    switch (page_) {
        case Page::Intro: draw_intro(); break;
        case Page::Home: draw_home(); break;
        case Page::Help: draw_help(); break;
        case Page::Name:
        case Page::Replace: draw_name(); break;
        case Page::Market: draw_market(); break;
        case Page::Trade: draw_trade(); break;
        case Page::Jet: draw_jet(); break;
        case Page::News: draw_news(); break;
        case Page::Offer: draw_offer(); break;
        case Page::Encounter: draw_encounter(); break;
        case Page::Officer: draw_officer(); break;
        case Page::Stash: draw_stash(); break;
        case Page::Family: draw_family(); break;
        case Page::Close: draw_close(); break;
        case Page::Ending: draw_ending(); break;
        case Page::Scores: draw_scores(); break;
        case Page::Share: draw_share(); break;
    }
    animate_scene_ = false;
}

// Apply common label styling and choose wrapping or ellipsis within the available width.
lv_obj_t* GameUi::label(const std::string& value, int x, int y, int available,
                        const lv_font_t* font, std::uint32_t color, lv_text_align_t align,
                        bool wrap, lv_obj_t* parent) {
    auto* object = lv_label_create(parent ? parent : screen_);
    lv_obj_remove_style_all(object);
    lv_label_set_long_mode(object, wrap ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
    lv_label_set_text(object, value.c_str());
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, available);
    if (!wrap) lv_obj_set_height(object, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(object, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(object, align, LV_PART_MAIN);
    return object;
}

// Create a fixed-size background or container with the default LVGL styling removed.
lv_obj_t* GameUi::box(int x, int y, int w, int h, std::uint32_t color, lv_obj_t* parent) {
    auto* object = lv_obj_create(parent ? parent : screen_);
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    return object;
}

// Store an action key on an LVGL widget and route its click to the shared key callback.
void GameUi::clickable(lv_obj_t* object, std::uint32_t key) {
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(object, reinterpret_cast<void*>(static_cast<std::uintptr_t>(key)));
    lv_obj_add_event_cb(object, click, LV_EVENT_CLICKED, this);
}

// Style a button for its focus and enabled states and register clicks only when enabled.
void GameUi::button(int x, int y, int w, int h, const std::string& caption,
                    std::uint32_t key, bool selected, bool enabled, lv_obj_t* parent,
                    const lv_font_t* font, bool market_colors) {
    const auto base = market_colors ? market_alt : panel;
    const auto selected_bg = market_colors ? pane_focus : focus;
    const auto foreground = market_colors ? pane_text : text;
    const auto disabled = market_colors ? pane_muted : muted;
    const auto accent = market_colors ? pane_accent : amber;
    auto* tile = box(x, y, w, h, selected ? selected_bg : base, parent);
    if (enabled) clickable(tile, key);
    if (selected) box(0, 0, 2, h, accent, tile);
    if (!font) font = h >= 18 ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
    label(caption, 3, (h - lv_font_get_line_height(font)) / 2, w - 6,
          font, !enabled ? disabled : selected ? accent : foreground, LV_TEXT_ALIGN_CENTER, false, tile);
}

// Draw the standard title strip for pages without the active-run HUD.
void GameUi::title(const std::string& value) {
    box(0, 0, width, 24, panel);
    label(value, 8, 4, 304, &lv_font_montserrat_14, amber);
}

// Put status messages above actions and divide spare button width among their captions.
void GameUi::footer(const std::string& value, std::initializer_list<Action> actions) {
    box(0, 142, width, 28, panel);
    box(0, 142, width, 1, line);
    label(message_.empty() ? value : message_, 8, 144, 304, &lv_font_montserrat_10,
          message_ == "Save failed. Try again." ? red : muted);
    if (!actions.size()) return;
    int spare = 312;
    for (const auto& action : actions) spare -= button_width(action.label);
    const int extra = spare / static_cast<int>(actions.size());
    int x = 4;
    for (const auto& action : actions) {
        const int step = spare >= 0 ? button_width(action.label) + extra
                                    : 312 / static_cast<int>(actions.size());
        button(x, 155, step, 15, action.label, action.key);
        x += step;
    }
}

// Fit a nonempty list of actions into evenly spaced buttons across the bottom.
void GameUi::draw_action_bar(std::initializer_list<Action> actions, bool market_colors) {
    const int button_width = (width - 16) / static_cast<int>(actions.size());
    int x = 8;
    box(0, action_bar_y, width, 1, market_colors ? pane_rule : line);
    for (const auto& action : actions) {
        button(x, action_bar_y + 3, button_width - 2, 19, action.label, action.key,
               false, true, nullptr, market_font, market_colors);
        x += button_width;
    }
}

// Summarize the city, run day, cash, and cargo capacity in the top strip.
void GameUi::hud(const lv_font_t* font, bool market_colors) {
    const auto& session = *profile_.active;
    const auto& run = session.run;
    const auto foreground = market_colors ? pane_text : text;
    box(0, 0, width, 24, market_colors ? market_alt : panel);
    if (market_colors) box(0, 23, width, 1, pane_rule);
    const int y = (24 - lv_font_get_line_height(font)) / 2;
    label(session.economy.cities[run.city].name, 8, y, 107, font, foreground);
    label("D" + std::to_string(run.day) + "/" + std::to_string(session.economy.days),
          115, y, 55, font, market_colors ? pane_muted : muted);
    label(compact(run.cash), 170, y, 78, font, amber, LV_TEXT_ALIGN_RIGHT);
    label(std::to_string(used_slots(session)) + "/" + std::to_string(run.capacity),
          252, y, 60, font, foreground, LV_TEXT_ALIGN_RIGHT);
}

// Place the menu on the selected artwork or use the plain layout if no image loaded.
void GameUi::draw_home() {
    constexpr auto* hint_font = &lv_font_montserrat_10;
    constexpr auto credit = "a game by randombits0110";
    const int hint_height = lv_font_get_line_height(hint_font);
    const char* sound_action = profile_.sound_enabled ? "S  Sound off" : "S  Sound on";
    if (!home_) {
        label("AI WARS", 12, 7, 296, &lv_font_montserrat_28, amber, LV_TEXT_ALIGN_CENTER);
        label("Buy low. Fly. Sell high.", 12, 39, 296, &lv_font_montserrat_12, muted, LV_TEXT_ALIGN_CENTER);
        if (profile_.active) {
            button(50, 54, 220, 16, "C  Continue as " + profile_.active->run.player,
                   home_keys[0], home_row_ == 0, true, nullptr, &lv_font_montserrat_12);
        }
        button(50, 72, 220, 16, "N  New Game", home_keys[1], home_row_ == 1);
        button(50, 89, 220, 16, "H  Highscores", home_keys[2], home_row_ == 2);
        button(50, 106, 220, 16, "G  How to Play", home_keys[3], home_row_ == 3);
        button(50, 123, 220, 16, sound_action, home_keys[4], home_row_ == 4);
        footer("Hold Esc to exit", {});
        label(credit, 8, 144 + hint_height, 304, hint_font, 0xffffff);
        return;
    }

    draw_scene_image(*home_->image, 0);
    constexpr int w = 148;
    constexpr int row_height = 15;
    constexpr int row_step = row_height + 1;
    constexpr int continue_height = 26;
    const int continue_step = profile_.active ? continue_height + 1 : 0;
    const int hint_offset = continue_step + 4 * row_step;
    const int menu_height = hint_offset + 2 * hint_height;
    const int x = std::min(home_->menu_x, width - w);
    const int y = std::min(home_->menu_y, height - menu_height - 2);
    const int next_row = y + continue_step;
    const int hint_y = y + hint_offset;
    // Cover the sample menu baked into the artwork and back both footer labels.
    box(x, y, w, menu_height, bg);
    if (profile_.active) {
        const bool selected = home_row_ == 0;
        auto* row = box(x, y, w, continue_height, selected ? focus : panel);
        clickable(row, home_keys[0]);
        if (selected) box(0, 0, 2, continue_height, amber, row);
        label("C  Continue", 5, 0, w - 10, &lv_font_montserrat_12, selected ? amber : text,
              LV_TEXT_ALIGN_CENTER, false, row);
        auto* name = label(profile_.active->run.player, 5, 14, w - 10, &lv_font_montserrat_10, muted,
                           LV_TEXT_ALIGN_CENTER, false, row);
        lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    const std::array captions{"N  New Game", "H  Highscores", "G  How to Play", sound_action};
    for (int row = 1; row < static_cast<int>(home_keys.size()); ++row) {
        button(x, next_row + (row - 1) * row_step, w, row_height, captions[row - 1],
               home_keys[row], home_row_ == row, true, nullptr, &lv_font_montserrat_12);
    }
    const auto hint = message_ == "Save failed. Try again." ? "Save failed" : message_;
    label(hint.empty() ? "Hold Esc to exit" : hint, x + 4, hint_y, w - 8,
          hint_font, message_.empty() ? muted : red, LV_TEXT_ALIGN_CENTER);
    label(credit, x + 4, hint_y + hint_height, w - 8, hint_font, 0xffffff, LV_TEXT_ALIGN_CENTER);
    if (home_->veil) home_->veil_canvas = draw_scene_image(*home_->veil, 0);
}

// Clamp a guide page step to the available pages.
void GameUi::move_help(int direction) {
    help_page_ = std::clamp(help_page_ + direction, 0, static_cast<int>(help_pages.size()) - 1);
}

// Show a guide page and enable navigation only toward available neighbors.
void GameUi::draw_help() {
    const auto& page = help_pages[help_page_];
    title("HOW TO PLAY");
    label(std::to_string(help_page_ + 1) + "/" + std::to_string(help_pages.size()), 248, 4, 64,
          market_font, muted, LV_TEXT_ALIGN_RIGHT);
    label(page.heading, 8, 28, 304, &lv_font_montserrat_16, amber);
    label(page.body, 8, 48, 304, market_font, text, LV_TEXT_ALIGN_LEFT, true);
    box(0, action_bar_y, width, 1, line);
    button(8, 149, 98, 19, "Z  Prev", 'z', false, help_page_ > 0, nullptr, market_font);
    button(111, 149, 98, 19, "Esc  Back", escape, false, true, nullptr, market_font);
    button(214, 149, 98, 19, "C  Next", 'c', false,
           help_page_ + 1 < static_cast<int>(help_pages.size()), nullptr, market_font);
}

// Show the editable player name or ask before replacing an existing run.
void GameUi::draw_name() {
    if (page_ == Page::Replace) {
        title("REPLACE ACTIVE RUN?");
        label("Start a new run as " + player_name(name_) + "?", 8, 45, 304,
              &lv_font_montserrat_16, text, LV_TEXT_ALIGN_LEFT, true);
        label("The current run will be replaced.", 8, 88, 304, &lv_font_montserrat_12, muted);
        footer("", {{"Enter Replace", enter}, {"Esc Back", escape}});
        return;
    }
    title("NEW GAME");
    label("Player name", 8, 36, 304, &lv_font_montserrat_14, text);
    box(8, 59, 304, 31, prefilled_name_ ? focus : panel);
    label(name_.empty() ? "_" : name_ + (prefilled_name_ ? "" : "_"),
          15, 64, 288, &lv_font_montserrat_18, amber);
    label("Up to 16 letters, digits, spaces, - or _", 8, 99, 304, &lv_font_montserrat_12, muted);
    label(prefilled_name_ ? "Type to replace. Backspace edits." : "Enter confirms your name.",
          8, 116, 304, &lv_font_montserrat_12, muted);
    footer("", {{"Enter Start", enter}, {"Clear", LV_KEY_DEL}, {"Esc Back", escape}});
}

// Start from the current animated position so reversing the slide feels continuous.
void GameUi::toggle_jacket() {
    jacket_slide_ = JacketSlide{jacket_position(), lv_tick_get()};
    jacket_open_ = !jacket_open_;
    jacket_focused_ = jacket_open_;
}

// Ease from the saved starting position to the open or closed stop.
int GameUi::jacket_position() const {
    const int target = jacket_open_ ? jacket_open_x : jacket_closed_x;
    if (!jacket_slide_) return target;
    const int elapsed = std::min(lv_tick_elaps(jacket_slide_->started), std::uint32_t{jacket_slide_ms});
    const int remaining = jacket_slide_ms - elapsed;
    return target + (jacket_slide_->from_x - target) * remaining * remaining
        / (jacket_slide_ms * jacket_slide_ms);
}

// Resize the market pane and product labels as the jacket tray moves.
void GameUi::update_jacket_slide() {
    const int x = jacket_position();
    lv_obj_set_x(jacket_tray_, x);
    lv_obj_set_width(market_pane_, x);
    for (const auto& name : market_names_) {
        if (name.label) lv_obj_set_width(name.label, x - name.reserved_width);
    }
    if (lv_tick_elaps(jacket_slide_->started) >= jacket_slide_ms) jacket_slide_.reset();
}

// Lay out both sorted panes with independent selections and five visible rows per page.
void GameUi::draw_market() {
    hud(&lv_font_montserrat_12, true);
    const auto& session = *profile_.active;
    constexpr int hud_height = 24;
    constexpr int pane_height = market_footer_y - hud_height;
    constexpr int header_height = 20;
    constexpr int row_height = (pane_height - header_height) / rows_per_page;
    constexpr int market_header_width = 30;
    constexpr int jacket_header_width = 43;
    constexpr int own_width = 35;
    constexpr int own_inset = 4;
    constexpr int jacket_price_inset = own_width + own_inset + 3;
    constexpr auto jacket_bg = 0x1b2f24;
    constexpr auto jacket_alt = 0x233d2e;
    const int row_text_y = (row_height - lv_font_get_line_height(market_font)) / 2;
    const int tray_x = jacket_position();

    market_pane_ = box(0, hud_height, tray_x, pane_height, market_bg);
    jacket_tray_ = box(tray_x, hud_height, jacket_width, pane_height, jacket_bg);
    market_names_.fill({});
    const int market_target_width = jacket_open_ ? jacket_open_x : jacket_closed_x;

    auto* tab = box(0, 0, jacket_tab_width, pane_height, jacket_focused_ ? pane_focus : jacket_alt, jacket_tray_);
    clickable(tab, ' ');
    box(jacket_tab_width - 1, 0, 1, pane_height, pane_rule, tab);
    auto* tab_text = label("J\nA\nC\nK\nE\nT", 0, 0, jacket_tab_width - 1, market_font,
                           pane_accent, LV_TEXT_ALIGN_CENTER, true, tab);
    lv_obj_center(tab_text);

    for (const bool jacket : {false, true}) {
        const auto items = listing(jacket);
        const int row = selected_row(items, jacket);
        const int page = row / rows_per_page;
        const int count = static_cast<int>(items.size());
        const int pages = std::max(1, (count + rows_per_page - 1) / rows_per_page);
        const bool focused = jacket_focused_ == jacket;
        auto* pane = jacket ? jacket_tray_ : market_pane_;
        const int x = jacket ? jacket_tab_width : 0;
        const int inset = jacket ? jacket_price_inset : market_price_inset;
        const int name_x = x + market_name_x;
        const auto base = jacket ? jacket_bg : market_bg;
        const auto alternate = jacket ? jacket_alt : market_alt;
        const auto header_color = focused ? pane_accent : pane_muted;
        auto* header = box(x, 0, 1, header_height - 1, focused ? pane_focus : alternate, pane);
        lv_obj_set_width(header, jacket ? jacket_width - x : lv_pct(100));
        const int header_left = x + 4;
        const int price_header_width = jacket ? jacket_header_width : market_header_width;
        const int heading_width = (jacket ? jacket_width : jacket_open_x) - inset - price_header_width - header_left;
        label(std::string(jacket ? "Jacket " : "Market ") + std::to_string(page + 1) + "/" + std::to_string(pages),
              header_left, 2, heading_width, market_font,
              header_color, LV_TEXT_ALIGN_LEFT, false, pane);
        auto* price_header = label(jacket ? "Avg $" : "Buy", 0, 0, price_header_width, market_font,
                                   header_color, LV_TEXT_ALIGN_RIGHT, false, pane);
        lv_obj_align(price_header, LV_ALIGN_TOP_RIGHT, -inset, 2);
        if (jacket) {
            label("Own", jacket_width - own_inset - own_width, 2, own_width, market_font,
                  header_color, LV_TEXT_ALIGN_RIGHT, false, pane);
        }
        auto* rule = box(x, header_height - 1, 1, 1, pane_rule, pane);
        lv_obj_set_width(rule, jacket ? jacket_width - x : lv_pct(100));
        if (items.empty()) {
            label("Jacket is empty", x + 6, 52, jacket_width - x - 12, market_font,
                  pane_muted, LV_TEXT_ALIGN_CENTER, false, pane);
        }

        for (int i = 0; i < rows_per_page && page * rows_per_page + i < count; ++i) {
            const int position = page * rows_per_page + i;
            const int good = items[position];
            const int y = header_height + i * row_height;
            const bool selected = focused && position == row;
            auto* tile = box(x, y, 1, row_height, selected ? pane_focus : i % 2 ? alternate : base, pane);
            lv_obj_set_width(tile, jacket ? jacket_width - x : lv_pct(100));
            clickable(tile, pane_row_key + (jacket ? rows_per_page : 0) + i);
            if (selected) box(x, y, 2, row_height, pane_accent, pane);
            label(std::to_string(i + 1), x + 6, y + row_text_y, 11, market_font,
                  selected ? pane_accent : pane_muted, LV_TEXT_ALIGN_LEFT, false, pane);
            const auto& holding = session.run.holdings[good];
            const auto price = compact(jacket ? average_cost(holding) : session.run.market[good].price);
            lv_point_t price_size{};
            lv_text_get_size(&price_size, price.c_str(), market_font, 0, 0, width, LV_TEXT_FLAG_NONE);
            const int reserved_width = name_x + price_size.x + inset + 4;
            const int name_width = (jacket ? jacket_width : tray_x) - reserved_width;
            const auto& commodity = session.economy.goods[good];
            // Choose full or abbreviated names using the final width to keep names stable during the slide.
            const int settled_width = (jacket ? jacket_width : market_target_width) - reserved_width;
            lv_point_t full_size{};
            lv_text_get_size(&full_size, commodity.name.c_str(), market_font, 0, 0, width, LV_TEXT_FLAG_NONE);
            auto* name = label(full_size.x <= settled_width ? commodity.name : commodity.short_name,
                               name_x, y + row_text_y, name_width, market_font, pane_text,
                               LV_TEXT_ALIGN_LEFT, false, pane);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_SCROLL);
            if (!jacket) market_names_[i] = {name, reserved_width};
            auto* price_label = label(price, 0, 0, price_size.x, market_font,
                                      selected ? pane_accent : pane_text, LV_TEXT_ALIGN_RIGHT, false, pane);
            lv_obj_align(price_label, LV_ALIGN_TOP_RIGHT, -inset, y + row_text_y);
            if (jacket) {
                label(std::to_string(holding.lots), jacket_width - own_inset - own_width, y + row_text_y,
                      own_width, market_font, pane_text, LV_TEXT_ALIGN_RIGHT, false, pane);
            }
        }
    }

    draw_market_actions(session.run.day == session.economy.days);
}

// Show day-specific shortcuts and prefer a status message when one is present.
void GameUi::draw_market_actions(bool last_day) {
    constexpr int footer_height = height - market_footer_y;
    auto* bar = box(0, market_footer_y, width, footer_height, market_alt);
    box(0, 0, width, 1, pane_rule, bar);

    std::string status = message_;
    if (status.empty() && last_day) {
        status = "Final day. Sell your cargo before closing.";
    } else if (status.empty() && jacket_focused_) {
        const auto& run = profile_.active->run;
        const auto exploits = run.unused_exploits + run.exposed_exploits;
        status = run.jacket;
        if (exploits) status += "  /  0days: " + std::to_string(exploits);
    }
    int action_y = 1;
    if (!status.empty()) {
        label(status, 8, 1, width - 16, market_font, message_ == "Save failed. Try again." ? red : pane_muted,
              LV_TEXT_ALIGN_LEFT, false, bar);
        action_y += lv_font_get_line_height(market_font);
    }

    struct MarketAction {
        const char* shortcut;
        const char* suffix;
        std::uint32_t key;
        int width;
    };
    const MarketAction actions[]{
        {"B", "uy", 'b', 43},
        {"S", "ell", 's', 44},
        {last_day ? "E" : "J", last_day ? "nd Run" : "et",
         static_cast<std::uint32_t>(last_day ? 'e' : 'j'), 74},
        {"Space", " Jacket", ' ', 105},
        {"N", "ews", 'n', 54},
    };
    int x = 0;
    for (const auto& action : actions) {
        auto* tile = box(x, action_y, action.width, footer_height - action_y, market_alt, bar);
        clickable(tile, action.key);

        auto* caption = lv_spangroup_create(tile);
        lv_obj_remove_style_all(caption);
        lv_obj_remove_flag(caption, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(caption, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(caption, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(caption, market_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(caption, lv_color_hex(pane_text), LV_PART_MAIN);
        auto* initial = lv_spangroup_add_span(caption);
        lv_span_set_text_static(initial, action.shortcut);
        lv_style_set_text_font(lv_span_get_style(initial), market_hotkey_font_ ? market_hotkey_font_ : market_font);
        lv_style_set_text_color(lv_span_get_style(initial), lv_color_hex(pane_accent));
        lv_span_set_text_static(lv_spangroup_add_span(caption), action.suffix);
        lv_spangroup_refresh(caption);
        lv_obj_center(caption);
        x += action.width;
    }
}

// Preview the order's cost, cash balance, and cargo usage for the proposed quantity.
void GameUi::draw_trade() {
    hud();
    const auto& session = *profile_.active;
    const auto& item = session.economy.goods[selected_good_];
    const auto& holding = session.run.holdings[selected_good_];
    const auto price = session.run.market[selected_good_].price;
    const auto total = price * quantity_;
    const auto maximum = trade_limit(session, selected_good_, buying_);
    const auto valid = quantity_ > 0 && quantity_ <= maximum;

    label(std::string(buying_ ? "BUY  " : "SELL  ") + item.name, 8, 28, 304, &lv_font_montserrat_14, text);
    label(cash(price) + " per lot  /  1 slot each", 8, 48, 304,
          &lv_font_montserrat_12, muted);
    label("Quantity", 8, 69, 92, &lv_font_montserrat_12, text);
    button(106, 65, 33, 21, "-", LV_KEY_DOWN);
    label(std::to_string(quantity_), 142, 67, 43, &lv_font_montserrat_16,
          valid ? amber : red, LV_TEXT_ALIGN_CENTER);
    button(189, 65, 33, 21, "+", LV_KEY_UP);
    label("Max " + std::to_string(maximum), 230, 69, 82, &lv_font_montserrat_12, muted, LV_TEXT_ALIGN_RIGHT);
    label("Order total", 8, 93, 112, &lv_font_montserrat_12, muted);
    label(cash(total), 120, 91, 192, &lv_font_montserrat_16, amber, LV_TEXT_ALIGN_RIGHT);
    if (valid) {
        const auto next_cash = session.run.cash + (buying_ ? -total : total);
        const auto next_slots = used_slots(session) + (buying_ ? quantity_ : -quantity_);
        label("Cash " + cash(next_cash) + "  /  Jacket " + std::to_string(next_slots)
                  + "/" + std::to_string(session.run.capacity),
              8, 114, 304, &lv_font_montserrat_12, text);
    }
    std::string status = "Type quantity or choose Max";
    if (!maximum) {
        status = !buying_ ? "You own none"
            : session.run.cash < price ? "Insufficient cash" : "No room in jacket";
    } else if (!buying_) {
        status = "Average cost " + cash(average_cost(holding)) + " per lot";
    }
    footer(status, {{"Enter Confirm", enter}, {"M Max", 'm'}, {"Esc Back", escape}});
}

// Lay out the cities in a two-column grid and disable the current location.
void GameUi::draw_jet() {
    hud();
    const auto& session = *profile_.active;
    const auto& cities = session.economy.cities;
    label("JET TO A NEW CITY", 8, 26, 304, &lv_font_montserrat_10, muted);
    for (int i = 0; i < static_cast<int>(cities.size()); ++i) {
        const int x = 7 + (i % 2) * 155;
        const int y = 42 + (i / 2) * 24;
        button(x, y, 151, 22, std::to_string(i + 1) + "  " + cities[i].name,
               jet_city_key + i, i == destination_, i != session.run.city);
    }
    footer(cities[destination_].hint + "  /  1 day", {{"1-8 or Enter Jet", enter}, {"Esc Back", escape}});
}

// Present the headline's affected goods, price change, and duration when applicable.
void GameUi::draw_news() {
    const auto& session = *profile_.active;
    const auto& run = session.run;
    if (run.last_headline == ai_wars_headline) {
        draw_ai_wars_news();
        return;
    }
    box(0, 0, width, height, market_bg);
    hud(market_font, true);
    const bool market_wide = run.price_news && run.price_news->all_goods;
    if (!market_wide) {
        label(arrival_news_ ? "BREAKING NEWS" : "LAST HEADLINE", 8, 27, 304, market_font, pane_accent);
    }
    std::string effect;
    const lv_font_t* effect_font = market_font;
    if (run.last_headline < 0) {
        label("No headlines yet", 8, 71, 304, market_font, pane_muted, LV_TEXT_ALIGN_CENTER);
    } else if (market_wide) {
        const auto& news = *run.price_news;
        const auto& stories = news.direction > 0 ? session.economy.market_wide_news.up
                                               : session.economy.market_wide_news.down;
        const auto& story = stories[news.story];
        const auto accent = news.direction > 0 ? pane_accent : red;
        const auto* title_font = assets_.load_font("Montserrat-Bold.ttf", 18);
        if (!title_font) title_font = &lv_font_montserrat_18;
        lv_point_t title_size{};
        lv_text_get_size(&title_size, story.title.c_str(), title_font, 0, 0, 304, LV_TEXT_FLAG_NONE);
        auto* title = label(story.title, 8, 28, 304, title_font, accent, LV_TEXT_ALIGN_LEFT, true);
        const int story_y = 28 + title_size.y + 4;
        const lv_font_t* story_font = market_font;
        lv_point_t story_size{};
        lv_text_get_size(&story_size, story.text.c_str(), story_font, 0, 0, 304, LV_TEXT_FLAG_NONE);
        if (story_size.y > 125 - story_y) story_font = &lv_font_montserrat_12;
        label(story.text, 8, story_y, 304, story_font, pane_text, LV_TEXT_ALIGN_LEFT, true);
        box(8, 124, 304, 1, accent);
        if (animate_scene_) slide(title, width, 8, 240);
        effect = "All commodities  " + change(news_price_percent(run, session.economy.events[run.last_headline]))
            + "  /  " + (run.day == news.day ? std::string{"This market"}
                                              : "Day " + std::to_string(news.day) + " market");
        effect_font = &lv_font_montserrat_12;
    } else {
        const auto& event = session.economy.events[run.last_headline];
        auto* banner = label(event.headline, 8, 46, 304, market_font, pane_text, LV_TEXT_ALIGN_LEFT, true);
        lv_obj_set_height(banner, 4 * lv_font_get_line_height(market_font));
        lv_label_set_long_mode(banner, LV_LABEL_LONG_DOT);
        if (animate_scene_) slide(banner, width, 8, 240);
        std::string goods;
        for (int good : event.goods) {
            if (!goods.empty()) goods += ", ";
            goods += session.economy.goods[good].short_name;
        }
        label(goods, 8, 111, 304, market_font, pane_accent);
        effect = event.local ? "Local" : "Global";
        const int price_percent = news_price_percent(run, event);
        if (price_percent != 100) effect += "  Price " + change(price_percent);
        if (!event.price_direction && run.last_event_day[run.last_headline] > 0) {
            const int end = run.last_event_day[run.last_headline] + event.duration - 1;
            effect += end >= session.economy.days ? "  /  Through final day"
                : (end < run.day ? "  /  Ended on day " : "  /  Through day ") + std::to_string(end);
        }
    }
    label(effect, 8, 129, 304, effect_font, pane_muted);
    box(0, action_bar_y, width, height - action_bar_y, market_alt);
    if (message_.empty()) {
        draw_action_bar({{"Press Any Key", enter}}, true);
    } else {
        button(192, market_wide ? 149 : 26, 120, 19, "Press Any Key", enter, false, true, nullptr,
               market_font, true);
        box(0, action_bar_y, width, 1, pane_rule);
        label(message_, 8, 150, market_wide ? 180 : 304, market_wide ? &lv_font_montserrat_12 : market_font,
              message_ == "Save failed. Try again." ? red : pane_muted);
    }
}

// Show the courier's jacket price and compare its capacity with the current jacket.
void GameUi::draw_offer() {
    hud();
    const auto& session = *profile_.active;
    const auto& offer = session.economy.jackets[session.run.pending_jacket];
    label("BAGGAGE CAROUSEL", 8, 28, 304, &lv_font_montserrat_10, amber);
    auto* coat = box(17, 47, 73, 68, 0x223543);
    box(21, 14, 31, 39, 0x8196a0, coat);
    box(12, 19, 9, 29, 0x8196a0, coat);
    box(52, 19, 9, 29, 0x8196a0, coat);
    box(35, 16, 2, 35, bg, coat);
    box(38, 31, 9, 5, amber, coat);
    box(10, 57, 53, 3, line, coat);
    if (animate_scene_) slide(coat, -80, 17, 440);
    label(offer.name, 104, 49, 208, &lv_font_montserrat_16, text);
    label(std::to_string(session.run.capacity) + " to " + std::to_string(offer.capacity) + " slots",
          104, 76, 208, &lv_font_montserrat_12, muted);
    label(cash(offer.cost), 104, 94, 208, &lv_font_montserrat_16, amber);
    footer("A courier offers a jacket upgrade", {{"B Buy Jacket", 'b'}, {"P Pass", 'p'}, {"Esc Pause", escape}});
}

// Choose the vendor art and build a purchase panel with current cash and equipment.
void GameUi::draw_black_hat() {
    const auto& session = *profile_.active;
    const auto& run = session.run;
    // The last two stories feature the briefcase and the loyalty card.
    const char* filename = "01-alley-offer.png";
    if (run.encounter.story == 6) filename = "02-briefcase.png";
    if (run.encounter.story == 7) filename = "03-loyalty-card.png";
    encounter_image_ = load_encounter_image(std::filesystem::path("images/encounters/black-hat") / filename);
    draw_market();

    auto& scene = black_hat_.emplace();
    scene.layer = box(0, 0, width, height, 0x000000);
    lv_obj_set_style_bg_opa(scene.layer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(scene.layer, LV_OBJ_FLAG_CLICKABLE);
    if (encounter_image_) scene.canvas = draw_scene_image(*encounter_image_, width, scene.layer);

    scene.panel = box(0, height, width, height - black_hat_panel_y, 0x000000, scene.layer);
    label("A FIGURE IN A BLACK HAT", 8, 3, width - 16, market_font, amber,
          LV_TEXT_ALIGN_LEFT, false, scene.panel);
    label(encounter_story(session), 8, 21, width - 16, market_font, text,
          LV_TEXT_ALIGN_LEFT, true, scene.panel);
    button(8, 71, 140, 19, "1 0day Exploit " + cash(exploit_price), '1', false,
           can_buy_equipment(session, Equipment::Exploit), scene.panel);
    button(150, 71, 162, 19,
           "2 Jacket +" + std::to_string(jacket_upgrade_slots) + " slots " + cash(jacket_upgrade_price),
           '2', false, can_buy_equipment(session, Equipment::JacketUpgrade), scene.panel);
    const auto exploits = run.unused_exploits + run.exposed_exploits;
    scene.status = label("0days: " + std::to_string(exploits) + " / Exposed: "
                         + std::to_string(run.exposed_exploits) + " / Cash: " + compact(run.cash),
                         8, 93, 243, &lv_font_montserrat_12, muted, LV_TEXT_ALIGN_LEFT, false, scene.panel);
    button(253, 91, 59, 19, "P Pass", 'p', false, true, scene.panel);
    scene.started = lv_tick_get();
}

// Slide in the artwork, pause, raise the purchase panel, and fade the dismissed scene.
void GameUi::update_black_hat() {
    auto& scene = *black_hat_;
    const auto elapsed = lv_tick_elaps(scene.started);
    if (scene.phase == BlackHatPhase::Fade) {
        if (elapsed < scene_transition::flight_fade_ms) {
            scene_transition::pixel_fade(scene.canvas, *encounter_image_, elapsed, scene.fade_position,
                                         scene_transition::flight_fade_ms);
        } else {
            finish_black_hat();
        }
        return;
    }
    if (scene.phase == BlackHatPhase::Choice) return;

    if (scene.canvas) lv_obj_set_x(scene.canvas, scene_transition::slide_offset(width, elapsed, black_hat_slide_ms));
    constexpr auto panel_start = black_hat_slide_ms + black_hat_pause_ms;
    if (elapsed < panel_start) return;

    const auto panel_elapsed = elapsed - panel_start;
    const int offset = scene_transition::slide_offset(height - black_hat_panel_y, panel_elapsed, black_hat_panel_ms);
    lv_obj_set_y(scene.panel, black_hat_panel_y + offset);
    if (panel_elapsed >= black_hat_panel_ms) scene.phase = BlackHatPhase::Choice;
}

// Save a vendor purchase before dismissing the panel, or keep it open to report a failure.
void GameUi::handle_black_hat(std::uint32_t key) {
    if (black_hat_->phase == BlackHatPhase::Fade) {
        finish_black_hat();
        return;
    }
    if (black_hat_->phase != BlackHatPhase::Choice) return;
    if (key == 'p') {
        profile_.active->run.encounter = {};
        fade_black_hat();
        return;
    }
    if (key != '1' && key != '2') return;

    Profile next = profile_;
    auto& session = *next.active;
    const auto equipment = key == '1' ? Equipment::Exploit : Equipment::JacketUpgrade;
    if (!buy_equipment(session, equipment)) {
        message_ = key == '2' && session.run.capacity > max_cargo - jacket_upgrade_slots
            ? "Jacket is at its upgrade limit" : "Insufficient cash";
    } else {
        session.run.encounter = {};
        if (commit(std::move(next))) {
            fade_black_hat();
            return;
        }
    }

    auto* status = black_hat_->status;
    lv_label_set_text(status, message_.c_str());
    lv_obj_set_style_text_color(status, lv_color_hex(red), LV_PART_MAIN);
}

// Capture the finished vendor overlay and start its pixel fade above the rebuilt market.
void GameUi::fade_black_hat() {
    // Flatten the artwork and all panel contents into one frame above the updated market.
    DrawBuffer frame(lv_snapshot_take(black_hat_->layer, LV_COLOR_FORMAT_ARGB8888), lv_draw_buf_destroy);
    show_arrival();
    render();
    if (!frame || page_ != Page::Market) return;

    encounter_image_ = std::move(frame);
    auto& scene = black_hat_.emplace();
    scene.canvas = draw_scene_image(*encounter_image_, 0);
    clickable(scene.canvas, LV_KEY_ENTER);
    scene.phase = BlackHatPhase::Fade;
    scene.started = lv_tick_get();
}

// Delete the canvas before releasing its borrowed image buffer.
void GameUi::finish_black_hat() {
    lv_obj_delete(black_hat_->canvas);
    encounter_image_.reset();
    black_hat_.reset();
}

// Show a generic story and paged cargo losses, with a dedicated view for the vendor.
void GameUi::draw_encounter() {
    const auto& session = *profile_.active;
    const auto& run = session.run;
    const auto& encounter = run.encounter;
    if (encounter.type == EncounterType::BlackHat) {
        draw_black_hat();
        return;
    }
    hud();
    const char* heading = "";
    switch (encounter.type) {
        case EncounterType::Mugging: heading = "MUGGING"; break;
        case EncounterType::Family: heading = "FAMILY FOUND YOUR STASH"; break;
        case EncounterType::Patch: heading = "SECURITY ADVISORY"; break;
        case EncounterType::Fight: heading = "FIGHT SUCCESSFUL"; break;
        case EncounterType::Escape: heading = "YOU ESCAPED"; break;
        case EncounterType::Confiscation: heading = "CARGO CONFISCATED"; break;
        default: break;
    }
    label(heading, 8, 27, 304, market_font, amber);
    constexpr int story_y = 45;
    const auto story = encounter_story(session);
    label(story, 8, story_y, 304, market_font, text, LV_TEXT_ALIGN_LEFT, true);

    const int count = static_cast<int>(encounter.cargo.size());
    const int first = encounter_cargo_page_ * encounter_rows_per_page;
    lv_point_t story_size{};
    lv_text_get_size(&story_size, story.c_str(), market_font, 0, 0, 304, LV_TEXT_FLAG_NONE);
    const int cargo_y = story_y + story_size.y + 4;
    for (int i = 0; i < encounter_rows_per_page && first + i < count; ++i) {
        const auto& cargo = encounter.cargo[first + i];
        label("-" + std::to_string(cargo.lots) + " x "
                  + session.economy.goods[cargo.good].name,
              8, cargo_y + lv_font_get_line_height(market_font) * i, 304, market_font, red);
    }
    if (count > encounter_rows_per_page) {
        const int pages = (count + encounter_rows_per_page - 1) / encounter_rows_per_page;
        label(std::to_string(encounter_cargo_page_ + 1) + "/" + std::to_string(pages),
              236, 27, 76, market_font, muted, LV_TEXT_ALIGN_RIGHT);
        draw_action_bar({{"Z Prev", 'z'}, {"C Next", 'c'}, {"Continue", enter}});
    } else {
        draw_action_bar({{"Enter Continue", enter}});
    }
}

// Show the final score calculation and fit large totals within the score field.
void GameUi::draw_close() {
    hud(market_font);
    const auto& session = *profile_.active;
    const auto buyout = final_buyout(session);
    label("CLOSE RUN", 8, 29, 304, market_font, amber);
    label("Cash", 8, 52, 104, market_font, muted);
    label(cash(session.run.cash), 112, 52, 200, market_font, text, LV_TEXT_ALIGN_RIGHT);
    label("Jacket goods", 8, 75, 104, market_font, muted);
    label(cash(buyout), 112, 75, 200, market_font, text, LV_TEXT_ALIGN_RIGHT);
    box(8, 98, 304, 1, line);
    label("Final score", 8, 109, 102, market_font, text);
    const auto total = format_number(session.run.cash + buyout);
    constexpr int value_width = 202;
    lv_point_t size{};
    lv_text_get_size(&size, total.c_str(), &lv_font_montserrat_20, 0, 0, width, LV_TEXT_FLAG_NONE);
    const auto* score_font = size.x <= value_width ? &lv_font_montserrat_20 : market_font;
    label(total, 110, 109 + (lv_font_get_line_height(market_font) - lv_font_get_line_height(score_font)) / 2,
          value_width, score_font, amber, LV_TEXT_ALIGN_RIGHT);
    if (!message_.empty()) label(message_, 8, 129, 304, market_font, red);
    draw_action_bar({{"Press any key to continue", enter}});
}

// Draw ranked scores and the actions for sharing an entry.
void GameUi::draw_scores() {
    title("HIGHSCORES");
    if (profile_.scores.empty()) {
        label("Finish a run to set the first score", 8, 69, 304, &lv_font_montserrat_14,
              muted, LV_TEXT_ALIGN_CENTER);
    }
    for (std::size_t i = 0; i < profile_.scores.size(); ++i) {
        const auto& score = profile_.scores[i];
        const int y = 22 + static_cast<int>(i) * 12;
        const bool selected = i == static_cast<std::size_t>(score_row_);
        auto* tile = box(0, y, width, 12, selected ? focus : i % 2 ? panel : bg);
        clickable(tile, i == 9 ? '0' : static_cast<std::uint32_t>('1' + i));
        if (selected) box(0, y, 2, 12, amber);
        const auto color = i < podium.size() ? podium[i] : text;
        label(std::to_string(i + 1), 8, y, 23, &lv_font_montserrat_10, color);
        label(score.player, 34, y, 145, &lv_font_montserrat_10, color);
        label(format_number(score.value), 179, y, 133, &lv_font_montserrat_10, color, LV_TEXT_ALIGN_RIGHT);
    }
    if (profile_.scores.empty()) {
        footer("", {{"Esc Home", escape}});
    } else {
        footer("F/X Select   0 selects rank 10",
               {{"Enter Share QR", enter}, {"Esc Home", escape}});
    }
}

// Encode the selected score as a QR code alongside the human-readable name and value.
void GameUi::draw_share() {
    const auto& score = profile_.scores[score_row_];
    label("SCAN TO SHARE", 8, 17, 145, &lv_font_montserrat_12, amber);
    label(score.player, 8, 46, 145, &lv_font_montserrat_16, text, LV_TEXT_ALIGN_LEFT, true);
    const auto value = format_number(score.value);
    label(value, 8, 91, 145, value.size() > 15 ? &lv_font_montserrat_12 : &lv_font_montserrat_16, amber);
    button(8, 141, 142, 20, "Esc  Back to scores", escape);
    const auto payload = share_score(score);
    auto* qr = lv_qrcode_create(screen_);
    lv_obj_set_pos(qr, 164, 12);
    lv_qrcode_set_size(qr, 146);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_set_quiet_zone(qr, true);
    if (lv_qrcode_update(qr, payload.data(), static_cast<std::uint32_t>(payload.size())) != LV_RESULT_OK) {
        lv_obj_delete(qr);
        label("QR unavailable", 164, 76, 146, &lv_font_montserrat_12, red, LV_TEXT_ALIGN_CENTER);
    }
}

} // namespace wars
