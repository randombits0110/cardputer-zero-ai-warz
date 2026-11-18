/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Finds installed and development assets and supplies fonts and screen art to LVGL.

#include "asset_manager.h"

#include "logger.h"

#include <png.h>

#if defined(_WIN32) && USE_DESKTOP
#include <SDL_filesystem.h>
#endif

#if LV_USE_FREETYPE
#include "src/libs/freetype/lv_freetype.h"
#endif

#include <algorithm>
#include <fstream>
#include <utility>

#ifndef APP_NAME
#define APP_NAME "ai-wars"
#endif

#ifndef APP_CMAKE_ASSETS_ROOT
#define APP_CMAKE_ASSETS_ROOT ""
#endif

#ifndef APP_SOURCE_ASSETS_ROOT
#define APP_SOURCE_ASSETS_ROOT "assets"
#endif

namespace app {
namespace {

#if !USE_DESKTOP
constexpr const char* kNotoSansCjkRegular =
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
constexpr const char* kNotoSansCjkBold =
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc";
#endif

// Treat filesystem lookup errors as missing paths so the search can continue.
bool path_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Normalize candidate paths for cache and root comparisons, even if canonicalization fails.
std::filesystem::path weakly_canonical_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

#if !USE_DESKTOP
// Choose a bundled Montserrat size to fill missing glyphs in the system font.
const lv_font_t* fallback_font(uint32_t size) {
    if (size <= 10) {
        return &lv_font_montserrat_10;
    }
    if (size <= 12) {
        return &lv_font_montserrat_12;
    }
    if (size <= 14) {
        return &lv_font_montserrat_14;
    }
    if (size <= 16) {
        return &lv_font_montserrat_16;
    }
    if (size <= 18) {
        return &lv_font_montserrat_18;
    }
    if (size <= 20) {
        return &lv_font_montserrat_20;
    }
    if (size <= 24) {
        return &lv_font_montserrat_24;
    }
    return &lv_font_montserrat_28;
}
#endif

} // namespace

// Decode with the draw buffer's stride and channel order so LVGL can display the pixels directly.
bool read_screen_png(const std::filesystem::path& path, lv_draw_buf_t& buffer) {
    constexpr std::uintmax_t max_png_bytes = 512 * 1024;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return false;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > max_png_bytes) return false;

    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (!input.read(bytes.data(), static_cast<std::streamsize>(size))
        || input.peek() != std::char_traits<char>::eof() || input.bad()) return false;

    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    bool decoded = false;
    if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size())
        && image.width == buffer.header.w && image.height == buffer.header.h) {
        image.format = buffer.header.cf == LV_COLOR_FORMAT_ARGB8888 ? PNG_FORMAT_BGRA : PNG_FORMAT_BGR;
        const png_color black{};
        decoded = png_image_finish_read(&image, &black, buffer.data, buffer.header.stride, nullptr) != 0;
    }
    png_image_free(&image);
    if (decoded) lv_draw_buf_flush_cache(&buffer, nullptr);
    return decoded;
}

struct AssetManager::LoadedFont {
    std::filesystem::path path;
    uint32_t size;
    lv_font_t* font;
};

// Register roots in lookup order, ending with the installed Linux data directory.
AssetManager::AssetManager(std::string app_name, std::filesystem::path cmake_assets_root) {
    add_root_if_valid(std::move(cmake_assets_root));
    add_root_if_valid(default_cmake_assets_root());
#if defined(_WIN32) && USE_DESKTOP
    const std::unique_ptr<char, decltype(&SDL_free)> executable_dir(SDL_GetBasePath(), SDL_free);
    if (executable_dir) {
        add_root_if_valid(std::filesystem::u8path(executable_dir.get()) / ".." / "share" / app_name);
    }
#elif !USE_DESKTOP
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) add_root_if_valid(executable.parent_path() / ".." / "share" / app_name);
#endif
    add_root_if_valid(APP_SOURCE_ASSETS_ROOT);
    add_root_if_valid(std::filesystem::path{"/usr/share"} / app_name);
}

// Destroy each cached FreeType font when its asset manager goes out of scope.
AssetManager::~AssetManager() {
#if LV_USE_FREETYPE
    for (auto& loaded_font : loaded_fonts_) {
        if (loaded_font && loaded_font->font) {
            lv_freetype_font_delete(loaded_font->font);
        }
    }

#endif
}

// Use the build's app name for installed asset paths.
std::string AssetManager::default_app_name() {
    return APP_NAME;
}

// Read the asset directory override embedded by CMake.
std::filesystem::path AssetManager::default_cmake_assets_root() {
    return APP_CMAKE_ASSETS_ROOT;
}

// Return the current search list by reference.
const std::vector<std::filesystem::path>& AssetManager::roots() const {
    return roots_;
}

// Use the first existing path in search order, or return an empty path if all lookups fail.
std::filesystem::path AssetManager::resolve(const std::filesystem::path& relative_path) const {
    if (relative_path.empty() || relative_path.has_root_path()
        || std::find(relative_path.begin(), relative_path.end(), "..") != relative_path.end()) return {};

    for (const auto& root : roots_) {
        std::error_code error;
        const auto candidate = std::filesystem::canonical(root / relative_path, error);
        if (error) continue;
        const auto prefix = std::mismatch(root.begin(), root.end(), candidate.begin(), candidate.end());
        if (prefix.first == root.end() && prefix.second != candidate.end()) return candidate;
    }

    return {};
}

// Resolve the font name through the same search order as other assets.
std::filesystem::path AssetManager::resolve_font(const std::filesystem::path& file_name) const {
    if (file_name.is_absolute() && path_exists(file_name)) return file_name;
    return resolve(std::filesystem::path{"fonts"} / file_name);
}

// Cache FreeType fonts by resolved path and size to reuse them across screens.
lv_font_t* AssetManager::load_font(const std::filesystem::path& file_name, uint32_t size) const {
#if LV_USE_FREETYPE
    auto path = resolve_font(file_name);
    if (path.empty()) {
        LOG_WARN("font asset not found: {}", file_name.string());
        return nullptr;
    }

    auto existing = std::find_if(loaded_fonts_.begin(), loaded_fonts_.end(), [&](const auto& loaded_font) {
        return loaded_font && loaded_font->path == path && loaded_font->size == size;
    });
    if (existing != loaded_fonts_.end()) {
        return (*existing)->font;
    }

    lv_font_t* font = lv_freetype_font_create(path.string().c_str(),
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              size,
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!font) {
        LOG_WARN("failed to load freetype font: {}", path.string());
        return nullptr;
    }

    loaded_fonts_.push_back(std::make_unique<LoadedFont>(LoadedFont{path, size, font}));
    LOG_INFO("loaded freetype font: {} ({})", path.string(), size);
    return font;
#else
    LV_UNUSED(file_name);
    LV_UNUSED(size);
    LOG_WARN("FreeType font loading requested but LV_USE_FREETYPE is disabled");
    return nullptr;
#endif
}

// Load the requested Noto Sans CJK weight on the device and attach the bundled fallback.
lv_font_t* AssetManager::load_standard_font(uint32_t size, StandardFontWeight weight) {
#if USE_DESKTOP
    LV_UNUSED(size);
    LV_UNUSED(weight);
    return nullptr;
#else
    const char* path = weight == StandardFontWeight::Regular
                           ? kNotoSansCjkRegular
                           : kNotoSansCjkBold;
    auto* font = load_font(path, size);
    if (font) {
        font->fallback = fallback_font(size);
    }
    return font;
#endif
}

// Normalize and deduplicate available roots while preserving lookup priority.
void AssetManager::add_root_if_valid(std::filesystem::path path) {
    if (path.empty()) {
        return;
    }

    path = weakly_canonical_path(path);
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return;
    }

    auto it = std::find(roots_.begin(), roots_.end(), path);
    if (it == roots_.end()) {
        roots_.push_back(std::move(path));
    }
}

} // namespace app
