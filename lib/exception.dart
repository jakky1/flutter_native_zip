part of 'native_zip.dart';

class ZipException implements Exception {
  final int errCode;
  final String? message;
  ZipException(this.errCode, {this.message});

  @override
  String toString() {
    return "<$runtimeType> errCode:$errCode, message: $message";
  }
}

//

class ZipStreamException extends ZipException {
  ZipStreamException(super.errCode, {super.message});
}

//

abstract class ZipFileException extends ZipException {
  ZipFileException(super.errCode, {super.message});
}

class ZipFileInvalidPathException extends ZipFileException {
  ZipFileInvalidPathException(String? message) : super(0, message: message);
}

class ZipFileCreateException extends ZipFileException {
  ZipFileCreateException(String? message) : super(0, message: message);
}

class ZipFileOpenException extends ZipFileException {
  ZipFileOpenException(String? message) : super(0, message: message);
}

class ZipFileReadingException extends ZipFileException {
  ZipFileReadingException() : super(0);
}

class ZipFileWritingException extends ZipFileException {
  ZipFileWritingException() : super(0);
}

class ZipFileClosedException extends ZipFileException {
  ZipFileClosedException() : super(0);
}

//

abstract class ZipEntryException extends ZipException {
  ZipEntryException(super.errCode);
}

class ZipEntryOpenException extends ZipEntryException {
  ZipEntryOpenException() : super(0);
}

class ZipEntryReadException extends ZipEntryException {
  ZipEntryReadException(super.errCode);
}

// --------------------------------------------------------------------------
// convert error code to exception
// --------------------------------------------------------------------------

Exception _getExceptionByErrorCode(int err) {
  var msg = _errorMessageMap[err] ?? "Unknown error code: $err";
  return ZipException(err, message: msg);
}

void _throwByErrorCode(int err) {
  var exp = _getExceptionByErrorCode(err);
  if (exp == null) return;
  throw exp;
}

// error code mapping to error message
final _errorMessageMap = <int, String>{
  // my error code
  //
  NativeZipErrors.ERR_NZ_CANCELLED.value: "user cancelled",
  NativeZipErrors.ERR_NZ_DIR_TRAVERSAL_FILE_NOTFOUND.value:
      "file not found during directory traversal",
  NativeZipErrors.ERR_NZ_DIR_TRAVERSAL_NO_MORE_FILE.value:
      "no more file during directory traversal",
  NativeZipErrors.ERR_NZ_DIR_TRAVERSAL_PATH_TOO_LONG.value:
      "path too long during directory traversal",
  NativeZipErrors.ERR_NZ_INVALID_ARGUMENT.value:
      "invalid arguments", // malicious path, or entry path not ends with '/' for a dir
  NativeZipErrors.ERR_NZ_INVALID_PATH.value:
      "invalid path", // malicious path, or entry path not ends with '/' for a dir
  NativeZipErrors.ERR_NZ_ZIP_HAS_MALICIOUS_PATH.value:
      "malicious entry path in .zip file", // malicious path, or entry path not ends with '/' for a dir
  NativeZipErrors.ERR_NZ_INTERNAL_ERROR.value: "native_zip internal error",
  NativeZipErrors.ERR_NZ_MKDIR.value: "mkdir failed",
  NativeZipErrors.ERR_NZ_ZIP_ENTRY_NOT_FOUND.value: "zip entry not found",
  NativeZipErrors.ERR_NZ_ZIP_ENTRY_ALREADY_EXISTS.value:
      "try to add / move / rename to a zip entry which is already exists",
  NativeZipErrors.ERR_NZ_FILE_ALREADY_EXISTS.value:
      "try to unzip a file which is already exists",

  //
  // libzip error code
  //
  1: "ZIP_ER_MULTIDISK : Multi-disk zip archives not supported",
  2: "ZIP_ER_RENAME : Renaming temporary file failed",
  3: "ZIP_ER_CLOSE : Closing zip archive failed",
  4: "ZIP_ER_SEEK : Seek error",
  5: "ZIP_ER_READ : Read error",
  6: "ZIP_ER_WRITE : Write error",
  7: "ZIP_ER_CRC : CRC error",
  8: "ZIP_ER_ZIPCLOSED : Containing zip archive was closed",
  9: "ZIP_ER_NOENT : No such file",
  10: "ZIP_ER_EXISTS : File already exists",
  11: "ZIP_ER_OPEN : Can't open file",
  12: "ZIP_ER_TMPOPEN : Failure to create temporary file. Perhaps no access permission for that directory.",
  13: "ZIP_ER_ZLIB : Zlib error",
  14: "ZIP_ER_MEMORY : Malloc failure",
  15: "ZIP_ER_CHANGED : Entry has been changed",
  16: "ZIP_ER_COMPNOTSUPP : Compression method not supported",
  17: "ZIP_ER_EOF : Premature end of file",
  18: "ZIP_ER_INVAL : Invalid argument",
  19: "ZIP_ER_NOZIP : Not a zip archive",
  20: "ZIP_ER_INTERNAL : Internal error",
  21: "ZIP_ER_INCONS : Zip archive inconsistent",
  22: "ZIP_ER_REMOVE : Can't remove file",
  23: "ZIP_ER_DELETED : Entry has been deleted",
  24: "ZIP_ER_ENCRNOTSUPP : Encryption method not supported",
  25: "ZIP_ER_RDONLY : Read-only archive",
  26: "ZIP_ER_NOPASSWD : No password provided",
  27: "ZIP_ER_WRONGPASSWD : Wrong password provided",
  28: "ZIP_ER_OPNOTSUPP : Operation not supported",
  29: "ZIP_ER_INUSE : Resource still in use",
  30: "ZIP_ER_TELL : Tell error",
  31: "ZIP_ER_COMPRESSED_DATA : Compressed data invalid",
  32: "ZIP_ER_CANCELLED : Operation cancelled",
  33: "ZIP_ER_DATA_LENGTH : Unexpected length of data",
  34: "ZIP_ER_NOT_ALLOWED : Not allowed in torrentzip",
  35: "ZIP_ER_TRUNCATED_ZIP : Possibly truncated or corrupted zip archive",
};
