@echo off
chcp 65001 > nul
cd /d "%~dp0"
rem GD 프레임 윈도우 카운터 - 윈도우 .exe 만들기 (이 폴더에서 실행)
rem 필요: Python 3.10.1 이상 (3.10.0은 PyInstaller가 안 됨)
if not exist gui.py ( echo [오류] gui.py가 없어요. zip을 먼저 "모두 압축 풀기" 한 뒤 fwc_app 폴더 안에서 실행하세요. & goto :err )

rem 쓸 파이썬 고르기: 3.10.1 이상인 3.10이 있으면 그것(검증된 조합), 없으면 py -3 (가장 새 버전)
set "P310=%LOCALAPPDATA%\Programs\Python\Python310\python.exe"
set BASE=py -3
if not exist "%P310%" goto :nopy310
"%P310%" -c "import sys; sys.exit(0 if sys.version_info[:3] != (3,10,0) else 1)"
if errorlevel 1 goto :nopy310
set BASE="%P310%"
goto :havebase
:nopy310
where py > nul 2> nul
if errorlevel 1 ( echo [오류] py 명령이 없어요. python.org 에서 Python을 설치하세요. & goto :err )
:havebase

echo [1/4] 가상 환경 만들기
%BASE% mkvenv.py
if errorlevel 1 goto :err
set /p VPY=<venv_path.txt
if not exist "%VPY%" ( echo [오류] 가상 환경의 python을 못 찾았어요: %VPY% & goto :err )

echo [2/4] 필요한 모듈 설치 (몇 분 걸림)
"%VPY%" -m pip install --upgrade pip
"%VPY%" -m pip install PySide6 numpy pillow msgpack imageio-ffmpeg pyinstaller
if errorlevel 1 goto :err
"%VPY%" -c "import numpy, PySide6, PIL, msgpack, imageio_ffmpeg; print('모듈 불러오기 확인: numpy', numpy.__version__)"
if errorlevel 1 ( echo [오류] 설치한 모듈이 이 파이썬 버전에서 안 불러와져요. & goto :err )

echo [3/4] exe 만들기 (몇 분 걸림)
"%VPY%" -m PyInstaller --noconfirm --windowed --name GD_FWC --workpath build_v2 ^
  --paths engine --hidden-import gdplay --hidden-import partrun --hidden-import trigmove --hidden-import gdwave2 --hidden-import wave_sim --hidden-import showcase_patch --hidden-import export_lib ^
  --add-data "engine;engine" --add-data "fonts;fonts" --add-data "bin;bin" --collect-binaries imageio_ffmpeg --exclude-module scipy gui.py
if errorlevel 1 goto :err

echo.
echo [4/4] 시험: 리눅스에서 계산한 정답(tests\expected_linux_v3.json)과 비교
set FWC_SAC_PATCH=1
dist\GD_FWC\GD_FWC.exe --selftest tests\sac3.txt tests\sac_route.gdr2 tests\windows_result.png
"%VPY%" compare_test.py tests\expected_linux_v3.json tests\windows_result.json
echo.
echo 완료: dist\GD_FWC\GD_FWC.exe
pause
goto :eof
:err
echo.
echo 실패했어요. 위 메시지를 확인하세요.
pause
exit /b 1
