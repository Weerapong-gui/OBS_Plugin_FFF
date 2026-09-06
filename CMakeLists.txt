cmake_minimum_required(VERSION 3.28...3.30)

# cmake/common/buildnumber.cmake only assigns PLUGIN_BUILD_NUMBER when running in
# CI, so local builds end up with an empty value that breaks
# set_target_properties_plugin() with "incorrect number of arguments". Seed the
# cache file it reads so the counter starts at 1 outside of CI as well.
set(_build_number_cache "${CMAKE_CURRENT_SOURCE_DIR}/cmake/.CMakeBuildNumber")
if(EXISTS "${_build_number_cache}")
  file(READ "${_build_number_cache}" _cached_build_number)
  string(STRIP "${_cached_build_number}" _cached_build_number)
else()
  set(_cached_build_number "")
endif()
if(NOT _cached_build_number MATCHES "^[0-9]+$")
  file(WRITE "${_build_number_cache}" "0")
endif()
unset(_cached_build_number)
unset(_build_number_cache)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)

  # Qt 6.8 links AGL alongside OpenGL, but the macOS 26 SDK no longer ships
  # AGL.framework (only a runtime stub under /System remains, which the linker
  # rejects with "ld: framework 'AGL' not found"). Drop it when the SDK in use
  # has no AGL; older SDKs keep Qt's original behaviour.
  # CMAKE_OSX_SYSROOT is empty with the Xcode generator, so ask xcrun instead.
  if(APPLE)
    if(CMAKE_OSX_SYSROOT AND IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
      set(_macos_sdk_path "${CMAKE_OSX_SYSROOT}")
    else()
      execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE _macos_sdk_path
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
    endif()
  endif()

  if(APPLE
     AND TARGET WrapOpenGL::WrapOpenGL
     AND _macos_sdk_path
     AND NOT EXISTS "${_macos_sdk_path}/System/Library/Frameworks/AGL.framework")
    get_target_property(_wrap_opengl_libraries WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_opengl_libraries)
      list(FILTER _wrap_opengl_libraries EXCLUDE REGEX "AGL\\.framework|-framework AGL")
      set_target_properties(
        WrapOpenGL::WrapOpenGL
        PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libraries}"
      )
    endif()
    unset(_wrap_opengl_libraries)
  endif()
  unset(_macos_sdk_path)

  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/tint-filter.c)

if(ENABLE_QT AND ENABLE_FRONTEND_API)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/fff-dock.cpp)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE FFF_ENABLE_DOCK)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
