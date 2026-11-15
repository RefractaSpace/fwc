"""mkvenv.py - build.bat가 부름. 지금 파이썬 버전 이름으로 가상 환경 폴더를 만들고(.venv_310, .venv_315 ...)
그 안의 python 경로를 venv_path.txt에 적는다. 버전마다 폴더가 달라서 다른 버전 모듈이 섞이지 않는다."""
import os, sys, venv
name = ".venv_%d%d" % sys.version_info[:2]
nt = os.name == "nt"
py = os.path.join(name, "Scripts" if nt else "bin", "python.exe" if nt else "python")
if not os.path.exists(py):
    venv.create(name, with_pip=True)
with open("venv_path.txt", "w", encoding="ascii") as f:
    f.write(py + "\n")
print("Python %s -> %s" % (sys.version.split()[0], name))
