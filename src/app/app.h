/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Declares the application entry point used by the executable.

#pragma once

namespace app {

class Application {
public:
    // Initialize the game and return its process exit status when the UI closes or startup fails.
    int run();
};

} // namespace app
