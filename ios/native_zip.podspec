#
# To learn more about a Podspec see http://guides.cocoapods.org/syntax/podspec.html.
# Run `pod lib lint native_zip.podspec` to validate before publishing.
#
Pod::Spec.new do |s|
  s.name             = 'native_zip'
  s.version          = '0.8.0'
  s.summary          = 'A Flutter FFI plugin for fast, easy-to-use, multi-threaded ZIP file operations.'
  s.description      = <<-DESC
A Flutter FFI plugin for fast, easy-to-use, multi-threaded ZIP file operations.
                       DESC
  s.homepage         = 'https://github.com/jakky1/flutter_native_zip'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'jakky1' => 'jakky1@gmail.com' }

  # This will ensure the source files in Classes/ are included in the native
  # builds of apps using this FFI plugin. Podspec does not support relative
  # paths, so Classes contains a forwarder C file that relatively imports
  # `../src/*` so that the C sources can be shared among all target platforms.
  s.source           = { :path => '.' }
  #s.source_files = 'Classes/**/*'
  s.source_files  = "../src/*.c", "../src/*.h"
  s.libraries     = 'z', 'zip'

  s.dependency 'Flutter'
  s.platform = :ios, '12.0'

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.0'
end
