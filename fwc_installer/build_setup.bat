@echo off
chcp 65001 > nul
cd /d "%~dp0"
rem 자동 설치 exe 만들기. 먼저 fwc_app 에서 dist_v17\GD_FWC, fwc_geode 에서 build\fwc.window-counter.geode 가 만들어져 있어야 함.
set PY=..\fwc_app\.venv_315\Scripts\python.exe
if not exist payload mkdir payload
copy /y ..\fwc_geode\build\fwc.window-counter.geode payload\ > nul
"%PY%" -c "import zipfile, os; src=r'..\fwc_app\dist_v17\GD_FWC'; z=zipfile.ZipFile(r'payload\GD_FWC.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6); [z.write(os.path.join(d,f), os.path.relpath(os.path.join(d,f),src)) for d,_,fs in os.walk(src) for f in fs]; z.close()"
"%PY%" -m PyInstaller --noconfirm --onefile --windowed --name GD_FWC_Setup --workpath build_setup --distpath dist_setup --add-data payload;payload fwc_setup.py
echo 결과: dist_setup\GD_FWC_Setup.exe
