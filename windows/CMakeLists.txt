# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "native_zip")
project(${PROJECT_NAME} LANGUAGES CXX)

# Invoke the build for native code shared with the other target platforms.
# This can be changed to accommodate different builds.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../src" "${CMAKE_CURRENT_BINARY_DIR}/shared")




set(PKGNAME native_zip_libs_windows_v1)
set(myExtLibsPrefix ${CMAKE_BINARY_DIR}/packages_src)
set(URL_PREBUILT_LIBS "https://github.com/jakky1/jk_custom_appbar/releases/download/screenshot/${PKGNAME}.tar.gz")
set(ZIP_FILE_PREBUILT_LIBS ${myExtLibsPrefix}/${PKGNAME}.tar.gz)
set(DIR_PREBUILT_LIBS ${myExtLibsPrefix}/${PKGNAME})

# download prebuilt libraries
set(LIBS_DOWNLOAD_STAMP_FILE ${myExtLibsPrefix}/${PKGNAME}_download.stamp)
if (NOT EXISTS ${LIBS_DOWNLOAD_STAMP_FILE})
  message(NOTICE "[native_zip] downloading prebuilt libraries...")
  file(DOWNLOAD "${URL_PREBUILT_LIBS}" "${ZIP_FILE_PREBUILT_LIBS}"
    STATUS download_status
    LOG download_log
    SHOW_PROGRESS
    #EXPECTED_MD5 "abcdef1234567890abcdef1234567890"
  )
  list(GET download_status 0 status_code)
  list(GET download_status 1 status_string)
  if(NOT status_code EQUAL 0)
    message(FATAL_ERROR "[native_zip] prebuild libraries download failed: ${status_string}\nLog: ${download_log}")
  else()
    message(NOTICE "[native_zip] prebuilt libraries download success.")
    file(ARCHIVE_EXTRACT INPUT "${ZIP_FILE_PREBUILT_LIBS}" DESTINATION "${myExtLibsPrefix}") # unzip
    file(TOUCH ${LIBS_DOWNLOAD_STAMP_FILE})
  endif()
else()  
  # message(NOTICE "[native_zip] prebuilt libraries downloaded before")
endif()

# add 'include' folder
target_include_directories(${PROJECT_NAME} PRIVATE ${DIR_PREBUILT_LIBS}/include)

# link 'lib' files
target_link_libraries(${PROJECT_NAME} PRIVATE ${DIR_PREBUILT_LIBS}/zlib.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE ${DIR_PREBUILT_LIBS}/zip.lib)
#target_link_libraries(${PROJECT_NAME} PRIVATE ${DIR_PREBUILT_LIBS}/lzma.lib)


# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(native_zip_bundled_libraries
  # Defined in ../src/CMakeLists.txt.
  # This can be changed to accommodate different builds.
  $<TARGET_FILE:native_zip>

  # Jacky, copy dlls to exe dir  
  ${DIR_PREBUILT_LIBS}/zlib.dll 
  ${DIR_PREBUILT_LIBS}/zip.dll
  # ${DIR_PREBUILT_LIBS}/liblzma.dll

  PARENT_SCOPE
)
