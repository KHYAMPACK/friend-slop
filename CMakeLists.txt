cmake_minimum_required(VERSION 3.16)
project(heart_house CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# Steamworks SDK: STEAMWORKS_SDK env/cache, or third_party/steamworks (gitignored).
if(NOT STEAMWORKS_SDK AND DEFINED ENV{STEAMWORKS_SDK})
    set(STEAMWORKS_SDK "$ENV{STEAMWORKS_SDK}")
endif()
if(NOT STEAMWORKS_SDK)
    set(STEAMWORKS_SDK "${CMAKE_SOURCE_DIR}/third_party/steamworks")
endif()
if(NOT EXISTS "${STEAMWORKS_SDK}/public/steam/steam_api.h"
   AND EXISTS "${STEAMWORKS_SDK}/sdk/public/steam/steam_api.h")
    set(STEAMWORKS_SDK "${STEAMWORKS_SDK}/sdk")
endif()
if(NOT EXISTS "${STEAMWORKS_SDK}/public/steam/steam_api.h")
    message(FATAL_ERROR
        "Steamworks SDK not found (need public/steam/steam_api.h).\n"
        "Download from https://partner.steamgames.com/downloads/list\n"
        "Extract so public/steam/steam_api.h exists, then either:\n"
        "  - put it in third_party/steamworks/\n"
        "  - or cmake -DSTEAMWORKS_SDK=path/to/sdk")
endif()

set(STEAMWORKS_BIN "${STEAMWORKS_SDK}/redistributable_bin/win64")
if(NOT EXISTS "${STEAMWORKS_BIN}/steam_api64.lib")
    message(FATAL_ERROR
        "steam_api64.lib not found under ${STEAMWORKS_BIN}.\n"
        "This project links the 64-bit Steamworks redistributable.")
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/steam/steam_client.cpp
    Headers/Math/Vector/Vector2.hpp
    Headers/Math/Vector/Vector2Math.hpp
    Headers/Math/Vector/Vector3.hpp
    Headers/Math/Vector/Vector3Math.hpp
    Headers/Math/Vector/Vector4.hpp
    Headers/Math/Quaternion/Quaternion.hpp
    Headers/Math/Matrix/Matrix4.hpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    "${STEAMWORKS_SDK}/public"
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
    "${STEAMWORKS_BIN}/steam_api64.lib"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${STEAMWORKS_BIN}/steam_api64.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/steam_appid.txt"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
