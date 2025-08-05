import 'dart:convert';
import 'dart:developer';
import 'dart:io';

//import 'package:archive/archive.dart';
//import 'package:archive/archive_io.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'dart:async';

import 'package:native_zip/native_zip.dart';
import 'package:native_zip/native_zip_bindings_generated.dart';

void main() {
  runApp(const MyApp());
}

class Stopwatch {
  int _now = 0;
  Stopwatch() {
    _now = DateTime.now().millisecondsSinceEpoch;
  }
  void step(String msg) {
    int start = _now;
    _now = DateTime.now().millisecondsSinceEpoch;
    int diff = _now - start;
    log("$msg -- time elapsed: $diff ms");
  }
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  late Future<int> sumAsyncResult;

  @override
  void initState() {
    super.initState();
    test();
  }

  @override
  void reassemble() {
    super.reassemble();
    test();
  }

  void onTestButtonClick() async {
    try {
      //await test_zipDir(); // OK 2

      test_zip_entry_rename();
      test_zip_read_big();
      //test_zip_get_entries();
    } catch (e) {
      log("@@@@@@@@@@@@@@@@ exception: $e");
    }
  }

  final _sw = Stopwatch();
  Future<void> test() async {
    try {
      //initDartLog();
      //test_ZipStream();
      //test_Zip();
      //test_zip_read();

      //await test_zipDir(); // OK 2
      //await test_unzipDir(); // OK 2

      //await test_zip_entry_rename(); // OK 2 // rename root dir 比 7-zip 耗時...
      //await test_zip_entry_move(); // OK 2
      //await test_zip_entry_remove(); // OK 2
    } catch (e) {
      log("@@@@@@@@@@@@@@@@ Exception: $e");
    }
  }

  Future<void> test_zip_entry_rename() async {
    var zipPath = "d:\\test\\zip\\flutter.zip";
    var oldEntryPath = "notFound/";
    var newEntryPath = "flutter/kk中文/";
    await NativeZip.openZipFile(
      zipPath,
    ).renameEntry(oldEntryPath, newEntryPath);
    _sw.step("renameEntry() finish");
  }

  Future<void> test_zip_entry_move() async {
    var zipPath = "d:\\test\\zip\\flutter.zip";
    var entries = <String>["flutter/tt暫存/docs/", "flutter/tt暫存/README.md"];
    var toDirPath = "";
    await NativeZip.openZipFile(zipPath).moveEntries(entries, toDirPath);
    _sw.step("moveEntries() finish");
  }

  Future<void> test_zip_entry_remove() async {
    var zipPath = "d:\\test\\zip\\flutter.zip";
    var entries = <String>["README.md"];
    await NativeZip.openZipFile(zipPath).removeEntries(entries);
    _sw.step("removeEntries() finish");
  }

  //

  Future<void> test_zipDir() async {
    var dirPath = "c:\\flutter";
    var zipPath = "d:\\test\\zip\\flutter.zip";
    log("zipDir() start...");
    var future = NativeZip.zipDir(
      dirPath,
      zipPath,
      compressLevel: 5,
      //password: "12345678",
    );
    test_showProgress(future);
    await future;
    _sw.step("zipDir() finish");
  }

  Future<void> test_unzipDir() async {
    var zipPath = "d:\\test\\zip\\flutter.zip";
    var dirPath = "d:\\test\\zip\\flutter_unzip";
    log("unzipDir() start...");
    var future = NativeZip.unzipToDir(zipPath, dirPath, password: "12345678");
    test_showProgress(future);
    await future;
    _sw.step("unzipDir() finish");
  }

  void test_showProgress(ZipTaskFuture future) {
    var timer = Timer.periodic(const Duration(milliseconds: 500), (_) {
      var progress = future.processedSize / future.totalSize * 100;
      var compressRatio = future.compressedSize / future.processedSize * 100;
      var path = future.nowProcessingFilepath;
      log("progress: $progress%, compress ratio: $compressRatio%, path: $path");
    });
    future.whenComplete(() {
      log("================= finish ===================");
      timer.cancel();
    });
  }

  Future<void> test_zip_read() async {
    var zip = NativeZip.openZipFile("d:\\test\\zip\\flutter.zip");

    var content = await zip
        //.openRead("flutter/kk中文/LICENSE")
        .openRead("flutter/docs/README.md")
        .transform(utf8.decoder)
        .join();
    print(content);
  }

  Future<void> test_zip_read_big() async {
    var outFile = "d:\\test\\zip\\out.tmp";
    var zip = NativeZip.openZipFile("d:\\test\\zip\\flutter.zip");

    var fileSink = File(outFile).openWrite();
    await zip
        .openRead(
          "flutter/bin/cache/artifacts/engine/windows-x64/flutter_windows.dll.pdb",
        )
        .pipe(fileSink);
    await fileSink.close();
    zip.close();
    log("test_zip_read_big() finish");
  }

  void test_zip_get_entries() async {
    var zip = NativeZip.openZipFile("d:\\test\\zip\\flutter.zip");

    var entries = zip.getEntries(
      //path: "flutter/docs/contributing/",
      path: "flutter",
      recursive: false,
    );
    print("### count: ${entries.length}");
    for (var e in entries) {
      print(
        "==> ${e.originalSize}, ${DateFormat("yyyy-MM-dd HH:mm:ss").format(e.modifiedDateTime)} , ${e.path}",
      );
    }
  }

  Future<void> test_ZipStream() async {
    List<int> list = utf8.encode("333abcdefg");
    var cc = await Stream.value(list)
        .transform(NativeZip.gzip)
        .transform(NativeZip.gunzip)
        .expand((x) => x)
        .toList();
    var ss = utf8.decode(cc);
    log("ccc = $ss");
    if (true) return;

    var srcFile = "D:/license.txt";
    //var srcFile = "D:/novel.txt";
    var zipFile = "D:/zipped.txt";
    var unzipFile = "D:/unzipped.txt";

    var zipSink = File(zipFile).openWrite();
    await File(srcFile).openRead().transform(NativeZip.gzip).pipe(zipSink);
    await zipSink.close();

    var unzipSink = File(unzipFile).openWrite();
    await File(zipFile).openRead().transform(NativeZip.gunzip).pipe(unzipSink);
    await unzipSink.close();
  }

  @override
  Widget build(BuildContext context) {
    Widget body = Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        CircularProgressIndicator(),
        SizedBox.square(dimension: 30),
        ElevatedButton(onPressed: onTestButtonClick, child: Text("Test")),
      ],
    );

    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('Native Packages')),
        body: Center(child: body),
      ),
    );
  }
}
