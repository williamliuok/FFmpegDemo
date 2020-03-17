# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)

set(JNI ${CMAKE_CURRENT_SOURCE_DIR}/../jni)

# 添加.so动态库
add_library(libavcodec SHARED IMPORTED)
add_library(libavformat SHARED IMPORTED)
add_library(libavutil SHARED IMPORTED)
#add_library(libswresample SHARED IMPORTED)
add_library(libswscale SHARED IMPORTED)

set_target_properties(libavcodec PROPERTIES IMPORTED_LOCATION ${JNI}/../../../libs/${ANDROID_ABI}/libavcodec.so)
set_target_properties(libavformat PROPERTIES IMPORTED_LOCATION ${JNI}/../../../libs/${ANDROID_ABI}/libavformat.so)
set_target_properties(libavutil PROPERTIES IMPORTED_LOCATION ${JNI}/../../../libs/${ANDROID_ABI}/libavutil.so)
#set_target_properties(libswresample PROPERTIES IMPORTED_LOCATION ${JNI}/../../../libs/${ANDROID_ABI}/libswresample-2.so)
set_target_properties(libswscale PROPERTIES IMPORTED_LOCATION ${JNI}/../../../libs/${ANDROID_ABI}/libswscale.so)

include_directories(${JNI}/include)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

add_library( # Sets the name of the library.
        fdageffmpeg

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        fdageffmpeg.cpp
        AndroidBuf.cpp
        AndroidBuf.h
        log.h
        rtsp.cc
        )

add_library( # Sets the name of the library.
        rtsp

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        AndroidBuf.cpp
        AndroidBuf.h
        log.h
        rtsp.cc
        )

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

find_library( # Sets the name of the path variable.
        android-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        android)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        fdageffmpeg

        libavcodec
        libavformat
        libavutil
        #        libswresample
        libswscale
        # Links the target library to the log library
        # included in the NDK.
        ${android-lib}
        ${log-lib})

target_link_libraries( # Specifies the target library.
        rtsp

        libavcodec
        libavformat
        libavutil
        #        libswresample
        libswscale
        # Links the target library to the log library
        # included in the NDK.
        ${android-lib}
        ${log-lib})