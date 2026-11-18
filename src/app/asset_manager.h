/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Declares asset lookup, FreeType font ownership, and PNG decoding into LVGL draw buffers.

#pragma once

#include "lvgl.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app {

enum class StandardFontWeight {
    Regular,
    Bold,
};

// Decode a PNG into a caller-owned buffer with matching dimensions. Return false on failure.
bool read_screen_png(const std::filesystem::path& path, lv_draw_buf_t& buffer);

class AssetManager {
public:
    // Collect search roots for the given app name and optional build-time asset location.
    explicit AssetManager(std::string app_name = default_app_name(),
                          std::filesystem::path cmake_assets_root = default_cmake_assets_root());
    // Release FreeType fonts owned by this manager.
    ~AssetManager();

    // Keep font ownership unique to each manager.
    AssetManager(const AssetManager&) = delete;
    // Prevent copying another manager's font cache.
    AssetManager& operator=(const AssetManager&) = delete;

    // Return the application name configured at build time.
    static std::string default_app_name();
    // Return the optional asset root configured at build time.
    static std::filesystem::path default_cmake_assets_root();

    // Expose the search roots in priority order.
    const std::vector<std::filesystem::path>& roots() const;
    // Resolve a relative path that stays within an asset root.
    std::filesystem::path resolve(const std::filesystem::path& relative_path) const;
    // Look up a font under the asset roots' fonts directory.
    std::filesystem::path resolve_font(const std::filesystem::path& file_name) const;

    // Load or reuse a font at the given pixel size. The manager owns the returned font.
    lv_font_t* load_font(const std::filesystem::path& file_name, uint32_t size) const;
    // Load the system UI font and its fallback on CardputerZero, if available.
    lv_font_t* load_standard_font(uint32_t size,
                                  StandardFontWeight weight = StandardFontWeight::Bold);

private:
    struct LoadedFont;

    // Append an existing, unique root to the search list.
    void add_root_if_valid(std::filesystem::path path);

    std::vector<std::filesystem::path> roots_;
    mutable std::vector<std::unique_ptr<LoadedFont>> loaded_fonts_;
};

} // namespace app
