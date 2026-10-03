@echo off
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
set PATH=D:\Qt\5.15.2\msvc2019_64\bin;%PATH%
cd /d "D:\C++VS pro\QtNetworkChat"
qmake -v
qmake -o build-qt/Makefile QtNetworkChat.pro
