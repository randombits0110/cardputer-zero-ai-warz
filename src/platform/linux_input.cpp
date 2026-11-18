// Routes CardputerZero keyboard events, including Escape holds, to the game UI.

#include "linux_input.h"

#include <array>
#include <cstdint>
#include <cstring>

#if !USE_DESKTOP
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace platform {
namespace {

bool escape_down = false;
uint32_t press_started_at = 0;
bool long_press_sent = false;
lv_timer_t* long_press_timer = nullptr;
KeyReleaseListener key_release_listener = nullptr;
void* key_release_listener_user_data = nullptr;
GlobalKeyListener global_key_listener = nullptr;
void* global_key_listener_user_data = nullptr;

constexpr uint32_t kLongPressMs = 700;

#if !USE_DESKTOP
struct EvdevKeypad {
    int fd{-1};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
    bool router_event_pending{false};
    uint32_t router_key{0};
    bool router_pressed{false};
    unsigned shift{0};
    bool caps{false};
    std::array<std::uint32_t, KEY_MAX + 1> held{};
};
#endif

// Send a single long-press notification once Escape has been held for the threshold.
void long_press_timer_cb(lv_timer_t*) {
    if (!escape_down || long_press_sent || lv_tick_elaps(press_started_at) < kLongPressMs) {
        return;
    }

    long_press_sent = true;
    if (global_key_listener) {
        global_key_listener(LV_KEY_ESC, true, global_key_listener_user_data);
    }
}

// Create or restart the Escape hold timer on demand.
void start_escape_timer() {
    if (!long_press_timer) {
        long_press_timer = lv_timer_create(long_press_timer_cb, 40, nullptr);
    }
    if (long_press_timer) {
        lv_timer_resume(long_press_timer);
        lv_timer_reset(long_press_timer);
    }
}

// Track Escape press duration and forward key-down and key-up events to their listeners.
void route_key_state(uint32_t key, bool pressed) {
    if (!key) return;
    if (key == LV_KEY_ESC) {
        if (escape_down == pressed) return;
        escape_down = pressed;
        if (pressed) {
            press_started_at = lv_tick_get();
            long_press_sent = false;
            start_escape_timer();
        } else if (long_press_timer) {
            lv_timer_pause(long_press_timer);
        }
    }
    if (pressed && global_key_listener) {
        global_key_listener(key, false, global_key_listener_user_data);
    } else if (!pressed && key_release_listener) {
        key_release_listener(key, key_release_listener_user_data);
    }
}

// Read the active LVGL input device and forward its key event to the shared router.
void key_event_cb(lv_event_t* event) {
    LV_UNUSED(event);

    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }

#if !USE_DESKTOP
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (keypad) {
        // LVGL emits LV_EVENT_KEY on every poll, even when evdev had no new event.
        if (!keypad->router_event_pending) {
            return;
        }

        keypad->router_event_pending = false;
        const auto key = keypad->router_key;
        const bool pressed = keypad->router_pressed;

        route_key_state(key, pressed);
        return;
    }
#endif

    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    route_key_state(key, pressed);
}

#if !USE_DESKTOP
// Translate supported evdev codes into text characters and application or LVGL keys.
uint32_t map_evdev_key(uint16_t code, bool shift, bool caps) {
    if (code >= KEY_1 && code <= KEY_0) {
        constexpr char digits[] = "1234567890";
        return digits[code - KEY_1];
    }
    char letter = 0;
    if (code >= KEY_Q && code <= KEY_P) letter = "qwertyuiop"[code - KEY_Q];
    if (code >= KEY_A && code <= KEY_L) letter = "asdfghjkl"[code - KEY_A];
    if (code >= KEY_Z && code <= KEY_M) letter = "zxcvbnm"[code - KEY_Z];
    if (letter) return shift != caps ? letter - 'a' + 'A' : letter;
    switch (code) {
        case KEY_ESC:
            return LV_KEY_ESC;
        case KEY_ENTER:
        case KEY_KPENTER:
            return LV_KEY_ENTER;
        case KEY_TAB:
            return kKeyTab;
        case KEY_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case KEY_DELETE:
            return LV_KEY_DEL;
        case KEY_UP:
            return LV_KEY_UP;
        case KEY_DOWN:
            return LV_KEY_DOWN;
        case KEY_LEFT:
            return LV_KEY_LEFT;
        case KEY_RIGHT:
            return LV_KEY_RIGHT;
        case KEY_MINUS:
            return shift ? '_' : '-';
        case KEY_SPACE:
            return ' ';
        case KEY_SYSRQ:
            return kKeyPrintScreen;
        default:
            return 0;
    }
}

// Check for a letter, a digit, and Enter to distinguish usable keyboards from other devices.
bool has_nav_keys(int fd) {
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    // Look up an evdev key code in the packed capability bitmap.
    auto has_key = [&](int code) {
        const auto bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
        return (key_bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
    };

    return has_key(KEY_A) && has_key(KEY_1) && has_key(KEY_ENTER);
}

// Read one mapped transition per callback and ask LVGL to keep reading until the queue is drained.
void evdev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!keypad) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event input{};
    while (read(keypad->fd, &input, sizeof(input)) == sizeof(input)) {
        if (input.type != EV_KEY || input.value == 2 || input.code > KEY_MAX) {
            continue;
        }
        if (input.code == KEY_LEFTSHIFT || input.code == KEY_RIGHTSHIFT) {
            const unsigned mask = input.code == KEY_LEFTSHIFT ? 1U : 2U;
            keypad->shift = input.value ? keypad->shift | mask : keypad->shift & ~mask;
            continue;
        }
        if (input.code == KEY_CAPSLOCK) {
            if (input.value) keypad->caps = !keypad->caps;
            continue;
        }
        // Use the press-time character on release because Shift or Caps Lock may have since changed.
        auto& held = keypad->held[input.code];
        const auto key = input.value ? map_evdev_key(input.code, keypad->shift != 0, keypad->caps) : held;
        held = input.value ? key : 0;
        if (!key) {
            continue;
        }

        keypad->key = key;
        keypad->state = input.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        keypad->router_event_pending = true;
        keypad->router_key = key;
        keypad->router_pressed = keypad->state == LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        break;
    }

    data->key = keypad->key;
    data->state = keypad->state;
}

// Close the evdev file descriptor and free its state when LVGL deletes the keypad.
void evdev_delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (!keypad) {
        return;
    }

    if (keypad->fd >= 0) {
        close(keypad->fd);
    }
    delete keypad;
}

// Transfer ownership of the file descriptor to an LVGL keypad and close it if setup fails.
lv_indev_t* create_keypad_from_fd(int fd) {
    auto* keypad = new EvdevKeypad;
    keypad->fd = fd;

    auto* indev = lv_indev_create();
    if (!indev) {
        delete keypad;
        close(fd);
        return nullptr;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, evdev_read_cb);
    lv_indev_set_driver_data(indev, keypad);
    lv_indev_add_event_cb(indev, evdev_delete_cb, LV_EVENT_DELETE, nullptr);
    attach_key_router(indev);
    return indev;
}

// Open a readable keyboard in nonblocking mode so input polls can return immediately.
lv_indev_t* try_create_keypad(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }

    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LV_LOG_WARN("failed to open input device %s: %s", path, strerror(errno));
        return nullptr;
    }

    if (!has_nav_keys(fd)) {
        close(fd);
        return nullptr;
    }

    LV_LOG_INFO("using evdev key input %s", path);
    return create_keypad_from_fd(fd);
}

// Try the configured path, the built-in keyboard paths, then compatible event devices.
bool discover_keypads(lv_display_t* display) {
    const char* configured_device = APP_KEY_INPUT_DEVICE;
    if (configured_device[0] != '\0') {
        auto* indev = try_create_keypad(configured_device);
        if (indev) {
            lv_indev_set_display(indev, display);
        }
        return indev != nullptr;
    }

    for (const char* path : {"/dev/input/cardputer-zero-internal", "/dev/input/by-path/platform-3f804000.i2c-event"}) {
        if (access(path, R_OK) == 0) {
            if (auto* indev = try_create_keypad(path)) {
                lv_indev_set_display(indev, display);
                return true;
            }
        }
    }

    auto* dir = opendir("/dev/input");
    if (!dir) {
        LV_LOG_WARN("failed to open /dev/input: %s", strerror(errno));
        return false;
    }

    bool found = false;
    while (auto* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        std::string path = "/dev/input/";
        path += entry->d_name;
        auto* indev = try_create_keypad(path.c_str());
        if (indev) {
            lv_indev_set_display(indev, display);
            found = true;
        }
    }

    closedir(dir);
    return found;
}
#endif

} // namespace

// Start native keyboard discovery for the Linux display.
bool init_key_input(lv_display_t* display) {
#if !USE_DESKTOP
    return discover_keypads(display);
#else
    LV_UNUSED(display);
    return true;
#endif
}

// Subscribe the router to key events on a valid LVGL keypad.
void attach_key_router(lv_indev_t* indev) {
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) {
        return;
    }

    lv_indev_add_event_cb(indev, key_event_cb, LV_EVENT_KEY, nullptr);
}

// Replace the current release callback and store its context.
void set_key_release_listener(KeyReleaseListener listener, void* user_data) {
    key_release_listener = listener;
    key_release_listener_user_data = user_data;
}

// Clear the registered release listener only when the caller still owns it.
void clear_key_release_listener(KeyReleaseListener listener, void* user_data) {
    if (key_release_listener == listener && key_release_listener_user_data == user_data) {
        key_release_listener = nullptr;
        key_release_listener_user_data = nullptr;
    }
}

// Replace the callback used for action keys and the Escape hold.
void set_global_key_listener(GlobalKeyListener listener, void* user_data) {
    global_key_listener = listener;
    global_key_listener_user_data = user_data;
}

// Clear the registered global listener only when the caller still owns it.
void clear_global_key_listener(GlobalKeyListener listener, void* user_data) {
    if (global_key_listener == listener && global_key_listener_user_data == user_data) {
        global_key_listener = nullptr;
        global_key_listener_user_data = nullptr;
    }
}

} // namespace platform
