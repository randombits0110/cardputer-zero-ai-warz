/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Exposes PNG screenshot capture and the result reported to the game UI.

#pragma once

#include <string>

namespace platform::screenshot {

struct CaptureResult {
    bool success{false};
    std::string path;
    std::string error;
};

// Capture the active LVGL screen and return the saved path or a readable error.
CaptureResult capture_active_screen();

} // namespace platform::screenshot
