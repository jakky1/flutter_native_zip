part of 'native_zip.dart';

typedef ListIntStreamTransformer = StreamTransformer<List<int>, List<int>>;

class _NativeZipStreamTransformer
    extends StreamTransformerBase<List<int>, List<int>> {
  final int zipAction;
  final int windowsBits;
  final int level; // -1:default, 0:no_compression, 1:fast, 9:best_compression

  const _NativeZipStreamTransformer(
    this.zipAction, [
    this.windowsBits = 8,
    this.level = -1,
  ]);

  @override
  Stream<List<int>> bind(Stream<List<int>> stream) {
    if (level < -1 || level > 9 || level == 0) {
      throw ZipStreamException(
        -99,
        message: "invalid compress level: $level",
      );
    }

    return Stream<List<int>>.eventTransformed(
      stream,
      (sink) => _NativeZipStreamSink(zipAction, windowsBits, level, sink),
    );
  }
}

class _NativeZipStreamSink implements EventSink<List<int>> {
  // zipAction: 1=>zip , 0=>unzip
  final int zipAction;
  final int windowsBits;
  final int level; // only works when compress
  final EventSink<List<int>> sink;
  bool isEOF = false;

  late final Pointer<Void> pStream;
  static const int outBufSize = 1024 * 2;

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
  void addError(e, [st]) {
    closeZipStream();
    sink.addError(e, st);
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
        throw ZipStreamException(outLen);
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
          throw ZipStreamException(outLen);
        }
      }

      malloc.free(pInBuf);
      pInBuf = null;
    } finally {
      if (pInBuf != null) malloc.free(pInBuf);
      if (pOutBuf != null) malloc.free(pOutBuf);
    }
  }
}
