@echo off
chcp 65001 > nul
rem FWC 지오드 모드 빌드 (내 PC에서). 필요: Visual Studio 2022 Build Tools(C++), CMake, Geode CLI
rem 처음 한 번만:
rem   1) Geode CLI 설치: https://github.com/geode-sdk/cli/releases 에서 받기 (winget install GeodeSDK.GeodeCLI 도 됨)
rem   2) geode sdk install          (SDK 받기, GEODE_SDK 환경 변수 자동 설정)
rem   3) geode sdk update v5.11.2   (게임 2.2081용 SDK 버전으로 맞춤)
rem   4) geode sdk install-binaries
cd /d %~dp0
rem CPM_SOURCE_CACHE: 빌드에 필요한 파일을 미리 받아 둔 폴더 (이 PC에서 cmake 직접 다운로드가 멈춰서 필요)
if exist "%USERPROFILE%\Documents\geode-cpm-cache" set "CPM_SOURCE_CACHE=%USERPROFILE%\Documents\geode-cpm-cache"
if "%GEODE_SDK%"=="" ( echo GEODE_SDK가 없어요. 위 1~4를 먼저 하세요. & pause & exit /b 1 )
geode build
if errorlevel 1 ( echo 빌드 실패 & pause & exit /b 1 )
echo.
echo 빌드 성공: build 폴더의 fwc.window-counter.geode 를 지메 폴더\geode\mods 에 넣으세요 (geode build 가 자동으로 넣기도 함)
pause
