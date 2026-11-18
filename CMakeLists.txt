#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
#

# Builds AI Wars and configures the CardputerZero Debian package.

cmake_minimum_required(VERSION 3.31)

project(ai-wars VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# CMake configure options

set(USE_DESKTOP OFF CACHE BOOL "Build the local desktop emulator target with LVGL SDL support")
set(APP_NAME "${PROJECT_NAME}" CACHE STRING "Application name used for installed asset lookup")
set(APP_ASSETS_ROOT "" CACHE PATH "Optional runtime assets root. Expected subdirectories include fonts/")
set(APP_CONFIG_FILE "" CACHE FILEPATH "Economy JSON path")
set(APP_KEY_INPUT_DEVICE "" CACHE STRING "Optional Linux evdev keyboard/keypad device path, e.g. /dev/input/event0")
set(APP_FRAMEBUFFER_DEVICE "" CACHE STRING "Optional framebuffer override; defaults to the internal CardputerZero LCD")
set(APP_DRM_DEVICE "/dev/dri/card0" CACHE STRING "Linux DRM device path used when APP_USE_DRM=ON")
set(APP_DRM_CONNECTOR_ID "-1" CACHE STRING "DRM connector id used when APP_USE_DRM=ON; -1 auto-selects")
set(APP_USE_DRM OFF CACHE BOOL "Enable LVGL Linux DRM/KMS backend for embedded Linux builds")
set(CM0_ALLOW_FETCH_DEPS ON CACHE BOOL "Allow downloading missing CM0 BSP/dependencies during cross builds")
option(AI_WARS_WINDOWS_RELEASE "Build the Windows app for installation outside the source tree" OFF)
# Export compile commands
# Used for IDE IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

set(APP_SOURCE_ASSETS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(APP_SOURCE_ECONOMY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/config/economy.json")
if(NOT USE_DESKTOP OR AI_WARS_WINDOWS_RELEASE)
    set(APP_SOURCE_ASSETS_ROOT "")
    set(APP_SOURCE_ECONOMY_FILE "")
endif()
if(NOT APP_CONFIG_FILE)
    if(USE_DESKTOP)
        set(APP_CONFIG_FILE "${APP_SOURCE_ECONOMY_FILE}")
    else()
        set(APP_CONFIG_FILE "/etc/ai-wars/economy.json")
    endif()
endif()

# Keep local checkout paths out of distributed binaries, including __FILE__ in logs.
if(MSVC)
    add_compile_options("$<$<CONFIG:Release>:/experimental:deterministic>"
                        "$<$<CONFIG:Release>:/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=.>")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("$<$<CONFIG:Release>:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")
endif()

include(FetchContent)

# Use miniaudio's decoder and device APIs without building its examples or extra targets.
FetchContent_Declare(
    miniaudio
    URL https://codeload.github.com/mackron/miniaudio/tar.gz/refs/tags/0.11.26
    URL_HASH SHA256=838ba55a21c9142adf7b76bdc4351e5cd8b7ae128f46b25ec25f94ae9969c600
    SOURCE_SUBDIR cmake/unused
)
FetchContent_MakeAvailable(miniaudio)
add_library(ai-wars_audio STATIC "${miniaudio_SOURCE_DIR}/miniaudio.c")
target_include_directories(ai-wars_audio SYSTEM PUBLIC "${miniaudio_SOURCE_DIR}")
target_compile_definitions(ai-wars_audio PUBLIC MA_NO_ENGINE MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_NULL)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(ai-wars_audio PRIVATE Threads::Threads m ${CMAKE_DL_LIBS})
endif()


# lvgl - main ui framework
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/src/config/lv_conf.h" CACHE PATH "" FORCE)

# Manually download LVGL dependencies
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
endif()
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
set(LVGL_SOURCE_DIR "${lvgl_SOURCE_DIR}")
get_filename_component(LVGL_INCLUDE_PARENT "${LVGL_SOURCE_DIR}" DIRECTORY)

# other misc dependencies
# desktop builds
if(USE_DESKTOP)
    message(STATUS "Configuring DESKTOP build")

    # =========================
    # PNG
    # =========================
    find_package(PNG REQUIRED)

    # =========================
    # JPEG (platform abstraction handled by package manager)
    # =========================
    find_package(JPEG REQUIRED)

    if(TARGET libjpeg-turbo::libjpeg-turbo)
        set(JPEG_TARGET libjpeg-turbo::libjpeg-turbo)
    elseif(TARGET JPEG::JPEG)
        set(JPEG_TARGET JPEG::JPEG)
    else()
        message(FATAL_ERROR "No JPEG provider found")
    endif()

    # =========================
    # Freetype
    # =========================

    find_package(Freetype REQUIRED)
    # =========================
    # SDL2
    # =========================
    find_package(SDL2 CONFIG REQUIRED)

    # =========================
    # fmt
    # =========================
    find_package(fmt CONFIG REQUIRED)

    # =========================
    # ZLIB
    # =========================
    find_package(ZLIB REQUIRED)

    # =========================
    # Link safely
    # =========================
    set(LVGL_DESKTOP_DEPS
        ZLIB::ZLIB
        PNG::PNG
        ${JPEG_TARGET}
        Freetype::Freetype
        SDL2::SDL2
    )

    foreach(dep IN LISTS LVGL_DESKTOP_DEPS)
        if(TARGET ${dep})
            target_link_libraries(lvgl PUBLIC ${dep})
        endif()
    endforeach()

    if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
    endif()

    target_compile_definitions(lvgl PUBLIC USE_DESKTOP=1)
    
    if(MSVC)
        target_compile_options(lvgl PUBLIC /utf-8)
    endif()

# Cross builds
# This search individual dependencies from the BSP folder
# and add to the target
# this may not be the good way...
else()
    message(STATUS "Configuring CM0/aarch64 Linux build")

    if(NOT CM0_SDK_ROOT OR NOT EXISTS "${CM0_SDK_ROOT}")
        message(FATAL_ERROR
            "CM0 SDK sysroot not found or invalid.\n"
            "The cp0 toolchain should download the BSP before project() runs, "
            "or configure with -DCM0_SDK_ROOT=<path_to_sdk_sysroot>."
        )
    endif()

    if(CMAKE_SYSROOT)
        set(CM0_SYSROOT "${CMAKE_SYSROOT}")
    else()
        set(CM0_SYSROOT "${CM0_SDK_ROOT}")
    endif()
    if(NOT CM0_MULTIARCH)
        set(CM0_MULTIARCH "aarch64-linux-gnu")
    endif()
    message(STATUS "Using CM0 SDK root: ${CM0_SDK_ROOT}")
    message(STATUS "Using CM0 dependency root: ${CM0_SYSROOT}")

    set(CM0_INCLUDE_HINTS
        "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/include"
        "${CM0_SYSROOT}/usr/local/include"
        "${CM0_SYSROOT}/include/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/include"
    )
    set(CM0_LIBRARY_HINTS
        "${CM0_SYSROOT}/usr/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/lib"
        "${CM0_SYSROOT}/usr/local/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/usr/local/lib"
        "${CM0_SYSROOT}/lib/${CM0_MULTIARCH}"
        "${CM0_SYSROOT}/lib"
    )

    # Search only the provided BSP include paths and publish the result to the caller.
    function(cm0_find_header out_var header_name)
        find_path(${out_var}
            NAMES ${header_name}
            HINTS ${ARGN}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    # Look up any of the candidate library names inside the BSP library paths.
    function(cm0_find_library out_var)
        find_library(${out_var}
            NAMES ${ARGN}
            HINTS ${CM0_LIBRARY_HINTS}
            NO_DEFAULT_PATH
        )
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endfunction()

    # Wrap a BSP library and its headers in a reusable CMake target.
    function(cm0_add_imported_library target_name library_path include_dirs)
        if(NOT TARGET ${target_name})
            add_library(${target_name} UNKNOWN IMPORTED)
            set_target_properties(${target_name} PROPERTIES
                IMPORTED_LOCATION "${library_path}"
                INTERFACE_INCLUDE_DIRECTORIES "${include_dirs}"
            )
        endif()
    endfunction()

    # Display backend, DRM is optional. All lookups stay inside the BSP sysroot.
    if(APP_USE_DRM)
        cm0_find_header(DRM_XF86_INCLUDE_DIR xf86drm.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_XF86_MODE_INCLUDE_DIR xf86drmMode.h
            "${CM0_SYSROOT}/usr/include"
            "${CM0_SYSROOT}/usr/include/libdrm"
        )
        cm0_find_header(DRM_MODE_INCLUDE_DIR drm.h
            "${CM0_SYSROOT}/usr/include/drm"
            "${CM0_SYSROOT}/usr/include/libdrm"
            "${CM0_SYSROOT}/usr/include"
        )
        cm0_find_library(DRM_LIBRARY drm drm.so.2)

        if(NOT DRM_XF86_INCLUDE_DIR
           OR NOT DRM_XF86_MODE_INCLUDE_DIR
           OR NOT DRM_MODE_INCLUDE_DIR
           OR NOT DRM_LIBRARY)
            message(FATAL_ERROR
                "APP_USE_DRM=ON but libdrm headers/library were not found in BSP sysroot.\n"
                "Provide them in ${CM0_SYSROOT} or disable with -DAPP_USE_DRM=OFF."
            )
        endif()
        cm0_add_imported_library(CM0::DRM "${DRM_LIBRARY}"
            "${DRM_XF86_INCLUDE_DIR};${DRM_XF86_MODE_INCLUDE_DIR};${DRM_MODE_INCLUDE_DIR}"
        )
        message(STATUS "CM0 DRM display support enabled")
    endif()

    cm0_find_header(FREETYPE_INCLUDE_DIR ft2build.h
        "${CM0_SYSROOT}/usr/include/freetype2"
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FREETYPE_LIBRARY freetype)
    if(NOT FREETYPE_INCLUDE_DIR OR NOT FREETYPE_LIBRARY)
        message(FATAL_ERROR "Missing Freetype header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::Freetype "${FREETYPE_LIBRARY}" "${FREETYPE_INCLUDE_DIR}")

    cm0_find_header(JPEG_INCLUDE_DIR jpeglib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(JPEG_LIBRARY jpeg)
    if(NOT JPEG_INCLUDE_DIR OR NOT JPEG_LIBRARY)
        message(FATAL_ERROR "Missing JPEG header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(CM0::JPEG "${JPEG_LIBRARY}" "${JPEG_INCLUDE_DIR}")

    cm0_find_header(ZLIB_INCLUDE_DIR zlib.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(ZLIB_LIBRARY z)
    if(NOT ZLIB_INCLUDE_DIR OR NOT ZLIB_LIBRARY)
        message(FATAL_ERROR "Missing ZLIB header/library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    cm0_add_imported_library(ZLIB::ZLIB "${ZLIB_LIBRARY}" "${ZLIB_INCLUDE_DIR}")

    # Use a private patched decoder for game assets, independent of the device's system libpng.
    set(ZLIB_LIBRARY_RELEASE "${ZLIB_LIBRARY}")
    set(PNG_SHARED OFF)
    set(PNG_STATIC ON)
    set(PNG_TESTS OFF)
    set(PNG_TOOLS OFF)
    set(PNG_PREFIX "ai_wars_")
    FetchContent_Declare(
        game_libpng
        GIT_REPOSITORY https://github.com/pnggroup/libpng.git
        GIT_TAG cd952f49f95bb27154ae77dbb103032d95f6e580 # 1.6.59
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(game_libpng)

    set(CM0_MULTIARCH_INCLUDE_DIRS)
    foreach(CM0_INCLUDE_DIR IN ITEMS
            "${CM0_SYSROOT}/usr/include/${CM0_MULTIARCH}"
            "${CM0_SYSROOT}/include/${CM0_MULTIARCH}")
        if(EXISTS "${CM0_INCLUDE_DIR}/bits/timesize.h" OR EXISTS "${CM0_INCLUDE_DIR}/bits/wordsize.h")
            list(APPEND CM0_MULTIARCH_INCLUDE_DIRS "${CM0_INCLUDE_DIR}")
            message(STATUS "Found multiarch include: ${CM0_INCLUDE_DIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES CM0_MULTIARCH_INCLUDE_DIRS)

    cm0_find_header(FMT_INCLUDE_DIR fmt/format.h
        "${CM0_SYSROOT}/usr/include"
    )
    cm0_find_library(FMT_LIBRARY fmt libfmt.so.10 libfmt.so.10.1.0)
    if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
        cm0_add_imported_library(fmt::fmt "${FMT_LIBRARY}" "${FMT_INCLUDE_DIR}")
    elseif(CM0_ALLOW_FETCH_DEPS)
        message(STATUS "Missing fmt in BSP sysroot, fetching from GitHub because CM0_ALLOW_FETCH_DEPS=ON")
        FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt.git
            GIT_TAG        10.1.1
            GIT_SHALLOW    TRUE
        )
        set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules for app packages" FORCE)
        set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
        set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
        FetchContent_MakeAvailable(fmt)
        if(TARGET fmt)
            target_include_directories(fmt SYSTEM BEFORE PRIVATE ${CM0_MULTIARCH_INCLUDE_DIRS})
        endif()
        if(TARGET fmt AND NOT TARGET fmt::fmt)
            add_library(fmt::fmt ALIAS fmt)
        endif()
    else()
        message(FATAL_ERROR
            "Missing fmt header/library in BSP sysroot: ${CM0_SYSROOT}\n"
            "Install/copy fmt into the BSP, or reconfigure with -DCM0_ALLOW_FETCH_DEPS=ON."
        )
    endif()

    set_target_properties(CM0::Freetype PROPERTIES INTERFACE_LINK_LIBRARIES ZLIB::ZLIB)

    cm0_find_library(PTHREAD_LIBRARY pthread libpthread.so.0 libpthread.a)
    cm0_find_library(M_LIBRARY m libm.so libm.so.6 libm.a)
    if(NOT PTHREAD_LIBRARY)
        message(FATAL_ERROR "Missing pthread library in BSP sysroot: ${CM0_SYSROOT}")
    endif()
    if(NOT M_LIBRARY)
        message(FATAL_ERROR "Missing math library in BSP sysroot: ${CM0_SYSROOT}")
    endif()

    set(PLATFORM_INCLUDE_DIR ${CM0_MULTIARCH_INCLUDE_DIRS})
    set(PLATFORM_LIBS ${PTHREAD_LIBRARY} ${M_LIBRARY})

    set(LVGL_INCLUDE_DIR
        ${FREETYPE_INCLUDE_DIR}
        ${JPEG_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
    )
    set(LVGL_LIBS
        CM0::Freetype
        png_static
        CM0::JPEG
        ZLIB::ZLIB
    )
    if(APP_USE_DRM)
        list(APPEND LVGL_INCLUDE_DIR
            ${DRM_XF86_INCLUDE_DIR}
            ${DRM_XF86_MODE_INCLUDE_DIR}
            ${DRM_MODE_INCLUDE_DIR}
        )
        list(APPEND LVGL_LIBS CM0::DRM)
    endif()

    list(REMOVE_DUPLICATES PLATFORM_INCLUDE_DIR)
    list(REMOVE_DUPLICATES PLATFORM_LIBS)
    list(REMOVE_DUPLICATES LVGL_INCLUDE_DIR)
    list(REMOVE_DUPLICATES LVGL_LIBS)

    target_compile_definitions(lvgl
        PUBLIC
            USE_DESKTOP=0
            APP_USE_DRM=$<BOOL:${APP_USE_DRM}>
    )

    # fix: issue of compiling lvgl on gcc >= 15.0
    if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # GCC version is available in CMAKE_C_COMPILER_VERSION
        if (CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 15.0)
            target_compile_options(lvgl PRIVATE -Wno-stringop-overflow)
        endif()
    endif()

    target_include_directories(lvgl
        PUBLIC
            ${LVGL_INCLUDE_DIR}
            ${PLATFORM_INCLUDE_DIR}
    )

    target_link_libraries(lvgl
        PUBLIC
            ${LVGL_LIBS}
            ${PLATFORM_LIBS}
    )

endif()



# ====================================================
# Platform sources
# ====================================================
# Create application platform sources and static library
add_library(${PROJECT_NAME}_platform 
    STATIC
    src/platform/arrival_time.cpp
    src/platform/linux_input.cpp
    src/platform/screenshot_service.cpp
)

target_include_directories(${PROJECT_NAME}_platform
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
)

target_compile_definitions(${PROJECT_NAME}_platform
    PUBLIC
        APP_KEY_INPUT_DEVICE="${APP_KEY_INPUT_DEVICE}"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
)

target_link_libraries(${PROJECT_NAME}_platform
    PUBLIC
        lvgl
)

# ====================================================
# Model sources
# ====================================================
add_library(${PROJECT_NAME}_model
    STATIC
    src/model/economy.cpp
    src/model/game.cpp
    src/model/profile_store.cpp
)

target_include_directories(${PROJECT_NAME}_model
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

# ====================================================
#   Core sources
# - Application lifecycle management
# - UI screen management
# - UI asset management
# - Logger based on fmt
# ====================================================
file(GLOB_RECURSE LOGGER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/logger/*.cpp"
)

add_library(logger STATIC
    ${LOGGER_SOURCES}
)

target_include_directories(logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
    ${PLATFORM_INCLUDE_DIR}
)

target_link_libraries(logger
    PUBLIC
        fmt::fmt
    PRIVATE
        ${PLATFORM_LIBS}
)

add_library(${PROJECT_NAME}_core STATIC
    src/app/app.cpp
    src/app/asset_manager.cpp
    src/app/game_ui.cpp
    src/app/intro_music.cpp
    src/view/ending_scene.cpp
    src/view/flight_transition.cpp
    src/view/game_screens.cpp
    src/view/intro_screen.cpp
    src/view/officer_scene.cpp
)

if(USE_DESKTOP)
    target_sources(${PROJECT_NAME}_core PRIVATE
        src/app/desktop_simulator_frame.cpp
        src/app/desktop_virtual_keypad.cpp
    )
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME}_model PRIVATE /EHsc)
    target_compile_options(${PROJECT_NAME}_core PRIVATE /EHsc)
endif()

target_include_directories(${PROJECT_NAME}_core
    PUBLIC
        ${LVGL_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
        ${CMAKE_CURRENT_SOURCE_DIR}/src/model
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
)

target_compile_definitions(${PROJECT_NAME}_core
    PRIVATE
        APP_SOURCE_ECONOMY_FILE="${APP_SOURCE_ECONOMY_FILE}"
    PUBLIC
        APP_NAME="${APP_NAME}"
        APP_CONFIG_FILE="${APP_CONFIG_FILE}"
        APP_CMAKE_ASSETS_ROOT="${APP_ASSETS_ROOT}"
        APP_SOURCE_ASSETS_ROOT="${APP_SOURCE_ASSETS_ROOT}"
        APP_FRAMEBUFFER_DEVICE="${APP_FRAMEBUFFER_DEVICE}"
        APP_DRM_DEVICE="${APP_DRM_DEVICE}"
        APP_DRM_CONNECTOR_ID=${APP_DRM_CONNECTOR_ID}
)

target_link_libraries(${PROJECT_NAME}_core
    PUBLIC
        lvgl
        logger
        ${PROJECT_NAME}_model
        ${PROJECT_NAME}_platform
    PRIVATE
        ai-wars_audio
)

if(USE_DESKTOP)
    target_link_libraries(${PROJECT_NAME}_core
        PRIVATE
            PNG::PNG
    )
    if(TARGET SDL2::SDL2)
        target_link_libraries(${PROJECT_NAME}_core PRIVATE SDL2::SDL2)
    else()
        target_include_directories(${PROJECT_NAME}_core PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(${PROJECT_NAME}_core PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

# ====================================================
# main
# ====================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
)

if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/PDBALTPATH:ai-wars.pdb>")
endif()

if(NOT USE_DESKTOP)
    set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE)
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_NAME}_platform
        ${PROJECT_NAME}_core
        ${PROJECT_NAME}_model
)

option(AI_WARS_TESTS "Build the desktop regression tests" OFF)
if(USE_DESKTOP AND AI_WARS_TESTS)
    enable_testing()
    add_executable(ai-wars-pricing-test tests/commodity_pricing.cpp)
    target_include_directories(ai-wars-pricing-test PRIVATE third_party)
    target_link_libraries(ai-wars-pricing-test PRIVATE ${PROJECT_NAME}_model)
    add_executable(ai-wars-market-news-test tests/market_wide_news.cpp)
    target_include_directories(ai-wars-market-news-test PRIVATE third_party)
    target_link_libraries(ai-wars-market-news-test PRIVATE ${PROJECT_NAME}_model)
    add_executable(ai-wars-news-skew-test tests/price_news_skew.cpp)
    target_include_directories(ai-wars-news-skew-test PRIVATE third_party)
    target_link_libraries(ai-wars-news-skew-test PRIVATE ${PROJECT_NAME}_model)
    add_executable(ai-wars-flow-test tests/simulator_flow.cpp)
    target_include_directories(ai-wars-flow-test PRIVATE third_party)
    target_link_libraries(ai-wars-flow-test PRIVATE ${PROJECT_NAME}_core ${PROJECT_NAME}_model SDL2::SDL2)
    if(MSVC)
        target_compile_options(ai-wars-pricing-test PRIVATE /EHsc)
        target_compile_options(ai-wars-market-news-test PRIVATE /EHsc)
        target_compile_options(ai-wars-news-skew-test PRIVATE /EHsc)
        target_compile_options(ai-wars-flow-test PRIVATE /EHsc)
    endif()
    add_test(NAME ai-wars-pricing COMMAND ai-wars-pricing-test
        "${CMAKE_CURRENT_SOURCE_DIR}/config/economy.json" "${CMAKE_CURRENT_BINARY_DIR}/pricing-test")
    add_test(NAME ai-wars-market-news COMMAND ai-wars-market-news-test
        "${CMAKE_CURRENT_SOURCE_DIR}/config/economy.json" "${CMAKE_CURRENT_BINARY_DIR}/market-news-test")
    add_test(NAME ai-wars-news-skew COMMAND ai-wars-news-skew-test
        "${CMAKE_CURRENT_SOURCE_DIR}/config/economy.json" "${CMAKE_CURRENT_BINARY_DIR}/news-skew-test")
    add_test(NAME ai-wars-flow COMMAND ai-wars-flow-test
        "${CMAKE_CURRENT_SOURCE_DIR}/config/economy.json" "${CMAKE_CURRENT_BINARY_DIR}/flow-test")
endif()

include(GNUInstallDirs)
install(FILES assets/audio/intro.mp3 assets/audio/intro-LICENSE.txt
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/audio"
)
install(FILES "${miniaudio_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME miniaudio-license.txt)

include(cmake/license-notices.cmake)

if(WIN32 AND USE_DESKTOP)
    include(cmake/windows-package.cmake)
else()
    include(cmake/cm0-package.cmake)
endif()
