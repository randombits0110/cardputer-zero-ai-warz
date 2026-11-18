// Declares the LVGL game UI, its screen state, and the input and animation methods behind it.

#pragma once

#include "intro_music.h"
#include "profile_store.h"

#include "lvgl.h"

#include <array>
#include <deque>
#include <initializer_list>
#include <memory>
#include <vector>

namespace app {
class AssetManager;
}

namespace wars {

class GameUi {
public:
    // Create the UI for a loaded profile using the supplied economy file and shared assets.
    GameUi(ProfileStore& store, Profile profile, std::filesystem::path economy_file,
           const app::AssetManager& assets, ProfileRecovery recovery = ProfileRecovery::None);
    // Detach input listeners and remove the screen's widgets.
    ~GameUi();

    // Handle queued input and animations. Return false when the application should exit.
    bool poll();

private:
    enum class Page {
        Intro, Home, Help, Name, Replace, Market, Trade, Jet, News, Offer, Encounter, Officer, Stash, Family,
        Close, Ending, Scores, Share,
    };

    struct Action {
        const char* label;
        std::uint32_t key;
    };

    // Dispatch a key or synthetic button action to the active page or scene.
    void handle_key(std::uint32_t key);
    // Handle market and jacket navigation, sorting, and shortcuts.
    void handle_market(std::uint32_t key);
    // Edit or confirm the current order quantity.
    void handle_trade(std::uint32_t key);
    // Handle destination selection and flight confirmation.
    void handle_jet(std::uint32_t key);
    // Handle cargo paging and dismissal on the generic encounter screen.
    void handle_encounter(std::uint32_t key);
    // Handle score selection and requests to share a score.
    void handle_scores(std::uint32_t key);
    // Go back or dismiss the current page according to its Escape action.
    void back();
    // Open name entry with the last used player name.
    void enter_name();
    // Try to create and save a new run from the current economy file.
    void start();
    // Start destination artwork or advance directly if the image is unavailable.
    void begin_flight();
    // Try to apply and save travel to the selected city.
    bool commit_flight();
    // Advance the slide, landing hold, or fade for the active flight.
    void update_flight();
    // Release the flight canvas and reset the animation state.
    void clear_flight();
    // Complete or skip the flight animation and show the destination.
    void finish_flight();
    // Select the appropriate pending arrival screen or the market.
    void show_arrival();
    // Dismiss the headline and acknowledge it if the run was waiting for it.
    void leave_arrival();
    // Settle the final buyout, record the score, and close the saved run.
    void close_run();
    // Open a buy or sell quantity prompt for the focused good.
    void open_trade(bool buy);
    // Publish a new in-memory profile only after it has been saved.
    bool commit(Profile next);
    // Return good indices for the market or jacket in the chosen sort order.
    std::vector<int> listing(bool jacket) const;
    // Find the pane's retained selection within a sorted list of goods.
    int selected_row(const std::vector<int>& items, bool jacket) const;

    // Rebuild the LVGL widget tree for the current page.
    void render();
    // Prepare a valid title image and its shuffled reveal tiles, if available.
    bool load_intro(const app::AssetManager& assets);
    // Create the title canvas and start prompt.
    void draw_intro();
    // Reveal more title image tiles based on elapsed time.
    void update_intro();
    // Choose main menu artwork and optionally prepare a cover to fade out.
    void load_home_artwork(bool reveal);
    // Draw the main menu with a Continue action when a run is saved.
    void draw_home();
    // Move through the guide while keeping the page index in range.
    void move_help(int direction);
    // Draw the current guide page and navigation controls.
    void draw_help();
    // Advance the fade that uncovers the main menu.
    void update_home_reveal();
    // Draw the name field or confirmation to replace a saved run.
    void draw_name();
    // Draw the city market and its sliding jacket inventory.
    void draw_market();
    // Draw market shortcuts and status appropriate to the current day and pane.
    void draw_market_actions(bool last_day);
    // Change jacket visibility and move focus to the pane being shown.
    void toggle_jacket();
    // Return the jacket tray's current horizontal position in pixels.
    int jacket_position() const;
    // Update the pane widths and tray position during animation.
    void update_jacket_slide();
    // Draw order totals, the lot limit, and the confirmation controls.
    void draw_trade();
    // Draw the destination grid and the selected city's hint.
    void draw_jet();
    // Attach an image buffer to a canvas. Keep the buffer alive until the canvas is deleted.
    lv_obj_t* draw_scene_image(lv_draw_buf_t& image, int x, lv_obj_t* parent = nullptr);
    // Place the flight artwork at its current horizontal position.
    void draw_flight(int x);
    // Draw the last headline, its effect, and the prompt to continue.
    void draw_news();
    void draw_ai_wars_news();
    // Draw the courier jacket offer with its price and capacity.
    void draw_offer();
    // Draw a generic event summary or dispatch to the Black Hat scene.
    void draw_encounter();
    // Set up the animated equipment vendor encounter.
    void draw_black_hat();
    // Advance the vendor scene through its reveal and dismissal animations.
    void update_black_hat();
    // Process a vendor purchase or pass after the options become available.
    void handle_black_hat(std::uint32_t key);
    // Begin fading the completed vendor scene over the updated market.
    void fade_black_hat();
    // Remove the vendor overlay after its fade finishes or is skipped.
    void finish_black_hat();
    // Start the officer scene and its opening dialogue.
    void draw_officer();
    // Advance the officer scene's animation or typewritten dialogue.
    void update_officer();
    // Route an officer scene key according to its current phase.
    void handle_officer(std::uint32_t key);
    // Show abandoned stash artwork and newly found cargo.
    void draw_stash();
    // Show the family encounter with matching artwork when available.
    void draw_family();
    // Set up an illustrated story with a typewritten dialogue panel.
    void draw_story(std::string line, lv_obj_t* parent = nullptr);
    // Advance the story's arrival slide, dialogue, or dismissal fade.
    void update_story();
    // Finish typing the day 25 headline or dismiss a fully revealed story.
    void continue_story();
    // Show the score breakdown and prompt the player to settle the run.
    void draw_close();
    void begin_ending(Money score);
    void draw_ending();
    void update_ending();
    void advance_ending();
    // Draw the leaderboard.
    void draw_scores();
    // Draw the selected score's scannable QR code.
    void draw_share();
    // Draw the active city's name, day, cash, and used jacket slots.
    void hud(const lv_font_t* font = &lv_font_montserrat_12, bool market_colors = false);
    // Draw a page heading across the top of the display.
    void title(const std::string& text);
    // Draw the bottom status line and any available actions.
    void footer(const std::string& text, std::initializer_list<Action> actions);
    // Draw a row of equally sized buttons for a nonempty action list.
    void draw_action_bar(std::initializer_list<Action> actions, bool market_colors = false);
    // Add a positioned text label with wrapping or single-line truncation.
    lv_obj_t* label(const std::string& text, int x, int y, int width,
                    const lv_font_t* font, std::uint32_t color,
                    lv_text_align_t align = LV_TEXT_ALIGN_LEFT, bool wrap = false,
                    lv_obj_t* parent = nullptr);
    // Create a plain rectangle under the supplied parent or the active screen.
    lv_obj_t* box(int x, int y, int width, int height, std::uint32_t color,
                  lv_obj_t* parent = nullptr);
    // Build a labeled button and bind its key action when enabled.
    void button(int x, int y, int width, int height, const std::string& text,
                std::uint32_t key, bool selected = false, bool enabled = true, lv_obj_t* parent = nullptr,
                const lv_font_t* font = nullptr, bool market_colors = false);
    // Make a widget enqueue the supplied key when clicked.
    void clickable(lv_obj_t* object, std::uint32_t key);

    // Receive a keypad press or a timed Escape hold from the input router.
    static void key_pressed(std::uint32_t key, bool long_press, void* user);
    // Turn a short Escape release into a navigation action.
    static void key_released(std::uint32_t key, void* user);
    // Translate an LVGL click into the widget's associated action key.
    static void click(lv_event_t* event);

    static constexpr auto* market_font = &lv_font_montserrat_14;
    static constexpr int rows_per_page = 5;
    static constexpr std::uint32_t pane_row_key = 0x210000;
    static constexpr std::uint32_t jet_city_key = 0x210100;
    static constexpr int encounter_rows_per_page = 3;
    static constexpr std::array<std::uint32_t, 5> home_keys{'c', 'n', 'h', 'g', 's'};
    using DrawBuffer = std::unique_ptr<lv_draw_buf_t, decltype(&lv_draw_buf_destroy)>;
    struct HomeScene {
        DrawBuffer image{nullptr, lv_draw_buf_destroy};
        int menu_x;
        int menu_y;
        DrawBuffer veil{nullptr, lv_draw_buf_destroy};
        lv_obj_t* veil_canvas = nullptr;
        std::uint32_t reveal_started = 0;
        int fade_position = 0;
    };
    enum class EndingPhase { FadeOut, FadeIn, ArtworkHold, Typing, PromptHold, PromptFade, Ready, ArtworkView };
    struct EndingScene {
        EndingPhase phase = EndingPhase::FadeOut;
        std::uint32_t started = 0;
        const char* artwork = nullptr;
        const lv_font_t* font = nullptr;
        std::string text;
        int letter_space = 0;
        DrawBuffer image{nullptr, lv_draw_buf_destroy};
        DrawBuffer veil{nullptr, lv_draw_buf_destroy};
        std::vector<std::uint16_t> pixels;
        std::size_t visible = 0;
        lv_obj_t* veil_canvas = nullptr;
        lv_obj_t* panel = nullptr;
        lv_obj_t* dialogue = nullptr;
        lv_obj_t* prompt = nullptr;
    };
    std::optional<EndingScene> ending_;
    struct JacketSlide {
        int from_x;
        std::uint32_t started;
    };
    struct MarketName {
        lv_obj_t* label = nullptr;
        int reserved_width = 0;
    };
    enum class OfficerPhase { Slide, Blurb, Choice, Notice, RevealResult, HoldResult, Result, Losses, RevealMarket };
    struct OfficerScene {
        std::filesystem::path filename;
        DrawBuffer image{nullptr, lv_draw_buf_destroy};
        DrawBuffer result_image{nullptr, lv_draw_buf_destroy};
        lv_obj_t* canvas = nullptr;
        lv_obj_t* result_canvas = nullptr;
        lv_obj_t* panel = nullptr;
        lv_obj_t* dialogue = nullptr;
        std::string line;
        std::uint32_t started = 0;
        std::size_t visible = 0;
        std::size_t cargo_next = 0;
        int fade_position = 0;
        bool cursor_visible = false;
        OfficerPhase phase = OfficerPhase::Slide;
    };
    // Decode encounter art, optionally substituting a solid background if the file fails to load.
    DrawBuffer load_encounter_image(const std::filesystem::path& path, bool use_placeholder = true);
    // Replace the dialogue panel for the next phase of the officer scene.
    void officer_panel(int panel_height = 74);
    // Start typing a line of dialogue for the supplied officer phase.
    void officer_line(OfficerPhase phase, std::string line);
    // Present attack and escape choices and indicate exploit availability.
    void officer_choices();
    // Show the next page of confiscated goods over the result artwork.
    void officer_losses();
    // Reset the fade timer and discard the panel before revealing the next frame.
    void begin_officer_fade(OfficerPhase phase);
    // Redraw the destination page and release the officer scene.
    void leave_officer(Page page);
    ProfileStore& store_;
    Profile profile_;
    std::filesystem::path economy_file_;
    const app::AssetManager& assets_;
    app::IntroMusic intro_music_;
    std::optional<std::uint32_t> intro_music_queued_at_;
    const lv_font_t* market_hotkey_font_;
    lv_obj_t* screen_;
    DrawBuffer intro_image_{nullptr, lv_draw_buf_destroy};
    DrawBuffer intro_frame_{nullptr, lv_draw_buf_destroy};
    lv_obj_t* intro_canvas_ = nullptr;
    std::vector<int> intro_tiles_;
    std::size_t intro_revealed_ = 0;
    std::uint32_t intro_started_ = 0;
    std::optional<HomeScene> home_;
    DrawBuffer flight_image_{nullptr, lv_draw_buf_destroy};
    lv_obj_t* flight_canvas_ = nullptr;
    std::uint32_t flight_started_ = 0;
    std::optional<std::uint32_t> flight_landed_;
    int flight_fade_position_ = 0;
    std::optional<OfficerScene> officer_;
    struct StoryScene {
        lv_obj_t* layer = nullptr;
        lv_obj_t* canvas = nullptr;
        lv_obj_t* panel = nullptr;
        lv_obj_t* dialogue = nullptr;
        lv_obj_t* prompt = nullptr;
        std::string line;
        std::uint32_t started = 0;
        std::optional<std::uint32_t> landed;
        bool ready = false;
        std::optional<std::uint32_t> fade_started;
        int fade_position = 0;
    };
    std::optional<StoryScene> story_;
    enum class BlackHatPhase { Reveal, Choice, Fade };
    struct BlackHatScene {
        lv_obj_t* layer = nullptr;
        lv_obj_t* canvas = nullptr;
        lv_obj_t* panel = nullptr;
        lv_obj_t* status = nullptr;
        std::uint32_t started = 0;
        int fade_position = 0;
        BlackHatPhase phase = BlackHatPhase::Reveal;
    };
    std::optional<BlackHatScene> black_hat_;
    DrawBuffer encounter_image_{nullptr, lv_draw_buf_destroy};
    Page page_ = Page::Home;
    std::deque<std::uint32_t> keys_;
    bool running_ = true;
    bool escape_held_ = false;
    bool prefilled_name_ = false;
    bool buying_ = true;
    bool typing_quantity_ = false;
    bool arrival_news_ = false;
    bool animate_scene_ = false;
    bool jacket_open_ = false;
    bool jacket_focused_ = false;
    std::optional<JacketSlide> jacket_slide_;
    lv_obj_t* market_pane_ = nullptr;
    lv_obj_t* jacket_tray_ = nullptr;
    std::array<MarketName, rows_per_page> market_names_{};
    int market_selection_ = -1;
    int jacket_selection_ = -1;
    int selected_good_ = 0;
    int destination_ = 0;
    int quantity_ = 1;
    int home_row_ = 0;
    int help_page_ = 0;
    int score_row_ = 0;
    int encounter_cargo_page_ = 0;
    std::string name_;
    std::string message_;
};

} // namespace wars
