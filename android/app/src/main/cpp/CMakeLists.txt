cmake_minimum_required(VERSION 3.22.1)
project(SwordForgeAndroid LANGUAGES C)
get_filename_component(FORGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_library(swordforge SHARED android_bridge.c
    "${FORGE_ROOT}/src/data.c" "${FORGE_ROOT}/src/game.c"
    "${FORGE_ROOT}/src/forge.c" "${FORGE_ROOT}/src/adventure.c" "${FORGE_ROOT}/src/delve.c"
    "${FORGE_ROOT}/src/save.c" "${FORGE_ROOT}/src/game_api.c")
target_include_directories(swordforge PRIVATE "${FORGE_ROOT}/include")
target_compile_features(swordforge PRIVATE c_std_11)
target_compile_options(swordforge PRIVATE -Wall -Wextra -Wpedantic -Werror -Wformat=2)
target_link_options(swordforge PRIVATE "-Wl,-z,max-page-size=16384")
