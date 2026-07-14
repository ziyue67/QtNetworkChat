@echo off
cd /d "D:\C++VS pro\QtNetworkChat"
git add -A
git commit -m "build: add contact/group widgets to build configs; stage ContactsView integration"
exit /b %errorlevel%
