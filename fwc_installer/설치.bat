@echo off
chcp 65001 > nul
cd /d "%~dp0"
rem FWC 간편 설치기 실행 (관리자 권한 필요 없음)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
