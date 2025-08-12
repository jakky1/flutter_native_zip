# native_zip

A Flutter FFI plugin for fast, easy-to-use, multi-threaded ZIP file operations.<br>
Implemented in native C code, using well-known libraries [zlib](https://zlib.net/) and [libzip](https://libzip.org/).

## Platform Support

| Windows | Android | Linux | iOS | macOS | web
| :-----: | :-----: | :-----: | :-----: | :-----: | :-----: |
|    &#x2705;  |    &#x2705;   |    &#x2705;   |    &#x274c;<br>Help<br>Wanted   | &#x274c;<br>Help<br>Wanted | &#x274c;<br>Never<br>support |

## For iOS & macOS platform ( Help Wanted )

In theory, this package should be able to run on both iOS and macOS. Since I have no iOS or macOS devices, I would appreciate it if someone could help modify the relevant files such as the *.podspec, and generate the zlib and libzip shared library (.so) files for iOS.

## For Android platform

<details>
In Android platform, we ONLY support the following ABI:
- `arm64-v8a`: for 64-bit ARM CPU
- `x86_64`: for Android emulator on 64-bit Windows

To generate smaller APK file, run the following command:
```
flutter build apk --target-platform android-arm64
```

Or generate Android App Bundle (.aab) for uploading Google Play:
```
flutter build appbundle --target-platform android-arm64
```
</details>

## For Linux platform

<details>
In Ubuntu Linux (for example), the following packages need to be installed during development:
```
sudo apt install libzip-dev
sudo apt install zipcmp
sudo apt install zipmerge
sudo apt install ziptool
```
</details>

# Feature list
- Compress a folder into a .zip file
- Extract files from a .zip archive
- List all file properties inside a .zip archive
- Read file within a .zip archive
- Rename a file inside a .zip archive
- Move a file within a .zip archive
- Delete a file inside a .zip archive

## Multi-thread support
When you compress an entire folder into a .zip file or extract files from a .zip archive to a specified folder, you can specify the number of threads to speed up the compression or extraction process.

# Installation

Add this to your package's `pubspec.yaml` file:
```yaml
dependencies:
  native_zip:
```

# Easy-to-use Functions

## Compress a folder into a .zip file

```dart
var future = NativeZip.zipDir(
  "D:\\folder_path",
  "D:\\test.zip",
  password: "your-password",    // optional
  compressLevel: 5,  // optional
  threadCount: 8,    // optional
);
await future;
```

There are some optional arguments:
- `password`: set a password to protect .zip archive if necessary.
- `compressLevel`: The value ranges from 1 to 9. 1 represents the fastest compression speed but the lowest compression ratio. 9 represents the slowest compression speed but the highest compression ratio. Default value is `5`.
- `threadCount`: By default, the maximum number of CPU threads will be used.


If you want to display the progress during the operation:
```dart
showProgress(future);
await future;

void showProgress(ZipTaskFuture future) {
  var timer = Timer.periodic(const Duration(milliseconds: 500), (_) {
    var progress = future.processedSize / future.totalSize * 100;
    var compressRatio = future.compressedSize / future.processedSize * 100;
    var path = future.nowProcessingFilepath;
    log("progress: $progress%, compress ratio: $compressRatio%, path: $path");
  });
  future.whenComplete(() {
    log("finish");
    timer.cancel();
  });
}
```

Cancel the operation before finish:

```dart
future.cancel();
```

## Extract all files from a .zip archive

```dart
var future = NativeZip.unzipToDir(
  "D:\\test.zip", 
  "D:\\output_folder", 
  password: "password-of-zip",  // optional
  threadCount: 8,               // optional
);

showProgress(future); // to show progress, mentioned above
await future;
```

There are some optional arguments:
- `password`: set password if this .zip file is protected by password. Operation will be failed if password is incorrect.
- `threadCount`: By default, the maximum number of CPU threads will be used.

Call `showProgress()` mentioned above to display progress during operation.

Cancel the operation before finish:

```dart
future.cancel();
```


# Zip file operations

## Open zip file

```dart
ZipFile zip = NativeZip.openZipFile(
  "D:\\test.zip", 
  password: "password", // optional
);
```

For the optional argument `password`:
- When you open an existing .zip file, if the .zip file is protected by password, you MUST pass the correct password here; otherwise, the operation will fail.
- When you create a new .zip file, if you want to protect it with a password, you must provide the password here.


## Close zip file

Don't forget to close the zip file to free resources at last.

```dart
zip.close();
```


## Get file list

To get all files info within zip file:
```dart
var entries = zip.getEntries();
```

To get all files info under directory "flutter/" (recursively) within zip file:
```dart
var entries = zip.getEntries(
  path: "flutter/",  // optional, default is "" (root)
  recursive: true, // optional, default is true
);
```

To get all files info under directory "flutter/" (NO recursively) within zip file:
```dart
var entries = zip.getEntries(
  path: "flutter/docs/",  // optional, default is "" (root)
  recursive: false, // optional, default is true
);
```

NOTE:
- argument `path` must be:
  - empty string `""` (stands for root)
  - or, a path ends with `'/'` character
- all the `path` argument follow this rule

Print information of returned `entries`:
```dart
for (var e in entries) {
  // e.isDirectory
  // e.path
  // e.modifiedDateTime
  // e.originalSize
  // e.compressedSize

  // Stream<List<int>> stream = e.openRead();
}

```


## Read file content

Read file content in zip:
```dart
Stream<List<int>> content = zip.openRead("docs/README.md");
```

For example, reading a text file and print it:
```dart
String content = await zip
    .openRead("docs/README.md")
    .transform(utf8.decoder)
    .join();
print(content);
```


## Write file content

Due to technical limitations, there is no method like `zip.openWrite(...)` available, so writing from a Dart buffer directly into a zip archive is not supported.<br>
Please use the `zip.addFiles(...)` method described below to add files from the disk into the zip archive.

## Rename a file or directory

Rename directory dirA to dirB: ( <u>all directory path must ends with `'/` character</u> )
```dart
await zip.renameEntry("prefix/dirA/", "prefix/dirB/");
```

Rename file fileA.txt to fileB.txt:
```dart
await zip.renameEntry("prefix/fileA.txt", "prefix/fileB.txt");
```


## Move one or more file or directory

For example, move folder `flutter/DirA/` and folder `flutter/docs/DirB/` and file `prefix/fileA.txt` to folder `targetDirPath/` :
```dart
var entries = <String>["flutter/DirA/", "flutter/docs/DirB/", "prefix/fileA.txt"];
var toDirPath = "targetDirPath/";
await zip.moveEntries(entries, toDirPath);
```

NOTE: 
- all directory path must ends with `'/` character
- or a empty string `""` stands for root folder

Call `moveEntry` to move ONLY ONE file or directory:
```dart
await zip.moveEntry("flutter/DirA/", "targetDirPath/");
```


## Delete one or more file or directory

Remove directory `flutter/docs/` and file `README.md`
```dart
var entries = <String>["flutter/docs/", "README.md"];
await zip.removeEntries(entries);
```

Call `removeEntry` to remove ONLY ONE file or directory:
```dart
await zip.removeEntry("flutter/docs/");
```


## Add files/directories from disk into existing zip archive

For example, add directory `D:\\dirA` and file `C:\\fileB.txt`, into directory `targetDirInZip/` in zip archive:
```dart
var sourcesInDisk = <String>["D:\\dirA", "C:\\fileB.txt"];
var future = zip.addFiles(
  sourcesInDisk, 
  "targetDirInZip/",
  compressLevel: compressLevel, // optional
  skipTopLevel: skipTopLevel,   // optional
  threadCount: threadCount      // optional
);

showProgress(future); // to show progress, mentioned above
await future;
```

Refer to `NativeZip.zipDir()` mentioned above for details.


Cancel the operation before finish:

```dart
future.cancel();
```

## Extract files/directories from zip archive to disk

For example, copy directory `flutter/docs/` and file `flutter/README.md` from zip archive to disk directory `D:\\dir`:

```dart
var sourcesInZip = <String>["flutter/docs/", "flutter/README.md"];
var future = zip.saveFilesTo(
  sourcesInZip, 
  "D:\\dir",
  threadCount: threadCount, // optional
);

showProgress(future); // to show progress, mentioned above
await future;
```

Use `zip.saveTo()` instead to copy only ONE directory or file.

Refer to `NativeZip.unzipToDir()` mentioned above for details.

Cancel the operation before finish:

```dart
future.cancel();
```


## Rules for path string

- For a directory in disk:
  - path must NOT ends with `/` or `\\`
  - e.g. `/dirA` (Unix)
  - e.g. `D:\\dirA` (Windows) 
- For a directory in zip:
  - path must NOT starts with `/`
  - path MUST ends with `/`
  - e.g. `dirA/dirB/`
  - special case: empty string `""` stands for ROOT directory
- For a file in zip: 
  - path must NOT starts with `/`
  - path must NOT ends with `/`
  - e.g. `dirA/fileB.txt`


## Limitations

- Due to the limitation of zip format, the following information are ignored whe zipping and unzipping, and we won't support it:
  - All <u>symbolic link</u> files or directories are ignored.
  - The inode information of a hard link file are ignored.
  - Unix file permission information (e.g. -rwxrw-r--) are ignored