cmake_minimum_required(VERSION 3.28)
project(fff-layout-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
if(APPLE)
  # Current macOS SDKs no longer ship AGL, which Qt's OpenGL wrapper still names.
  # OpenGL stands in for it so Qt6::Widgets links for the dock tests.
  find_package(OpenGL REQUIRED)
  set(WrapOpenGL_AGL "${OPENGL_gl_LIBRARY}" CACHE FILEPATH "Stand-in for the removed AGL framework" FORCE)
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets)
find_path(OBS_INCLUDE_DIR obs-module.h REQUIRED)

enable_testing()
add_executable(
  layout-tests
  layout-tests.cpp
  obs-stubs.cpp
  ../src/fff-session.cpp
  ../src/fff-http-server.cpp
  ../src/fff-monitor-access.cpp
  ../src/fff-asset-rendition.cpp
)
target_include_directories(layout-tests PRIVATE ../src "${OBS_INCLUDE_DIR}")
target_link_libraries(layout-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
# The session the plugin ships with is checked against the real loader, so a
# bundle the plugin would refuse cannot reach a release.
target_compile_definitions(layout-tests PRIVATE FFF_BUNDLED_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../data")
add_test(NAME layout-tests COMMAND layout-tests)

add_executable(monitor-access-tests monitor-access-tests.cpp ../src/fff-monitor-access.cpp)
target_include_directories(monitor-access-tests PRIVATE ../src)
target_link_libraries(monitor-access-tests PRIVATE Qt6::Core)
add_test(NAME monitor-access-tests COMMAND monitor-access-tests)

add_executable(
  monitor-lan-tests
  monitor-lan-tests.cpp
  obs-stubs.cpp
  ../src/fff-session.cpp
  ../src/fff-http-server.cpp
  ../src/fff-monitor-access.cpp
  ../src/fff-asset-rendition.cpp
)
target_include_directories(monitor-lan-tests PRIVATE ../src "${OBS_INCLUDE_DIR}")
target_link_libraries(monitor-lan-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME monitor-lan-tests COMMAND monitor-lan-tests)

add_executable(
  dock-tests
  dock-tests.cpp
  obs-stubs.cpp
  ../src/fff-dock-live.cpp
  ../src/fff-dock-roster.cpp
  ../src/fff-dock-settings.cpp
  ../src/fff-session.cpp
  ../src/fff-http-server.cpp
  ../src/fff-monitor-access.cpp
  ../src/fff-asset-rendition.cpp
)
target_include_directories(dock-tests PRIVATE ../src "${OBS_INCLUDE_DIR}")
target_link_libraries(dock-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets)
add_test(NAME dock-tests COMMAND dock-tests)
set_tests_properties(
  dock-tests
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${Qt6_DIR}/../../../plugins"
)

# The hotkeys need the OBS hotkey system, which obs-hotkey-stubs.cpp stands in
# for; fff-dock.cpp (the only file needing the frontend API) still stays out.
add_executable(
  hotkey-tests
  hotkey-tests.cpp
  obs-stubs.cpp
  obs-hotkey-stubs.cpp
  ../src/fff-hotkeys.cpp
  ../src/fff-dock-live.cpp
  ../src/fff-session.cpp
  ../src/fff-http-server.cpp
  ../src/fff-monitor-access.cpp
  ../src/fff-asset-rendition.cpp
)
target_include_directories(hotkey-tests PRIVATE ../src "${OBS_INCLUDE_DIR}")
target_link_libraries(hotkey-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets)
add_test(NAME hotkey-tests COMMAND hotkey-tests)
set_tests_properties(
  hotkey-tests
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${Qt6_DIR}/../../../plugins"
)

add_executable(
  asset-rendition-tests
  asset-rendition-tests.cpp
  obs-stubs.cpp
  ../src/fff-asset-rendition.cpp
  ../src/fff-session.cpp
  ../src/fff-http-server.cpp
  ../src/fff-monitor-access.cpp
)
target_include_directories(asset-rendition-tests PRIVATE ../src "${OBS_INCLUDE_DIR}")
target_link_libraries(asset-rendition-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME asset-rendition-tests COMMAND asset-rendition-tests)

add_executable(
  logo-round-tests
  logo-round-tests.cpp
  obs-stubs.cpp
  ../src/fff-session.cpp
  ../src/fff-http-server.cpp
  ../src/fff-monitor-access.cpp
  ../src/fff-asset-rendition.cpp
)
target_include_directories(logo-round-tests PRIVATE ../src "${OBS_INCLUDE_DIR}")
target_link_libraries(logo-round-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
add_test(NAME logo-round-tests COMMAND logo-round-tests)
