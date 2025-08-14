part of 'native_zip.dart';

extension _CharPointer on Pointer<Char> {
  String toDartString() => cast<Utf8>().toDartString();
}

final class ZipEntryInfo {
  final ZipFile _zip;

  /// entry index
  final int index;

  /// unix time, seconds elapsed from 1970/01/01 00:00:00 (UTC)
  final int modifiedUnixTime;

  final String path;
  final int originalSize;
  final int compressedSize;

  bool get isDirectory => path.endsWith("/");
  DateTime get modifiedDateTime =>
      DateTime.fromMillisecondsSinceEpoch(modifiedUnixTime * 1000);

  ZipEntryInfo._(this._zip, NativeZipEntry e)
      : index = e.index,
        path = e.path.toDartString(),
        originalSize = e.originalSize,
        compressedSize = e.compressedSize,
        modifiedUnixTime = e.modifiedTime;

  Stream<List<int>> openRead() async* {
    yield* _zip.openReadByIndex(index);
  }
}

// --------------------------------------------------------------------------

final class ZipFile {
  final String _zipFilePath;
  final String? _password;
  late Pointer<Void> _pZip;

  int _readWriteCount = 0; // if read op +1 , if write op -1
  bool _isClosed = false;

  ZipFile._(this._zipFilePath, this._password) {
    _reopen();
  }

  void _reopen() {
    // NOTE:
    // in some operations that change the .zip content,
    // such as add / delete / rename, the zip_close() in libzip will be called
    // so we should reopen .zip after such operations
    var s1 = _zipFilePath.toNativeUtf8().cast<Char>();
    var s2 = _password?.toNativeUtf8().cast<Char>() ?? nullptr;
    _pZip = _bindings.openZip(s1, s2);
    malloc.free(s1);
    if (s2 != nullptr) malloc.free(s2);

    if (_pZip == nullptr) {
      throw ZipFileOpenException("Cannot open zip file: $_zipFilePath");
    }
  }

  void close() {
    _throwExceptionIf(false);

    _isClosed = true;
    _bindings.closeZip(_pZip);
  }

  void _throwExceptionIf(bool isReadOp) {
    if (!isReadOp && _readWriteCount > 0) throw ZipFileReadingException();
    if (_readWriteCount < 0) throw ZipFileWritingException();
    if (_isClosed) throw ZipFileClosedException();
  }

  /// get information of entries
  ///
  /// [path] is path of a file or directory in zip file
  ///
  /// [path] == "" means root directory
  ///
  /// path separator in [path] must be '/', even in Windows platform
  ///
  /// [recursive] == true return all files and subdirectories within the specified directory, including those within nested subdirectories
  ///
  /// [recursive] == false only return files and directories in specified folder
  List<ZipEntryInfo> getEntries({String path = "", bool recursive = true}) {
    _throwExceptionIf(true);

    final Pointer<Int> lenPtr = calloc<Int>();
    var nativePath = path.toNativeUtf8().cast<Char>();
    final nativePtr = _bindings.getZipEntries(
      _pZip,
      lenPtr,
      nativePath,
      recursive ? 1 : 0,
    );

    calloc.free(lenPtr);
    malloc.free(nativePath);

    final structList = List.generate(
      lenPtr.value,
      (i) => ZipEntryInfo._(this, nativePtr[i]),
    );

    _bindings.nativeFree(nativePtr.cast<Void>());
    return structList;
  }

  // --------

  Stream<List<int>> openRead(String entryPath) async* {
    _throwExceptionIf(true);

    var s1 = entryPath.toNativeUtf8().cast<Char>();
    var pFile = _bindings.readZipFileEntryOpen(_pZip, s1);
    malloc.free(s1);
    if (pFile == nullptr) throw ZipEntryOpenException();
    yield* __openRead(pFile);
  }

  Stream<List<int>> openReadByIndex(int entryIndex) async* {
    if (_isClosed) throw ZipFileClosedException();

    var pFile = _bindings.readZipFileEntryOpenByIndex(_pZip, entryIndex);
    if (pFile == nullptr) throw ZipEntryOpenException();
    yield* __openRead(pFile);
  }

  Stream<List<int>> __openRead(Pointer<Void> pFile) async* {
    const int bufSize = 1024 * 16;
    Pointer<Int8>? pOutBuf;

    _readWriteCount++;
    try {
      while (true) {
        pOutBuf = malloc<Int8>(bufSize);
        int outLen = _bindings.readZipFileEntry(pFile, pOutBuf, bufSize);
        if (outLen > 0) {
          yield pOutBuf.asTypedList(outLen, finalizer: malloc.nativeFree);
          pOutBuf = null;
        } else if (outLen == 0) {
          break;
        } else {
          if (_isClosed) throw ZipFileClosedException();
          throw ZipEntryReadException(outLen);
        }
      }
    } finally {
      // called when finished normally, or StreamSubscription.cancel() called
      if (pOutBuf != null) malloc.free(pOutBuf);
      _bindings.readZipFileEntryClose(pFile);

      _readWriteCount--;
    }
  }

  /// Copy files from .zip to disk, with multi-thread support
  ///
  /// if [entryPath] is a file, save file to folder [outDirPath]
  ///
  /// if [entryPath] is a directory, recursively save the directory to folder [outDirPath]
  ///
  /// [threadCount] default set to [Platform.numberOfProcessors]. In other words, use 100% of CPU
  ///
  /// Example: saveFilesTo(["prefix/dirA/"], "C:\\dirB\\") copy all files in 'prefix/dirA/*' in .zip to 'C:\\dirB\\dirA\\*' in disk
  ZipTaskFuture saveTo(
    String entryPath,
    String outDirPath, {
    int threadCount = 0,
  }) {
    return saveFilesTo(
      <String>[entryPath],
      outDirPath,
      threadCount: threadCount,
    );
  }

  /// Copy files from .zip to disk, with multi-thread support
  ///
  /// same as [saveTo], but copy many paths at the same time.
  ///
  /// Refer to [saveTo] document for details
  ZipTaskFuture saveFilesTo(
    List<String> entryPaths,
    String outDirPath, {
    int threadCount = 0,
  }) {
    _throwExceptionIf(true);

    if (threadCount < 1 || threadCount > Platform.numberOfProcessors) {
      if (Platform.isAndroid || Platform.isIOS) {
        threadCount = (Platform.numberOfProcessors / 2).toInt();
      } else {
        threadCount = Platform.numberOfProcessors;
      }
    }
    if (entryPaths.isEmpty) {
      throw ZipFileInvalidPathException("argument [entryPaths] must not empty");
    }
    for (var p in entryPaths) {
      if (p.startsWith("/")) {
        throw ZipFileInvalidPathException(
            "argument [entryPaths] cannot starts with separator: $p");
      }
    }

    int count = entryPaths.length;
    final Pointer<Pointer<Char>> nativeArr = malloc.allocate(
      sizeOf<Pointer<Utf8>>() * count,
    );
    for (int i = 0; i < count; i++) {
      nativeArr[i] = entryPaths[i].toNativeUtf8().cast<Char>();
    }

    var s1 = _password?.toNativeUtf8().cast<Char>() ?? nullptr;
    var s2 = _zipFilePath.toNativeUtf8().cast<Char>();
    var s3 = outDirPath.toNativeUtf8().cast<Char>();
    var task = _bindings
        .unzipToDirAsync(_pZip, s1, s2, nativeArr, count, s3, threadCount)
        .cast<NativeZipTaskInfo>();

    final completer = Completer<void>();
    if (task == nullptr) {
      completer.completeError(ZipFileException);
    } else {
      _registerTask(task.ref.taskId, completer);
    }

    var dartTask = ZipTaskFuture._(completer.future, task);
    _readWriteCount++;
    completer.future.whenComplete(() {
      _readWriteCount--;
      //_reopen(); // don't reopen() because unzipToDir() won't close zip

      // cleanup after task done
      if (s1 != nullptr) malloc.free(s1);
      malloc.free(s2);
      malloc.free(s3);
      for (int i = 0; i < count; i++) {
        malloc.free(nativeArr[i++]);
      }
      malloc.free(nativeArr);

      dartTask._destroy();
    });

    return dartTask;
  }

  // --------

  /// Add files from disk to .zip, with multi-thread support
  ///
  /// if [dirPath] is a file, copy file to zip file with entry path [zipEntryDirPath]/filename
  ///
  /// if [dirPath] is a directory, recursively copy files in [dirPath] to zip file with entry path [zipEntryDirPath]/dirName/*
  ///
  /// [zipEntryDirPath] can not start with '/' or '\\', and cannot contains ".."
  ///
  /// [zipEntryDirPath] set to empty string "" represents root directory.
  ///
  /// [threadCount] default set to [Platform.numberOfProcessors]. In other words, use 100% of CPU
  ZipTaskFuture addFile(String dirPath, String zipEntryDirPath,
      {int compressLevel = 5, bool skipTopLevel = false, int threadCount = 0}) {
    return addFiles(<String>[dirPath], zipEntryDirPath,
        compressLevel: compressLevel,
        skipTopLevel: skipTopLevel,
        threadCount: threadCount);
  }

  /// Add files from disk to .zip, with multi-thread support
  ///
  /// Same as [addFile], but copy many paths at one time
  ///
  /// Refer to [addFile] document for details.
  ///
  /// Example: addFiles(["c:\\dirA\\", "prefix/dirB"]) add all files in 'c:\\dirA\\*' in disk to 'prefx/dirB/dirA/*' in .zip
  ZipTaskFuture addFiles(List<String> dirPaths, String zipEntryDirPath,
      {int compressLevel = 5, bool skipTopLevel = false, int threadCount = 0}) {
    _throwExceptionIf(false);

    if (threadCount < 1 || threadCount > Platform.numberOfProcessors) {
      if (Platform.isAndroid || Platform.isIOS) {
        threadCount = (Platform.numberOfProcessors / 2).toInt();
      } else {
        threadCount = Platform.numberOfProcessors;
      }
    }
    if (compressLevel < 0 || compressLevel > 9) {
      throw ZipException(0, message: "argument [compressLevel] must be 0~9");
    }
    if (dirPaths.isEmpty) {
      throw ZipFileInvalidPathException("argument [dirPaths] must not empty");
    }
    for (var p in dirPaths) {
      if (p.endsWith(Platform.pathSeparator)) {
        throw ZipFileInvalidPathException(
            "argument [dirPaths] cannot ends with separator: $p");
      }
    }
    if (zipEntryDirPath.isNotEmpty && !zipEntryDirPath.endsWith("/")) {
      throw ZipFileInvalidPathException(
          "[zipEntryDirPath] must be empty, or must ends with '/': $zipEntryDirPath");
    }

    //

    int count = dirPaths.length;
    final Pointer<Pointer<Char>> nativeArr = malloc.allocate(
      sizeOf<Pointer<Utf8>>() * count,
    );
    for (int i = 0; i < count; i++) {
      nativeArr[i] = dirPaths[i].toNativeUtf8().cast<Char>();
    }

    var s1 = zipEntryDirPath.toNativeUtf8().cast<Char>();
    var task = _bindings
        .zipDirAsync(_pZip, _password != null ? true : false, nativeArr, count,
            s1, compressLevel, skipTopLevel ? 1 : 0, threadCount)
        .cast<NativeZipTaskInfo>();

    final completer = Completer<void>();
    if (task == nullptr) {
      completer.completeError(ZipFileException);
    } else {
      _registerTask(task.ref.taskId, completer);
    }

    var dartTask = ZipTaskFuture._(completer.future, task);
    _readWriteCount--;
    completer.future.whenComplete(() {
      _reopen(); // because zip_close() called when saving changes in .zip, so we reopen zip file here
      _readWriteCount++;

      // cleanup after task done
      malloc.free(s1);
      for (int i = 0; i < count; i++) {
        malloc.free(nativeArr[i++]);
      }
      malloc.free(nativeArr);

      dartTask._destroy();
    });

    return dartTask;
  }

  // ------------------------------------------------------------------------

  /// for rename / move / remove operation, most code are the same
  Future<void> _commonZipTask(
      String? str1,
      String? str2,
      List<String>? list,
      int Function(Pointer<Char>?, Pointer<Char>?, Pointer<Pointer<Char>>?)
          cb) {
    _throwExceptionIf(false);
    _readWriteCount--;

    Future.delayed(Duration.zero); // let caller can set catchError() for Future

    // convert dart string to native string
    var s1 = str1?.toNativeUtf8().cast<Char>();
    var s2 = str2?.toNativeUtf8().cast<Char>();
    int count = list?.length ?? 0;
    final Pointer<Pointer<Char>>? nativeArr = (list == null)
        ? null
        : malloc.allocate(
            sizeOf<Pointer<Utf8>>() * count,
          );
    for (int i = 0; i < count; i++) {
      nativeArr![i] = list![i].toNativeUtf8().cast<Char>();
    }

    // cleanup() allocated native strings when task finished or failed
    void cleanup() {
      _reopen(); // because zip_close() called when saving changes in .zip, so we reopen zip file here
      _readWriteCount++;

      if (s1 != null) malloc.free(s1);
      if (s2 != null) malloc.free(s2);
      if (nativeArr != null) {
        for (int i = 0; i < count; i++) {
          malloc.free(nativeArr[i++]);
        }
        malloc.free(nativeArr);
      }
    }

    // do rename / move / remove operation
    int taskId = cb(s1, s2, nativeArr);

    final completer = Completer<void>();
    _registerTask(taskId, completer); // listen for when it is completed

    completer.future.whenComplete(() => cleanup()); // clean when task completed
    return completer.future;
  }

  /// rename entry from [oldEntryPath] to [newEntryPath]
  ///
  /// if entry is a directory, both [oldEntryPath] and  [newEntryPath] must ends with '/'
  ///
  /// if entry is a directory, all the entries under [oldEntryPath] will be moved recursively
  ///
  /// Example: renameEntry("prefix/dirA/", "prefix/dirB/") rename 'dirA' to 'dirB'
  ///
  /// Example: renameEntry("prefix/fileA.txt", "prefix/fileB.txt") rename 'fileA.txt' to 'fileB.txt'
  ///
  /// Example: renameEntry("prefix/dirA/", "prefix/dirB/dirC/") move whole 'dirA' to 'prefix/dirB/' recursively, and rename to 'dirC'
  Future<void> renameEntry(String oldEntryPath, String newEntryPath) {
    _checkEntryPath(oldEntryPath);
    _checkEntryPath(newEntryPath);

    return _commonZipTask(oldEntryPath, newEntryPath, null, (s1, s2, list) {
      int taskId = _bindings.zipRenameEntryAsync(_pZip, s1!, s2!);
      return taskId;
    });
  }

  void _checkEntryPath(String entryPath) {
    if (entryPath.startsWith("/")) {
      throw ZipFileInvalidPathException(
          "entry path in argument cannot starts with separator: $entryPath");
    }
  }

  void _checkEntryPathList(List<String> entryPaths) {
    if (entryPaths.isEmpty) {
      throw ZipFileInvalidPathException(
          "argument [entryPaths] cannot be empty");
    }
    for (var p in entryPaths) {
      _checkEntryPath(p);
    }
  }

  // ------------------------------------------------------------------------

  /// the same with [moveEntries], but only move one entry
  Future<void> moveEntry(String entryPath, String newEntryBaseDirPath) {
    return moveEntries(<String>[entryPath], newEntryBaseDirPath);
  }

  /// move entries in [entryPathList] will be moved to [newEntryBaseDirPath]/*/* recursively
  ///
  /// [newEntryBaseDirPath] must ends with '/', or be empty string "" (which represents root directory)
  ///
  /// Example: moveEntries(&lt;String&gt;["prefix/dirA/"], "prefix/dirB/") move whole 'dirA' as 'prefix/dirB/dirA/'
  ///
  Future<void> moveEntries(
      List<String> entryPathList, String newEntryBaseDirPath) {
    _checkEntryPathList(entryPathList);
    _checkEntryPath(newEntryBaseDirPath);

    return _commonZipTask(newEntryBaseDirPath, null, entryPathList,
        (s1, s2, list) {
      int taskId = _bindings.zipMoveEntriesAsync(
          _pZip, list!, entryPathList.length, s1!);
      return taskId;
    });
  }

  // ------------------------------------------------------------------------

  /// the same with [removeEntries], but only remove one entry
  Future<void> removeEntry(String entryPathList) {
    return removeEntries(<String>[entryPathList]);
  }

  /// remove entries in [entryPathList]
  ///
  /// Example: removeEntries(&lt;String&gt;["prefix/dirA/"]) remove whole directory 'prefix/dirA/' recursively
  Future<void> removeEntries(List<String> entryPathList) {
    _checkEntryPathList(entryPathList);

    return _commonZipTask(null, null, entryPathList, (s1, s2, list) {
      int taskId =
          _bindings.zipRemoveEntriesAsync(_pZip, list!, entryPathList.length);
      return taskId;
    });
  }
}

// --------------------------------------------------------------------------

class ZipTaskFuture implements Future<void> {
  Pointer<NativeZipTaskInfo>? _task;
  final Future<void> _future;
  ZipTaskFuture._(this._future, this._task);

  /// cancel the task
  void cancel() {
    _task?.ref.isCancelled = true;
  }

  void _destroy() {
    if (_task != null) {
      _bindings.nativeFree(_task!.cast<Void>());
      _task = null;
    }
  }

  /// total (uncompressed) file size
  int get totalSize => _task?.ref.progress.total_fileSize ?? 0;

  /// now processed (uncompressed) file size
  int get processedSize => _task?.ref.progress.processed_fileSize ?? 0;

  /// now processed (compressed) file size
  int get compressedSize => _task?.ref.progress.processed_compressSize ?? 0;

  /// now processing file path
  String get nowProcessingFilepath {
    try {
      return _task?.ref.progress.now_processing_filePath.toDartString() ?? "";
    } catch (_) {
      return "";
    }
  }

  //

  @override
  Stream<void> asStream() => _future.asStream();

  @override
  Future<void> catchError(Function onError,
          {bool Function(Object error)? test}) =>
      _future.catchError(onError, test: test);

  @override
  Future<R> then<R>(FutureOr<R> Function(void value) onValue,
          {Function? onError}) =>
      _future.then(onValue, onError: onError);

  @override
  Future<void> timeout(Duration timeLimit,
          {FutureOr<void> Function()? onTimeout}) =>
      _future.timeout(timeLimit, onTimeout: onTimeout);

  @override
  Future<void> whenComplete(FutureOr<void> Function() action) =>
      _future.whenComplete(action);
}
