import 'dart:convert';
import 'dart:io';

import 'package:native_zip/native_zip.dart';

Future<void> runTestCases() async {
  final url = "https://github.com/nih-at/libzip/archive/refs/tags/v1.11.3.zip";
  final testDir = "d:/test/zip";
  final srcZipFile = "$testDir/__test_download.zip";
  final unzipDir = "$testDir/__unzip_dir";
  final zippedFile = "$testDir/__zip.zip";

  final textEntryFile = "libzip-1.11.3/android/readme.txt";
  final testUnzippedFile = "$unzipDir/$textEntryFile";
  ZipFile? zip;

  //remove files first
  print("delete previous testing files...");
  try {
    Directory(unzipDir).deleteSync(recursive: true);
    File(srcZipFile).deleteSync();
    File(zippedFile).deleteSync();
  } catch (_) {
    // do nothing
  }

  print("downloading test zip file...");
  await downloadFile(url, srcZipFile);

  // test unzipToDir()
  print("test unzipToDir() ...");
  await NativeZip.unzipToDir(srcZipFile, unzipDir);
  var isExists = await File(testUnzippedFile).exists();
  assert(isExists);

  // test zipDir()
  print("test zipDir() ...");
  await NativeZip.zipDir(unzipDir, zippedFile, skipTopLevel: true);
  var size = File(zippedFile).lengthSync();
  assert(size > 0);

  // test zip.getEntries()
  print("test zip.getEntries() ...");
  try {
    zip = NativeZip.openZipFile(zippedFile);
    //var zip = NativeZip.openZipFile(srcZipFile);
    var entries = zip.getEntries(path: "libzip-1.11.3/");
    assert(entries.length > 10);
  } finally {
    zip?.close();
    zip = null;
  }

  // test zip.openRead()
  print("test zip.openRead() ...");
  try {
    zip = NativeZip.openZipFile(zippedFile);
    var content = await zip
        .openRead(textEntryFile)
        .transform(utf8.decoder)
        .join();
    assert(
      content.contains("The libzip development team does not use Android"),
    );
  } finally {
    zip?.close();
    zip = null;
  }

  print("======== test finish ========");
}

Future<void> downloadFile(String url, String filepath) async {
  var httpClient = HttpClient();
  IOSink? fileSink;

  try {
    var request = await httpClient.getUrl(Uri.parse(url));
    var response = await request.close();

    fileSink = File(filepath).openWrite();
    await response.pipe(fileSink);
  } finally {
    httpClient.close();
    fileSink?.close();
  }
}
