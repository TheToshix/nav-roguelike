include(FetchContent)

# Prefer a GoogleTest already present on the machine (packaged, or vendored by a
# distribution); fall back to fetching a pinned release. That keeps an offline
# build working and keeps CI from depending on the network on every job.
find_package(GTest QUIET)

if(NOT GTest_FOUND)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(nav_tests
  test_rng.cpp
  test_geometry.cpp
  test_mapgen.cpp
  test_fov.cpp
  test_pathfind.cpp
  test_inventory.cpp
  test_effects.cpp
  test_combat.cpp
  test_consumables.cpp
  test_ai.cpp
  test_zones.cpp
  test_bosses.cpp
  test_classes.cpp
  test_save.cpp
  test_sprites.cpp
  test_phases.cpp
  test_gear.cpp
  test_lobby.cpp
  test_integration.cpp
)

target_include_directories(nav_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nav_tests PRIVATE nav_engine GTest::gtest_main)

# The sprite test reads the generated artwork out of the source tree, so that
# it checks the engine against the real pictures rather than against a second
# list that would drift alongside it.
target_compile_definitions(nav_tests PRIVATE
  NAV_SPRITES_JS="${CMAKE_CURRENT_SOURCE_DIR}/../frontend/web/sprites.js")

if(MSVC)
  target_compile_options(nav_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
gtest_discover_tests(nav_tests
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300
)
