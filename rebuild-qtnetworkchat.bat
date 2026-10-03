@echo off
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
set PATH=D:\Qt\5.15.2\msvc2019_64\bin;%PATH%
cd /d "D:\C++VS pro\QtNetworkChat"
cmake --build build-cmake-ninja --target QtNetworkChat -j 4
if %errorlevel% neq 0 exit /b %errorlevel%
windeployqt.exe build-cmake-ninja\QtNetworkChat.exe --dir build-cmake-ninja
