library;

import 'dart:async';
import 'dart:collection';
import 'dart:developer';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'native_zip_bindings_generated.dart';

part 'exception.dart';
part 'stream.dart';
part 'zip.dart';
part 'task_notify.dart';

// dart run ffigen --config ffigen.yaml

// --------------------------------------------------------------------------

typedef _wrappedPrint_C = Void Function(Pointer<Char> a);
final wrappedPrintPointer = Pointer.fromFunction<_wrappedPrint_C>(dartPrint);
void dartPrint(Pointer<Char> arg) {
  var str = arg.cast<Utf8>().toDartString();
  print("[FFI] $str");
}

void initDartLog() {
  //_bindings.initDartPrint(wrappedPrintPointer);
}

// --------------------------------------------------------------------------

final class NativeZip {
  // [windowsBits] values for different compression mode in zlib
  static const int _TYPE_GZIP = 31; // raw deflate, 16 + 8~15
  static const int _TYPE_ZLIB = 15; // zlib, 9~15
  static const int _TYPE_DEFLATE = -11; // raw deflate, -8 ~ -15

  /// _TYPE_DECOMPRESS: auto detect zip(zlib)/gzip headers to decode,
  /// but cannot decode file which is encoded by 'deflate'
  static const int _TYPE_DECOMPRESS_ZLIB_GZIP = 32 + 15; // 32 + 8~15

  /// [level] range: (1, 9)
  /// level==1 : fast, low compression
  /// level==9 : slow, max compression
  static ListIntStreamTransformer gzipWithLevel([int level = -1]) =>
      _NativeZipStreamTransformer(1, _TYPE_GZIP, level);
  static ListIntStreamTransformer get gzip =>
      const _NativeZipStreamTransformer(1, _TYPE_GZIP);
  static ListIntStreamTransformer get gunzip =>
      const _NativeZipStreamTransformer(0, _TYPE_DECOMPRESS_ZLIB_GZIP);

  static ListIntStreamTransformer zlibWithLevel([int level = -1]) =>
      _NativeZipStreamTransformer(1, _TYPE_ZLIB, level);
  static ListIntStreamTransformer get zlib =>
      const _NativeZipStreamTransformer(1, _TYPE_ZLIB);
  static ListIntStreamTransformer get unzlib =>
      const _NativeZipStreamTransformer(0, _TYPE_DECOMPRESS_ZLIB_GZIP);

  static ListIntStreamTransformer deflateWithLevel([int level = -1]) =>
      _NativeZipStreamTransformer(1, _TYPE_DEFLATE, level);
  static ListIntStreamTransformer get deflate =>
      const _NativeZipStreamTransformer(1, _TYPE_DEFLATE);
  static ListIntStreamTransformer get inflate =>
      const _NativeZipStreamTransformer(0, _TYPE_DEFLATE);

  //

  /// open or create zip file
  static ZipFile openZipFile(String zipPath, {String? password}) {
    // TODO: UI may block several ms when open big zip file
    return ZipFile._(zipPath, password);
  }

  static bool _isFileExists(String path) {
    return FileSystemEntity.typeSync(path) != FileSystemEntityType.notFound;
  }

  /// Compress the entire folder into a .zip file, with multi-thread support
  ///
  /// [dirPath] is path of the directory you want to compress
  ///
  /// [zipPath] is the result .zip file
  ///
  /// [compressLevel] parameter must be between 0 and 9. 0 means no compression, 1 means fast compression but low compression ratio, and 9 means the slowest compression but the highest compression ratio. Default is 5
  ///
  /// [threadCount] default set to [Platform.numberOfProcessors]. In other words, use 100% of CPU
  static ZipTaskFuture zipDir(String dirPath, String zipPath,
      {String? password,
      int compressLevel = 5,
      bool skipTopLevel = false,
      int threadCount = 0}) {
    if (_isFileExists(zipPath)) {
      throw ZipFileCreateException("Zip file already exists: $zipPath");
    }
    if (!_isFileExists(dirPath)) {
      throw ZipFileOpenException("directory path not exists: $dirPath");
    }

    var zip = openZipFile(zipPath, password: password);
    var future = zip.addFile(dirPath, "",
        compressLevel: compressLevel,
        skipTopLevel: skipTopLevel,
        threadCount: threadCount);
    future.whenComplete(() => zip.close());
    return future;
  }

  /// Extract the .zip file to the specified directory, with multi-thread support
  ///
  /// [zipPath] is the path of .zip file
  ///
  /// [dirPath] is directory path where you store the extracted .zip file.
  ///
  /// [threadCount] default set to [Platform.numberOfProcessors]. In other words, use 100% of CPU
  static ZipTaskFuture unzipToDir(String zipPath, String dirPath,
      {String? password, int threadCount = 0}) {
    if (!_isFileExists(zipPath)) {
      throw ZipFileOpenException("Zip file not exists: $zipPath");
    }

    var zip = openZipFile(zipPath, password: password);
    var future = zip.saveTo("", dirPath, threadCount: threadCount);
    future.whenComplete(() {
      zip.close();
    });
    return future;
  }
}

// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
// --------------------------------------------------------------------------

const String _libName = 'native_zip';

/// The dynamic library in which the symbols for [NativeZipBindings] can be found.
final DynamicLibrary _dylib = () {
  if (Platform.isMacOS || Platform.isIOS) {
    return DynamicLibrary.open('$_libName.framework/$_libName');
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('lib$_libName.so');
  }
  if (Platform.isWindows) {
    return DynamicLibrary.open('$_libName.dll');
  }
  throw UnsupportedError('Unknown platform: ${Platform.operatingSystem}');
}();

/// The bindings to the native functions in [_dylib].
final NativeZipBindings _bindings = NativeZipBindings(_dylib);
