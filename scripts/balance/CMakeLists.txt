cmake_minimum_required(VERSION 3.16)
project(ai_wars_balance LANGUAGES CXX)

# Deliberately independent of the desktop/device build: no SDL, assets, downloads,
# installed packages, or game profiles. Compile the actual current game rules.
get_filename_component(GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(ai-wars-balance
    runner.cpp
    "${GAME_ROOT}/src/model/game.cpp"
    "${GAME_ROOT}/src/model/economy.cpp")
target_compile_features(ai-wars-balance PRIVATE cxx_std_17)
target_include_directories(ai-wars-balance PRIVATE
    "${GAME_ROOT}/src/model" "${GAME_ROOT}/third_party")
if(MSVC)
    target_compile_options(ai-wars-balance PRIVATE /EHsc /W4 /utf-8)
else()
    target_compile_options(ai-wars-balance PRIVATE -Wall -Wextra -Wpedantic)
endif()
