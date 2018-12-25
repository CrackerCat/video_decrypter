# Video decrypter

This repository is inspired from https://github.com/xbmc/xbmc and https://github.com/peak3d/inputstream.adaptive.

Decrypt video from a streaming site with MPEG-DASH Widevine DRM encryption.

Compilation instructions on Windows :
* Download CMake from : **[CMake.org](https://cmake.org/download/)**
* Download MinGW-w64 POSIX : **[x86_64-8.1.0-release-posix-sjlj-rt_v6-rev0](https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/8.1.0/threads-posix/sjlj/x86_64-8.1.0-release-posix-sjlj-rt_v6-rev0.7z)**
* Extract zip and add the bin folder to Environment Variables PATH (User variables and Windows variables PATH).
* In widevine_decrypter/src/, create a **build**/ folder.
* With the <code>Windows Command Prompt</code> pointed to the build folder, run:

* `cmake .. -G "MinGW Makefiles"` to generate build Makefiles,
and do `make` to build the decrypter executable.

See the **[Wiki](https://github.com/x-hgg-x/video_decrypter/wiki)** for a running configuration
