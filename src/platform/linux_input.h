// Declares the LVGL keypad router and listeners for global key presses and releases.

#pragma once

#include "lvgl.h"

#include <cstdint>

namespace platform {

constexpr uint32_t kSpecialKeyBase = 0x200000U;
constexpr uint32_t kKeyPrintScreen = kSpecialKeyBase | 18U;
constexpr uint32_t kKeyTab = kSpecialKeyBase | 19U;

using KeyReleaseListener = void (*)(uint32_t key, void* user_data);
using GlobalKeyListener = void (*)(uint32_t key, bool long_pressed, void* user_data);

// Bind the detected Linux keypad or keypads to the game display.
bool init_key_input(lv_display_t* display);
// Connect a keypad input device to the shared key router.
void attach_key_router(lv_indev_t* indev);
// Register the release callback and the context passed to it.
void set_key_release_listener(KeyReleaseListener listener, void* user_data);
// Remove the release callback if both the callback and its context match.
void clear_key_release_listener(KeyReleaseListener listener, void* user_data);
// Register the callback for key presses and the Escape hold event.
void set_global_key_listener(GlobalKeyListener listener, void* user_data);
// Remove the global callback if both the callback and its context match.
void clear_global_key_listener(GlobalKeyListener listener, void* user_data);

} // namespace platform
