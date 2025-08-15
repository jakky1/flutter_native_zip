part of 'native_zip.dart';

class ZipStreamConverter extends Converter<List<int>, List<int>> {
  final int zipAction;
  final int windowsBits;
  final int level; // -1:default, 0:no_compression, 1:fast, 9:best_compression

  const ZipStreamConverter._(
    this.zipAction, [
    this.windowsBits = 8,
    this.level = -1,
  ]);

  @override
  Sink<List<int>> startChunkedConversion(Sink<List<int>> sink) {
    if (level < -1 || level > 9 || level == 0) {
      throw ZipStreamException(
        -99,
        message: "invalid compress level: $level",
      );
    }

    return _NativeZipStreamSink(zipAction, windowsBits, level, sink);
  }

  @override
  List<int> convert(List<int> input) {
    _BufferSink sink = _BufferSink();
    startChunkedConversion(sink)
      ..add(input)
      ..close();
    return sink.builder.takeBytes();
  }
}

class _BufferSink extends ByteConversionSink {
  final builder = BytesBuilder(copy: false);

  @override
  void add(List<int> chunk) => builder.add(chunk);

  @override
  void close() {}
}

class _NativeZipStreamSink extends ByteConversionSink {
  // zipAction: 1=>zip , 0=>unzip
  final int zipAction;
  final int windowsBits;
  final int level; // only works when compress
  final Sink<List<int>> sink;
  bool isEOF = false;

  late final Pointer<Void> pStream;
  static const int outBufSize = 1024 * 64;

  _NativeZipStreamSink(
    this.zipAction,
    this.windowsBits,
    this.level,
    this.sink,
  ) {
    if (zipAction == 1) {
      pStream = _bindings.openZipStream(windowsBits, level);
    } else {
      pStream = _bindings.openUnzipStream(windowsBits);
    }
  }

  static final emptyBuf = List<int>.empty();
  void closeZipStream() {
    isEOF = true; // flush input, and read rest data
    add(emptyBuf); // flush input, and read rest data
    if (zipAction == 1) {
      _bindings.closeZipStream(pStream);
    } else {
      _bindings.closeUnzipStream(pStream);
    }
  }

  @override
  void close() {
    closeZipStream();
    sink.close();
  }

  @override
  void add(List<int> inBuf) {
    Pointer<Int8>? pInBuf; // native memory
    Pointer<Int8>? pOutBuf; // native memory

    if (inBuf.isEmpty) return;

    try {
      pInBuf = malloc<Int8>(inBuf.length);
      pInBuf.asTypedList(inBuf.length).setAll(0, inBuf); // copy bytes !!
      int outLen;

      pOutBuf = malloc<Int8>(outBufSize);
      outLen = _bindings.writeZipStream(
        pStream,
        zipAction,
        pInBuf,
        inBuf.length,
        pOutBuf,
        outBufSize,
        isEOF ? 1 : 0,
      );
      if (outLen > 0) {
        sink.add(pOutBuf.asTypedList(outLen, finalizer: malloc.nativeFree));
        pOutBuf = null;
      } else if (outLen < 0) {
        _throwExceptionByErrCode(outLen);
      }

      while (outLen == outBufSize) {
        // maybe more unread data, read again
        pOutBuf = malloc<Int8>(outBufSize);
        outLen = _bindings.writeZipStream_readNext(
          pStream,
          zipAction,
          pOutBuf,
          outBufSize,
          isEOF ? 1 : 0,
        );
        if (outLen > 0) {
          sink.add(pOutBuf.asTypedList(outLen, finalizer: malloc.nativeFree));
          pOutBuf = null;
        } else if (outLen < 0) {
          _throwExceptionByErrCode(outLen);
        }
      }

      malloc.free(pInBuf);
      pInBuf = null;
    } finally {
      if (pInBuf != null) malloc.free(pInBuf);
      if (pOutBuf != null) malloc.free(pOutBuf);
    }
  }

  void _throwExceptionByErrCode(int errCode) {
    switch (errCode) {
      case 2: //Z_NEED_DICT
        throw ZipStreamException(errCode,
            message: "Z_NEED_DICT: need dictionary");
      case -1: //Z_ERRNO
        throw ZipStreamException(errCode, message: "Z_ERRNO: unknown error");
      case -2: //Z_STREAM_ERROR
        throw ZipStreamException(errCode,
            message: "Z_STREAM_ERROR: wrong param or state");
      case -3: //Z_DATA_ERROR
        throw ZipStreamException(errCode, message: "Z_DATA_ERROR: data error");
      case -4: //Z_MEM_ERROR
        throw ZipStreamException(errCode,
            message: "Z_MEM_ERROR: not enough memory");
      case -5: //Z_BUF_ERROR
        throw ZipStreamException(errCode, message: "Z_BUF_ERROR: buffer error");
      case -6: //Z_VERSION_ERROR
        throw ZipStreamException(errCode,
            message: "ZipStreamException: zlib version incompatible");
    }
  }
}
