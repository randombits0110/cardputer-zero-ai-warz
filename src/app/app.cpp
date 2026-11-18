/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */

// Starts LVGL and the game, then services the UI until the player or operating system requests exit.

#include "app.h"

#include "asset_manager.h"
#include "display_size.h"
#include "game_ui.h"
#include "linux_input.h"
#include "logger.h"

#include <csignal>
#include <cstdlib>
#include <fstream>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#elif APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif

namespace app {
namespace {

volatile std::sig_atomic_t stop_requested = 0;

// Set a signal-safe flag for the main loop to check.
void request_stop(int) {
    stop_requested = 1;
}

// Prefer the environment override, then an installed Windows copy or the configured default.
std::filesystem::path economy_file(const AssetManager& assets) {
    if (const char* path = std::getenv("AI_WARS_ECONOMY"); path && *path) {
        return path;
    }
#if defined(_WIN32) && USE_DESKTOP
    const auto installed = assets.resolve("economy.json");
    if (!installed.empty()) return installed;
#else
    (void)assets;
#endif
    return APP_CONFIG_FILE;
}

#if !USE_DESKTOP && !APP_USE_DRM
// Honor display overrides before looking for the internal ST7789 LCD and its fallback paths.
std::string framebuffer() {
    if (const char* device = std::getenv("LV_LINUX_FBDEV_DEVICE"); device && *device) {
        return device;
    }
    const std::string configured = APP_FRAMEBUFFER_DEVICE;
    if (!configured.empty()) return configured;

    std::ifstream devices("/proc/fb");
    int index = 0;
    std::string name;
    while (devices >> index >> name) {
        if (name.find("st7789") != std::string::npos) {
            return "/dev/fb" + std::to_string(index);
        }
    }
    std::error_code ignored;
    if (std::filesystem::exists("/dev/fb_lcd", ignored)) {
        return "/dev/fb_lcd";
    }
    return "/dev/fb0";
}
#endif

#if !USE_DESKTOP
// Open the configured Linux display backend and release its LVGL object if opening fails.
lv_display_t* device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (display && lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) == LV_RESULT_OK) {
        return display;
    }
#else
    const auto device = framebuffer();
    LOG_INFO("Opening CardputerZero framebuffer {}", device);
    auto* display = lv_linux_fbdev_create();
    if (display && lv_linux_fbdev_set_file(display, device.c_str()) == LV_RESULT_OK) {
        return display;
    }
#endif
    if (display) lv_display_delete(display);
    return nullptr;
}
#endif

// Show a startup failure on the active screen before the game UI exists.
void show_startup_error(const char* message) {
    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101922), 0);
    auto* label = lv_label_create(screen);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, 290);
    lv_label_set_text(label, message);
    lv_obj_center(label);
}

} // namespace

// Validate the display and keep the profile lock alive for the duration of the UI loop.
int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("ai-wars");
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    lv_init();

    AssetManager assets;
#if USE_DESKTOP
    lv_subject_t dark_shell;
    lv_subject_init_int(&dark_shell, 1);
    DesktopSimulatorFrame simulator(assets);
    simulator.bind_dark_mode(&dark_shell);
    auto* display = simulator.display();
#else
    auto* display = device_display();
#endif
    if (!display) {
        LOG_ERROR("Could not initialize the AI Wars display. Check the display device and its permissions");
        return 1;
    }
    if (lv_display_get_horizontal_resolution(display) != platform::screen_width
        || lv_display_get_vertical_resolution(display) != platform::screen_height) {
        LOG_ERROR("The selected display is not the 320x170 CardputerZero LCD");
        return 1;
    }
#if USE_DESKTOP && LV_USE_SYSMON && LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(display);
#endif
    // Check for a stop signal and pump the simulator's window events when present.
    const auto process_events = [&]() {
#if USE_DESKTOP
        return !stop_requested && simulator.process_events();
#else
        return !stop_requested;
#endif
    };
    // Give the player time to read the error by keeping LVGL running for up to four seconds.
    const auto startup_error = [&](const char* message, const std::exception& error) {
        LOG_ERROR("AI Wars startup failed: {}", error.what());
        show_startup_error(message);
        const auto started = lv_tick_get();
        while (process_events() && lv_tick_elaps(started) < 4000) {
            lv_timer_handler();
            lv_delay_ms(10);
        }
        return 1;
    };
#if !USE_DESKTOP
    if (!platform::init_key_input(display)) {
        return startup_error("AI Wars could not open the keyboard.\nCheck input device permissions.",
                             std::runtime_error("No readable CardputerZero keyboard"));
    }
#endif
    try {
        wars::ProfileStore store(wars::default_state_directory());
        wars::ProfileRecovery recovery;
        auto profile = store.load(&recovery);
        wars::GameUi ui(store, std::move(profile), economy_file(assets), assets, recovery);
        LOG_INFO("AI Wars ready at 320x170");
        while (process_events() && ui.poll()) {
            lv_timer_handler();
            lv_delay_ms(5);
        }
        return 0;
    } catch (const wars::ProfileInUse& error) {
        return startup_error("AI Wars is already open.\nClose the other copy and try again.", error);
    } catch (const std::exception& error) {
        return startup_error("AI Wars could not open its profile.\nCheck the application log.", error);
    }
}

} // namespace app
