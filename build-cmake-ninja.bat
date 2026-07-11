@echo off
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
set PATH=D:\Qt\5.15.2\msvc2019_64\bin;D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
cd /d "D:\C++VS pro\QtNetworkChat"
if not exist build-cmake-ninja mkdir build-cmake-ninja
cmake -S . -B build-cmake-ninja -G Ninja -D CMAKE_PREFIX_PATH=D:\Qt\5.15.2\msvc2019_64 -D CMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build build-cmake-ninja -j 4
if %errorlevel% neq 0 exit /b %errorlevel%
windeployqt "D:\C++VS pro\QtNetworkChat\build-cmake-ninja\QtNetworkChat.exe" --no-translations
