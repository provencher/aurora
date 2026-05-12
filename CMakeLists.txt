cmake_minimum_required(VERSION 3.25)
project(aurora LANGUAGES C CXX)
if (APPLE)
    enable_language(OBJC)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(AURORA_ENABLE_GX "Enable GX implementation and WebGPU renderer" ON)
option(AURORA_ENABLE_DVD "Enable DVD implementation backed by nod" OFF)  # TODO: need to fix tests
option(AURORA_ENABLE_CARD "Enable CARD implementation based on kabufuda" ON)
option(AURORA_ENABLE_RMLUI "Enable HTML/CSS Based UI Library for use in end-user UI development." OFF)
option(AURORA_ENABLE_OPENXR "Enable optional OpenXR SDK/runtime probing" OFF)
option(AURORA_FETCH_OPENXR_SDK "Fetch Khronos OpenXR-SDK if AURORA_ENABLE_OPENXR is ON and no OpenXR package is found" OFF)
option(AURORA_CACHE_USE_ZSTD "Compress WebGPU cache entries with zstd" ON)

# Dependency versions
set(AURORA_DAWN_VERSION "v20260423.175430" CACHE STRING "Dawn version tag (https://github.com/encounter/dawn-build/releases)")
set(AURORA_SDL3_VERSION "3.4.4" CACHE STRING "SDL3 version tag (https://github.com/libsdl-org/SDL/releases)")
set(AURORA_NOD_VERSION "v2.0.0-alpha.8" CACHE STRING "nod version tag (https://github.com/encounter/nod/releases)")
set(AURORA_OPENXR_SDK_VERSION "release-1.1.59" CACHE STRING "OpenXR-SDK ref (https://github.com/KhronosGroup/OpenXR-SDK)")

# Platform-specific defaults
if (CMAKE_CROSSCOMPILING)
  # Cross-compiling: vendor everything, don't use system packages
  set(_default_provider "vendor")
  set(_default_linkage "static")
else ()
  set(_default_provider "auto")
  if (WIN32)
    set(_default_linkage "shared")
  else ()
    set(_default_linkage "static")
  endif ()
endif ()

# Dawn/WebGPU (if AURORA_ENABLE_GX)
set(AURORA_DAWN_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide Dawn: auto, vendor (build from source), system (find_package/imported), package (download prebuilt)")
set_property(CACHE AURORA_DAWN_PROVIDER PROPERTY STRINGS auto vendor system package)
set(AURORA_DAWN_LINKAGE "${_default_linkage}" CACHE STRING "Dawn linkage type preference")
set_property(CACHE AURORA_DAWN_LINKAGE PROPERTY STRINGS shared static)

# SDL3
set(AURORA_SDL3_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide SDL3: auto, vendor (build from source), system (find_package/imported), or package (download prebuilt)")
set_property(CACHE AURORA_SDL3_PROVIDER PROPERTY STRINGS auto vendor system package)
set(AURORA_SDL3_LINKAGE "${_default_linkage}" CACHE STRING "SDL3 linkage type preference")
set_property(CACHE AURORA_SDL3_LINKAGE PROPERTY STRINGS shared static)

# nod (if AURORA_ENABLE_DVD)
set(AURORA_NOD_PROVIDER "${_default_provider}" CACHE STRING
  "How to provide nod: auto, vendor (build from source), system (find_package/imported), package (download prebuilt)")
set_property(CACHE AURORA_NOD_PROVIDER PROPERTY STRINGS auto vendor system package)
set(AURORA_NOD_LINKAGE "${_default_linkage}" CACHE STRING "nod linkage type preference")
set_property(CACHE AURORA_NOD_LINKAGE PROPERTY STRINGS shared static)

add_subdirectory(extern)

set(AURORA_OPENXR_LOADER_TARGET "")
if (AURORA_ENABLE_OPENXR)
  find_package(OpenXR CONFIG QUIET)
  if (NOT TARGET OpenXR::openxr_loader AND NOT TARGET openxr_loader AND AURORA_FETCH_OPENXR_SDK)
    message(STATUS "aurora: Fetching Khronos OpenXR-SDK")
    set(BUILD_TESTS OFF CACHE INTERNAL "")
    set(BUILD_API_LAYERS OFF CACHE INTERNAL "")
    set(BUILD_CONFORMANCE_TESTS OFF CACHE INTERNAL "")
    set(BUILD_CONFORMANCE_CLI OFF CACHE INTERNAL "")
    if (WIN32)
      # The SDK's static-loader target links the dynamic MSVC runtime on Windows.
      # Build the DLL loader instead so /MT Dusk builds do not hit CRT conflicts.
      set(DYNAMIC_LOADER ON CACHE INTERNAL "")
    endif ()
    include(FetchContent)
    FetchContent_Declare(openxr_sdk
      URL "https://github.com/KhronosGroup/OpenXR-SDK/archive/${AURORA_OPENXR_SDK_VERSION}.tar.gz"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(openxr_sdk)
  endif ()
  if (TARGET OpenXR::openxr_loader)
    set(AURORA_OPENXR_LOADER_TARGET OpenXR::openxr_loader)
  elseif (TARGET openxr_loader)
    set(AURORA_OPENXR_LOADER_TARGET openxr_loader)
  else ()
    message(WARNING "aurora: AURORA_ENABLE_OPENXR=ON, but no OpenXR loader target was found; building XR API stubs")
  endif ()
endif ()

include(cmake/aurora_core.cmake)
include(cmake/aurora_os.cmake)
if (AURORA_ENABLE_GX)
  include(cmake/aurora_gx.cmake)
  include(cmake/aurora_gd.cmake)
endif ()
include(cmake/aurora_pad.cmake)
include(cmake/aurora_ms.cmake)
include(cmake/aurora_si.cmake)
include(cmake/aurora_main.cmake)
include(cmake/aurora_mtx.cmake)
include(cmake/aurora_vi.cmake)
if(AURORA_ENABLE_CARD)
  include(cmake/aurora_card.cmake)
endif()
if (AURORA_ENABLE_DVD)
  include(cmake/aurora_dvd.cmake)
endif ()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(examples)
endif ()

if (NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif ()
