// Routes input, manages screens and transitions, and saves player actions through ProfileStore.

#include "game_ui.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "logger.h"
#include "screenshot_service.h"

#include <algorithm>
#include <random>

namespace wars {
namespace {

constexpr std::uint32_t up = LV_KEY_UP;
constexpr std::uint32_t down = LV_KEY_DOWN;
constexpr std::uint32_t left = LV_KEY_LEFT;
constexpr std::uint32_t right = LV_KEY_RIGHT;
constexpr std::uint32_t enter = LV_KEY_ENTER;
constexpr std::uint32_t intro_music_restart_delay_ms = 2000;

// Fold ASCII action keys to lowercase while preserving LVGL and synthetic key codes.
std::uint32_t lower(std::uint32_t key) {
    return key >= 'A' && key <= 'Z' ? key + ('a' - 'A') : key;
}

// Map vertical navigation keys and letter shortcuts to a signed row step.
int movement(std::uint32_t key) {
    if (key == up || key == 'f') return -1;
    if (key == down || key == 'x') return 1;
    return 0;
}

// Map horizontal navigation keys and letter shortcuts to a signed page step.
int page_movement(std::uint32_t key) {
    if (key == left || key == 'z') return -1;
    if (key == right || key == 'c') return 1;
    return 0;
}

// Combine two entropy draws into the seed for a new run.
std::uint64_t new_seed() {
    std::random_device entropy;
    const auto high = static_cast<std::uint64_t>(entropy()) << 32;
    return high | static_cast<std::uint32_t>(entropy());
}

} // namespace

// Choose the opening screen, install input listeners, and render with the loaded profile.
GameUi::GameUi(ProfileStore& store, Profile profile, std::filesystem::path economy_file,
               const app::AssetManager& assets, ProfileRecovery recovery)
    : store_(store), profile_(std::move(profile)), economy_file_(std::move(economy_file)),
      assets_(assets), market_hotkey_font_(assets.load_font("Montserrat-Bold.ttf", 14)),
      screen_(lv_screen_active()) {
    if (recovery == ProfileRecovery::Backup) message_ = "Save backup restored";
    if (recovery == ProfileRecovery::Fresh) message_ = "Invalid save archived";
    if (profile_.sound_enabled) intro_music_.start(assets_.resolve("audio/intro.mp3"));
    if (load_intro(assets_)) page_ = Page::Intro;
    else load_home_artwork(false);
    home_row_ = profile_.active ? 0 : 1;
    platform::set_global_key_listener(key_pressed, this);
    platform::set_key_release_listener(key_released, this);
    render();
}

// Unregister callbacks before deleting LVGL widgets that refer to this UI.
GameUi::~GameUi() {
    platform::clear_key_release_listener(key_released, this);
    platform::clear_global_key_listener(key_pressed, this);
    lv_obj_clean(screen_);
}

// Drain queued actions before advancing each active animation for this frame.
bool GameUi::poll() {
    while (running_ && !keys_.empty()) {
        const auto key = keys_.front();
        keys_.pop_front();
        handle_key(key);
    }
    if (running_ && intro_music_queued_at_ && lv_tick_elaps(*intro_music_queued_at_) >= intro_music_restart_delay_ms) {
        intro_music_queued_at_.reset();
        intro_music_.start(assets_.resolve("audio/intro.mp3"));
    }
    if (running_ && page_ == Page::Intro) update_intro();
    if (running_ && page_ == Page::Home && home_ && home_->veil) update_home_reveal();
    if (running_ && flight_image_) update_flight();
    if (running_ && page_ == Page::Officer) update_officer();
    if (running_ && story_) update_story();
    if (running_ && black_hat_) update_black_hat();
    if (running_ && page_ == Page::Ending) update_ending();
    if (running_ && page_ == Page::Market && jacket_slide_) update_jacket_slide();
    return running_;
}

// Queue ordinary presses and mark a held Escape as a quit request.
void GameUi::key_pressed(std::uint32_t key, bool long_press, void* user) {
    auto& ui = *static_cast<GameUi*>(user);
    if (key == LV_KEY_ESC) {
        ui.escape_held_ = !long_press;
        if (long_press) ui.running_ = false;
    } else if (!long_press) {
        ui.keys_.push_back(key);
    }
}

// Queue a short Escape action on release once a hold has been ruled out.
void GameUi::key_released(std::uint32_t key, void* user) {
    auto& ui = *static_cast<GameUi*>(user);
    if (key == LV_KEY_ESC && ui.escape_held_) {
        ui.escape_held_ = false;
        ui.keys_.push_back(key);
    }
}

// Read the widget's synthetic key so clicks use the same action queue as keyboard input.
void GameUi::click(lv_event_t* event) {
    auto& ui = *static_cast<GameUi*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_target_obj(event);
    const auto key = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(target));
    ui.keys_.push_back(static_cast<std::uint32_t>(key));
}

// Give screenshots and active scenes first access to input, then route actions by page.
void GameUi::handle_key(std::uint32_t raw_key) {
    if (raw_key == platform::kKeyPrintScreen) {
        const auto capture = platform::screenshot::capture_active_screen();
        LOG_INFO("AI Wars screenshot: {}", capture.success ? capture.path : capture.error);
        return;
    }
    if (flight_image_) {
        if (raw_key == enter || raw_key == ' ' || raw_key == LV_KEY_ESC) finish_flight();
        return;
    }
    if (page_ == Page::Officer) {
        handle_officer(lower(raw_key));
        return;
    }
    if (story_) {
        continue_story();
        return;
    }
    if (black_hat_) {
        handle_black_hat(lower(raw_key));
        return;
    }
    if (page_ == Page::Ending) {
        advance_ending();
        return;
    }
    if (raw_key == LV_KEY_ESC && page_ != Page::Intro && page_ != Page::Close) {
        message_.clear();
        back();
        render();
        return;
    }

    const auto key = lower(raw_key);
    switch (page_) {
        case Page::Intro:
            page_ = Page::Home;
            load_home_artwork(true);
            render();
            // The redraw has deleted the old canvas, so the intro buffers can be released.
            intro_canvas_ = nullptr;
            intro_frame_.reset();
            intro_image_.reset();
            intro_tiles_.clear();
            return;
        case Page::Home: {
            message_.clear();
            const int direction = key == left ? -1 : key == right ? 1 : movement(key);
            if (direction) {
                const auto row = std::clamp(home_row_ + direction, profile_.active ? 0 : 1,
                                            static_cast<int>(home_keys.size()) - 1);
                if (row == home_row_) return;
                home_row_ = row;
                break;
            }
            const auto action = key == enter ? home_keys[home_row_] : key;
            if (action == 'n' || (action == 'c' && profile_.active)) {
                intro_music_queued_at_.reset();
                intro_music_.stop();
                if (action == 'n') enter_name();
                else show_arrival();
            } else if (action == 'h') {
                score_row_ = 0;
                page_ = Page::Scores;
            } else if (action == 'g') {
                help_page_ = 0;
                page_ = Page::Help;
            } else if (action == 's') {
                Profile next = profile_;
                next.sound_enabled = !next.sound_enabled;
                if (commit(std::move(next))) {
                    intro_music_queued_at_.reset();
                    if (profile_.sound_enabled) intro_music_.start(assets_.resolve("audio/intro.mp3"));
                    else intro_music_.stop();
                }
            } else {
                return;
            }
            break;
        }
        case Page::Help:
            move_help(page_movement(key));
            break;
        case Page::Name:
            message_.clear();
            if (raw_key == enter) {
                const auto normalized = player_name(name_);
                if (normalized.empty()) {
                    message_ = "Enter a player name";
                } else if (profile_.active) {
                    page_ = Page::Replace;
                } else {
                    start();
                }
            } else if (raw_key == LV_KEY_BACKSPACE) {
                if (!name_.empty()) name_.pop_back();
                prefilled_name_ = false;
            } else if (raw_key == LV_KEY_DEL) {
                name_.clear();
                prefilled_name_ = false;
            } else if (raw_key < 128 && name_character(static_cast<char>(raw_key))) {
                if (prefilled_name_) name_.clear();
                if (name_.size() < max_name_length) name_.push_back(static_cast<char>(raw_key));
                prefilled_name_ = false;
            }
            break;
        case Page::Replace:
            if (key == enter) start();
            break;
        case Page::Market:
            handle_market(key);
            break;
        case Page::Trade:
            handle_trade(key);
            break;
        case Page::Jet:
            handle_jet(key);
            if (flight_image_) return;
            break;
        case Page::News:
            leave_arrival();
            break;
        case Page::Offer:
            if (key == 'b' || key == 'p' || key == enter) {
                Profile next = profile_;
                if (!answer_jacket(*next.active, key == 'b')) {
                    message_ = "Insufficient cash";
                } else if (commit(std::move(next))) {
                    page_ = Page::Market;
                }
            }
            break;
        case Page::Encounter:
            handle_encounter(key);
            break;
        case Page::Officer:
        case Page::Stash:
        case Page::Family:
        case Page::Ending:
            return;
        case Page::Close:
            close_run();
            if (page_ == Page::Ending) return;
            break;
        case Page::Scores:
            handle_scores(key);
            break;
        case Page::Share:
            if (key == enter) page_ = Page::Scores;
            break;
    }
    render();
}

// Navigate the focused pane, persist changes to sorting, and open trade, travel, or news.
void GameUi::handle_market(std::uint32_t key) {
    if (key == ' ' || key == 'i') {
        toggle_jacket();
        return;
    }
    if (key == platform::kKeyTab) {
        Profile next = profile_;
        next.sort_order = static_cast<SortOrder>((static_cast<int>(next.sort_order) + 1) % 4);
        commit(std::move(next));
        return;
    }
    // Decode which pane and visible row were clicked, then reuse the digit shortcut.
    if (key >= pane_row_key && key < pane_row_key + 2 * rows_per_page) {
        const auto offset = static_cast<int>(key - pane_row_key);
        const bool jacket = offset >= rows_per_page;
        if (jacket && !jacket_open_) return;
        jacket_focused_ = jacket;
        key = '1' + offset % rows_per_page;
    }

    const auto goods = listing(jacket_focused_);
    const int count = static_cast<int>(goods.size());
    int row = selected_row(goods, jacket_focused_);
    if (const int direction = movement(key); direction && count) {
        row = std::clamp(row + direction, 0, count - 1);
    } else if (const int direction = page_movement(key); direction && count) {
        const int page = std::clamp(row / rows_per_page + direction, 0, (count - 1) / rows_per_page);
        row = page * rows_per_page;
    } else if (key >= '1' && key < '1' + rows_per_page) {
        const int candidate = row / rows_per_page * rows_per_page + static_cast<int>(key - '1');
        if (candidate < count) row = candidate;
    } else {
        const auto& session = *profile_.active;
        if (key == 'b' || key == 's' || key == enter) {
            open_trade(key == 'b' || (key == enter && !jacket_focused_));
        } else if (key == 'n') {
            page_ = Page::News;
            arrival_news_ = false;
        } else if (key == 'j' && session.run.day < session.economy.days) {
            destination_ = (session.run.city + 1) % static_cast<int>(session.economy.cities.size());
            message_.clear();
            page_ = Page::Jet;
        } else if (key == 'e' && session.run.day == session.economy.days) {
            message_.clear();
            page_ = Page::Close;
        }
        return;
    }
    if (count) (jacket_focused_ ? jacket_selection_ : market_selection_) = goods[row];
}

// Capture the selected good and start a quantity draft within the trade limit.
void GameUi::open_trade(bool buy) {
    const auto goods = listing(jacket_focused_);
    if (goods.empty()) return;
    selected_good_ = goods[selected_row(goods, jacket_focused_)];
    buying_ = buy;
    quantity_ = std::min(1, trade_limit(*profile_.active, selected_good_, buy));
    typing_quantity_ = false;
    message_.clear();
    page_ = Page::Trade;
}

// Edit a lot quantity or save the confirmed trade, then restore a valid jacket selection.
void GameUi::handle_trade(std::uint32_t key) {
    const int maximum = trade_limit(*profile_.active, selected_good_, buying_);
    if (key == enter) {
        Profile next = profile_;
        if (!trade(*next.active, selected_good_, buying_, quantity_)) {
            message_ = "Enter a quantity within the limit";
            return;
        }
        const auto item = next.active->economy.goods[selected_good_].short_name;
        // Remember the selected row so selling the last lot can select the nearest remaining row.
        const auto previous_jacket = listing(true);
        const bool removed_selection = next.active->run.holdings[selected_good_].lots == 0
            && !previous_jacket.empty()
            && previous_jacket[selected_row(previous_jacket, true)] == selected_good_;
        if (commit(std::move(next))) {
            message_ = (buying_ ? "Bought " : "Sold ") + std::to_string(quantity_) + " x " + item;
            page_ = Page::Market;
            if (removed_selection) {
                const auto jacket = listing(true);
                const auto row = selected_row(previous_jacket, true);
                jacket_selection_ = jacket.empty() ? -1 : jacket[std::min(row, static_cast<int>(jacket.size()) - 1)];
            }
        }
        return;
    }

    message_.clear();
    if (key == 'm') {
        quantity_ = maximum;
        typing_quantity_ = false;
    } else if (key >= '0' && key <= '9') {
        const int digit = static_cast<int>(key - '0');
        quantity_ = typing_quantity_ ? std::min(max_cargo, quantity_ * 10 + digit) : digit;
        typing_quantity_ = true;
    } else if (key == LV_KEY_BACKSPACE) {
        quantity_ /= 10;
        typing_quantity_ = true;
    } else if (const int direction = movement(key)) {
        quantity_ = std::clamp(quantity_ - direction, 0, maximum);
        typing_quantity_ = false;
    }
}

// Pick a valid destination from the two-column grid or fly directly using a number key.
void GameUi::handle_jet(std::uint32_t key) {
    const auto& session = *profile_.active;
    const int count = static_cast<int>(session.economy.cities.size());
    const bool numbered = key >= '1' && key <= '8';
    if (key == enter || numbered) {
        const int candidate = numbered ? static_cast<int>(key - '1') : destination_;
        if (can_jet(session, candidate)) {
            destination_ = candidate;
            begin_flight();
        }
        return;
    }
    if (key >= jet_city_key && key < jet_city_key + count) {
        const int candidate = static_cast<int>(key - jet_city_key);
        if (candidate != session.run.city) destination_ = candidate;
        return;
    }
    const int direction = movement(key) * 2 + page_movement(key);
    if (!direction) return;
    for (int candidate = destination_ + direction; candidate >= 0 && candidate < count; candidate += direction) {
        if (candidate != session.run.city) {
            destination_ = candidate;
            break;
        }
    }
}

// Dismiss the generic encounter report or page through its cargo changes.
void GameUi::handle_encounter(std::uint32_t key) {
    auto& session = *profile_.active;
    const auto& encounter = session.run.encounter;
    if (key == enter || key == ' ') {
        session.run.encounter = {};
        show_arrival();
    } else if (const int direction = page_movement(key); direction && !encounter.cargo.empty()) {
        const int last_page = (static_cast<int>(encounter.cargo.size()) - 1) / encounter_rows_per_page;
        encounter_cargo_page_ = std::clamp(encounter_cargo_page_ + direction, 0, last_page);
    }
}

// Move the leaderboard selection or open the selected entry's QR code.
void GameUi::handle_scores(std::uint32_t key) {
    const int count = static_cast<int>(profile_.scores.size());
    if (!count) return;
    if (const int direction = movement(key)) {
        score_row_ = std::clamp(score_row_ + direction, 0, count - 1);
    } else if (key >= '0' && key <= '9') {
        const int candidate = key == '0' ? 9 : static_cast<int>(key - '1');
        if (candidate < count) score_row_ = candidate;
    } else if (key == enter) {
        page_ = Page::Share;
    }
}

// Apply the current page's Escape action, including dismissing an arrival prompt.
void GameUi::back() {
    switch (page_) {
        case Page::Intro:
        case Page::Home:
            break;
        case Page::Replace:
            page_ = Page::Name;
            break;
        case Page::Trade:
        case Page::Jet:
            page_ = Page::Market;
            break;
        case Page::News:
            leave_arrival();
            break;
        case Page::Encounter:
            handle_encounter(enter);
            break;
        case Page::Officer:
        case Page::Stash:
        case Page::Family:
        case Page::Close:
        case Page::Ending:
            break;
        case Page::Share:
            page_ = Page::Scores;
            break;
        case Page::Help:
        case Page::Name:
        case Page::Market:
        case Page::Offer:
        case Page::Scores:
            page_ = Page::Home;
            break;
    }
}

// Prefill the name field with the last player and allow typing to replace it.
void GameUi::enter_name() {
    name_ = profile_.last_player;
    prefilled_name_ = !name_.empty();
    page_ = Page::Name;
}

// Load current rules and save a fresh session before opening its first market.
void GameUi::start() {
    try {
        Profile next = profile_;
        auto economy = load_economy(economy_file_);
        auto run = start_run(economy, name_, new_seed());
        next.last_player = run.player;
        next.active = Session{std::move(economy), std::move(run)};
        if (commit(std::move(next))) {
            market_selection_ = profile_.active->economy.opening_good;
            jacket_selection_ = -1;
            jacket_open_ = false;
            jacket_focused_ = false;
            jacket_slide_.reset();
            message_ = "Buy low, fly, and sell high";
            page_ = Page::Market;
        }
    } catch (const std::exception& error) {
        LOG_ERROR("Could not start AI Wars: {}", error.what());
        message_ = "Check economy.json (see log)";
        page_ = Page::Name;
    }
}

// Apply travel to a draft profile and persist it before the live session advances.
bool GameUi::commit_flight() {
    Profile next = profile_;
    return jet(*next.active, destination_) && commit(std::move(next));
}

// Show unresolved news or offers first, then any encounter and finally the market.
void GameUi::show_arrival() {
    const auto& run = profile_.active->run;
    message_.clear();
    encounter_cargo_page_ = 0;
    arrival_news_ = run.pending_news;
    animate_scene_ = run.pending_news || run.pending_jacket >= 0;
    if (run.pending_news) {
        page_ = Page::News;
    } else if (run.pending_jacket >= 0) {
        page_ = Page::Offer;
    } else if (run.encounter.type == EncounterType::Officer) {
        page_ = Page::Officer;
    } else if (run.encounter.type == EncounterType::Stash) {
        page_ = Page::Stash;
    } else if (run.encounter.type == EncounterType::Family) {
        page_ = Page::Family;
    } else if (run.encounter.type != EncounterType::None) {
        page_ = Page::Encounter;
    } else {
        page_ = Page::Market;
    }
}

// Acknowledge pending news in the save. Recalled headlines return directly to the market.
void GameUi::leave_arrival() {
    if (!arrival_news_) {
        page_ = Page::Market;
        return;
    }
    Profile next = profile_;
    next.active->run.pending_news = false;
    if (commit(std::move(next))) {
        show_arrival();
    }
}

// Save the final score and clear the active run in the same profile update.
void GameUi::close_run() {
    Profile next = profile_;
    const auto& session = *next.active;
    const Money total = session.run.cash + final_buyout(session);
    record_score(next, {session.run.player, total, session.run.seed});
    next.active.reset();
    if (commit(std::move(next))) {
        begin_ending(total);
    }
}

// Keep the live profile at its last saved state until the proposed update is written.
bool GameUi::commit(Profile next) {
    try {
        store_.save(next);
        profile_ = std::move(next);
        message_.clear();
        return true;
    } catch (const std::exception& error) {
        LOG_ERROR("AI Wars save failed: {}", error.what());
        message_ = "Save failed. Try again.";
        return false;
    }
}

// Get the selected pane's good indices in the profile's saved sort order.
std::vector<int> GameUi::listing(bool jacket) const {
    return market_listing(*profile_.active, profile_.sort_order, jacket);
}

// Resolve the selected good to a row in the current sort order, defaulting to the first row.
int GameUi::selected_row(const std::vector<int>& items, bool jacket) const {
    const int good = jacket ? jacket_selection_ : market_selection_;
    const auto position = std::find(items.begin(), items.end(), good);
    return position == items.end() ? 0 : static_cast<int>(position - items.begin());
}

} // namespace wars
