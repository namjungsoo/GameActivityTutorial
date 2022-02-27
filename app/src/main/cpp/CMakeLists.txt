# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.18.1)

# Declares and names the project.

project("gameactivitytutorial")

# Add the packages from the Android Game SDK
find_package(game-activity REQUIRED CONFIG)
find_package(games-frame-pacing REQUIRED CONFIG)
find_package(games-performance-tuner REQUIRED CONFIG)
find_package(oboe REQUIRED CONFIG)

# Set the base dir
set(GAMESDK_BASE_DIR "../../../../../games-samples/agdk")

set(PROTOBUF_NANO_SRC_DIR "${GAMESDK_BASE_DIR}/third_party/nanopb-c")
# Include the protobuf utility file from the Android Game SDK
include("${GAMESDK_BASE_DIR}/util/protobuf/protobuf.cmake")
# Directory of nano protobuf library source files
include_directories(${PROTOBUF_NANO_SRC_DIR})
# generate runtime files using protoc
protobuf_generate_nano_c( ${CMAKE_CURRENT_SOURCE_DIR}/../proto ../proto/dev_tuningfork.proto)
protobuf_generate_nano_c( ${CMAKE_CURRENT_SOURCE_DIR}/../proto ../proto/tuningfork.proto)
include_directories(${PROTO_GENS_DIR})

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

add_library( # Sets the name of the library.
        gameactivitytutorial

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        ${PROTOBUF_NANO_SRCS}
        ${PROTO_GENS_DIR}/nano/dev_tuningfork.pb.c
        ${PROTO_GENS_DIR}/nano/tuningfork.pb.c
        android_main.cpp
        native_engine.cpp
        tuning_manager.cpp
        game_activity_included.cpp
        game_text_input_included.cpp
        native_app_glue_included.c
        native-lib.cpp)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        gameactivitytutorial

        game-activity::game-activity
        games-frame-pacing::swappy_static
        games-performance-tuner::tuningfork_static
        oboe::oboe

        android
        EGL
        GLESv3
        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})