#
# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
#
# SPDX-License-Identifier: MIT
# Packaging and install layout for CardputerZero Debian builds.

include(GNUInstallDirs)

set(APP_DISPLAY_NAME "AI Wars" CACHE STRING "Human-readable application name used by launchers and package filename")
set(APP_DEBIAN_REVISION "rev2" CACHE STRING "Debian package revision/vendor suffix")
set(APP_DEBIAN_ARCHITECTURE "arm64" CACHE STRING "Debian package architecture")
set(APP_MAINTAINER "AI Wars local build <local-build@example.invalid>" CACHE STRING "Debian package maintainer")
set(APP_PACKAGE_DESCRIPTION "AI technology trading game for CardputerZero" CACHE STRING "Debian package summary")
set(APP_INSTALL_SYSTEMD_SERVICE OFF CACHE BOOL "Install a systemd service file for embedded deployments")

# Generate APPLaunch metadata in the build tree using the package's configured name and description.
set(APP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/package")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/app.desktop.in"
    "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    @ONLY
)

if(APP_INSTALL_SYSTEMD_SERVICE)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/templates/app.service.in"
        "${APP_GENERATED_DIR}/${PROJECT_NAME}.service"
        @ONLY
    )
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/economy.json"
    DESTINATION "/etc/ai-wars"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/config/economy.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/ai-wars.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/share/images"
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Montserrat-Bold.ttf"
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Montserrat-Medium.ttf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/fonts"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/intro"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/start-screen"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/arrivals-v2"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/endgame"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images"
    FILES_MATCHING PATTERN "*.png"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/encounters/officer-ram-altman"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/encounters/officer-ram-altman-confiscation"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/encounters/black-hat"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/encounters/abandoned-stash"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/encounters/family-events"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images/encounters"
    FILES_MATCHING PATTERN "*.png"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/news/day-25"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images/news"
    FILES_MATCHING PATTERN "*.png"
)

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/encounters/officer-ram-altman-success"
                  "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/encounters/officer-ram-altman-escape"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/${APP_NAME}/images/encounters"
    OPTIONAL FILES_MATCHING PATTERN "*.png"
)

install(FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/applications"
)

if(APP_INSTALL_SYSTEMD_SERVICE)
    install(FILES "${APP_GENERATED_DIR}/${PROJECT_NAME}.service"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system"
    )
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES "${lvgl_SOURCE_DIR}/LICENCE.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "lvgl-license.txt"
)
install(FILES "${game_libpng_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "libpng-license.txt"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/LICENSE.MIT"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    RENAME "nlohmann-json-license.txt"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Montserrat-OFL.txt"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_PACKAGE_NAME "${APP_DISPLAY_NAME}")
set(CPACK_PACKAGE_VENDOR "${APP_MAINTAINER}")
set(CPACK_PACKAGE_CONTACT "${APP_MAINTAINER}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${APP_PACKAGE_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}_${PROJECT_VERSION}-${APP_DEBIAN_REVISION}_${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

string(TOLOWER "${APP_DISPLAY_NAME}" APP_DEBIAN_PACKAGE_NAME)
string(REGEX REPLACE "[^a-z0-9+.-]" "-" APP_DEBIAN_PACKAGE_NAME "${APP_DEBIAN_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_NAME "${APP_DEBIAN_PACKAGE_NAME}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "${APP_DEBIAN_REVISION}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${APP_DEBIAN_ARCHITECTURE}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${APP_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(APP_DEBIAN_DEPENDS "libc6 (>= 2.38), libstdc++6 (>= 12), libgcc-s1, libfreetype6, libpng16-16t64 | libpng16-16, libasound2t64 | libasound2, tzdata")
if(FMT_INCLUDE_DIR AND FMT_LIBRARY)
    string(APPEND APP_DEBIAN_DEPENDS ", libfmt10")
endif()
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${APP_DEBIAN_DEPENDS}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/templates/conffiles.in"
    "${APP_GENERATED_DIR}/conffiles"
    @ONLY
)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${APP_GENERATED_DIR}/conffiles")

include(CPack)
