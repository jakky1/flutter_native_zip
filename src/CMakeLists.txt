# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

project(native_zip VERSION 0.0.1 LANGUAGES C)

add_library(native_zip SHARED
        "native_zip.c"
        "my_zlib.c"
        "my_compress.c"
        "my_zip.c"
        "my_zip_async.c"
        "my_zip_utils.c"
        "my_task_notify.c"
        "my_utils.c"
        "my_file.c"
        "my_file_windows.c"
        "my_file_posix.c"
        "my_thread.c"
        "my_threadpool.c"
        "my_queue.c"
        "my_message_queue.c"
        "my_atomic_int_max.c"
        # "my_hashmap.c"
)

set_target_properties(native_zip PROPERTIES
        PUBLIC_HEADER native_zip.h
        OUTPUT_NAME "native_zip"
)

target_compile_definitions(native_zip PUBLIC DART_SHARED_LIB)

if (ANDROID)
  # Support Android 15 16k page size
  target_link_options(native_zip PRIVATE "-Wl,-z,max-page-size=16384")
endif()