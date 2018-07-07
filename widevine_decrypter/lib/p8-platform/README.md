![Pulse-Eight logo](https://pulseeight.files.wordpress.com/2016/02/pulse-eight-logo-white-on-green.png?w=200)

# About
This library provides platform specific support for other libraries, and is used by libCEC and binary add-ons for Kodi

# Supported platforms

## Linux, BSD & Apple OS X
To compile this library on Linux, you'll need the following dependencies:
* [cmake 2.6 or better] (http://www.cmake.org/)
* a supported C++ 11 compiler

Follow these instructions to compile and install the library:
```
apt-get update
apt-get install cmake build-essential
git clone https://github.com/Pulse-Eight/platform.git
mkdir platform/build
cd platform/build
cmake ..
make -j4
sudo make install
sudo ldconfig
```

## Microsoft Windows
To compile this library on Windows, you'll need the following dependencies:
* [cmake 2.6 or better] (http://www.cmake.org/)
* [Visual Studio 2013 (v120) or 2015 (v140)] (https://www.visualstudio.com/)

Follow these instructions to compile and install the library:
```
git clone https://github.com/Pulse-Eight/platform.git
cd platform
git submodule update --init --recursive
cd 
build.cmd
```
