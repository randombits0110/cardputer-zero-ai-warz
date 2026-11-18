/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Entry point for the AI Wars executable.

#include "app.h"

// Run the application and pass its exit status back to the operating system.
int main() {
    app::Application application;
    return application.run();
}
