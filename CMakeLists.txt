cmake_minimum_required(VERSION 3.16)
project(nav_roguelike VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(NAV_BUILD_TESTS "Build the unit test suite" ON)
option(NAV_BUILD_TERMINAL "Build the terminal frontend" ON)
option(NAV_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(NAV_COVERAGE "Instrument for gcov coverage" OFF)

# ---------------------------------------------------------------------------
# Engine: pure rules, no I/O. Both frontends and the tests link against this.
# ---------------------------------------------------------------------------
add_library(nav_engine
  engine/src/fov.cpp
  engine/src/mapgen.cpp
  engine/src/pathfind.cpp
  engine/src/data.cpp
  engine/src/game.cpp
  engine/src/actions.cpp
  engine/src/ai.cpp
  engine/src/save.cpp
  engine/src/score.cpp
  engine/src/achievements.cpp
  engine/src/keys.cpp
  engine/src/travel.cpp
)
target_include_directories(nav_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)

if(MSVC)
  target_compile_options(nav_engine PRIVATE /W4 /utf-8)
  if(NAV_WARNINGS_AS_ERRORS)
    target_compile_options(nav_engine PRIVATE /WX)
  endif()
else()
  target_compile_options(nav_engine PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
  if(NAV_WARNINGS_AS_ERRORS)
    target_compile_options(nav_engine PRIVATE -Werror)
  endif()
endif()

if(NAV_COVERAGE AND NOT MSVC)
  target_compile_options(nav_engine PUBLIC --coverage -O0 -g)
  target_link_options(nav_engine PUBLIC --coverage)
endif()

# ---------------------------------------------------------------------------
# Terminal frontend (not built for the WebAssembly target)
# ---------------------------------------------------------------------------
if(NAV_BUILD_TERMINAL AND NOT EMSCRIPTEN)
  add_executable(nav frontend/terminal/main.cpp frontend/terminal/bot.cpp)
  target_link_libraries(nav PRIVATE nav_engine)
  if(MSVC)
    target_compile_options(nav PRIVATE /utf-8)
  endif()
endif()

# ---------------------------------------------------------------------------
# WebAssembly frontend
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  add_executable(nav_web frontend/web/bindings.cpp)
  target_link_libraries(nav_web PRIVATE nav_engine)
  set_target_properties(nav_web PROPERTIES OUTPUT_NAME "nav" SUFFIX ".js")
  target_link_options(nav_web PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=NavModule"
    "-sSINGLE_FILE=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_nav_new_game','_nav_perform','_nav_set_language','_nav_state_json','_nav_static_json','_nav_save','_nav_load','_nav_record_score','_nav_score_table','_nav_achievements_record','_nav_achievements_table','_nav_daily_seed','_nav_keys_json','_nav_postmortem_json','_nav_free_cstr','_nav_version']"
    "-sENVIRONMENT=web,worker,node"
    "-O3"
  )
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NAV_BUILD_TESTS AND NOT EMSCRIPTEN)
  enable_testing()
  add_subdirectory(tests)
endif()
