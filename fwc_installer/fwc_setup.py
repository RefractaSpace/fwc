# -*- coding: utf-8 -*-
"""GD 프레임 윈도우 카운터 — 자동 설치 프로그램 (exe 하나로 묶어서 배포)

하는 일
  1) 앱을 내 계정 폴더(%LOCALAPPDATA%\\Programs\\GD_FWC)에 풀기
  2) 바탕 화면 바로 가기 만들기
  3) 지오드가 깔린 지오메트리 대시를 찾아 모드 파일을 geode\\mods 에 넣기
안 하는 일
  게임 원본·저장 파일 수정, 파일 삭제, 인터넷 다운로드, 관리자 권한 요구

시험용 인자 (창 없이 실행):
  --quiet               창을 띄우지 않고 바로 설치, 결과는 fwc_setup_log.txt (exe 옆)
  --dir <폴더>          앱을 풀 폴더
  --no-shortcut         바로 가기 안 만듦
  --no-mod              모드 안 넣음
  --mods-dir <폴더>     모드를 넣을 폴더를 직접 지정 (지메 찾기 건너뜀)
"""
import os
import re
import sys
import glob
import shutil
import zipfile
import subprocess
import threading
import traceback

APP_NAME = "GD 프레임 윈도우 카운터"
MOD_FILE = "fwc.window-counter.geode"
EXE_NAME = "GD_FWC.exe"


def res_dir():
    """묶인 파일(payload)이 있는 폴더. exe로 묶였으면 임시 폴더, 아니면 이 파일 옆."""
    return getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))


def default_dir():
    base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    return os.path.join(base, "Programs", "GD_FWC")


def steam_roots():
    """스팀 폴더 후보: 기본 위치 + libraryfolders.vdf 에 적힌 다른 보관함."""
    roots = []
    for env in ("ProgramFiles(x86)", "ProgramFiles"):
        p = os.environ.get(env)
        if p:
            r = os.path.join(p, "Steam")
            if os.path.isdir(r) and r not in roots:
                roots.append(r)
    for r in list(roots):
        vdf = os.path.join(r, "steamapps", "libraryfolders.vdf")
        if os.path.isfile(vdf):
            try:
                with open(vdf, encoding="utf-8", errors="replace") as f:
                    for line in f:
                        m = re.search(r'"path"\s+"(.+)"', line)
                        if m:
                            p = m.group(1).replace("\\\\", "\\")
                            if os.path.isdir(p) and p not in roots:
                                roots.append(p)
            except OSError:
                pass
    return roots


def find_games():
    """지메 폴더 목록: [(폴더, 실행 파일 이름, 지오드 있음?)]"""
    cands = []
    for r in steam_roots():
        cands.append(os.path.join(r, "steamapps", "common", "Geometry Dash"))
        dep = os.path.join(r, "steamapps", "content", "app_322170")
        if os.path.isdir(dep):
            cands += sorted(glob.glob(os.path.join(dep, "depot_*")))
    games = []
    for c in cands:
        if not os.path.isdir(c):
            continue
        exes = sorted(glob.glob(os.path.join(c, "GeometryDash*.exe")))
        if not exes:
            continue
        games.append((c, os.path.basename(exes[0]), os.path.isdir(os.path.join(c, "geode", "mods"))))
    return games


def make_shortcut(exe, workdir, log):
    """바탕 화면 바로 가기. 윈도우에 들어 있는 파워셸로 만든다 (추가 설치 없음)."""
    ps = (
        "$d=[Environment]::GetFolderPath('Desktop');"
        "$l=Join-Path $d '%s.lnk';"
        "$w=New-Object -ComObject WScript.Shell;"
        "$s=$w.CreateShortcut($l);$s.TargetPath='%s';$s.WorkingDirectory='%s';$s.Save();"
        "Write-Output $l"
    ) % (APP_NAME, exe.replace("'", "''"), workdir.replace("'", "''"))
    flags = 0x08000000 if os.name == "nt" else 0      # 검은 창 안 띄움
    pr = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps],
                        capture_output=True, text=True, creationflags=flags)
    if pr.returncode != 0:
        log("  [주의] 바로 가기를 못 만들었어요: %s" % (pr.stderr.strip().splitlines() or ["?"])[-1])
        return False
    log("  바로 가기: %s" % pr.stdout.strip())
    return True


def install(log, target, shortcut=True, mod=True, mods_dir=None, progress=None):
    """설치 본체. 돌려주는 값: (성공?, 앱 실행 파일 경로)"""
    payload = os.path.join(res_dir(), "payload")
    app_zip = os.path.join(payload, "GD_FWC.zip")
    mod_src = os.path.join(payload, MOD_FILE)
    if not os.path.isfile(app_zip):
        log("[오류] 설치 파일 안에 앱이 없어요 (payload\\GD_FWC.zip).")
        return False, None

    # 1) 앱 풀기 (같은 이름 파일은 덮어쓰고, 지우지는 않음)
    log("[1/3] 앱 설치 → %s" % target)
    os.makedirs(target, exist_ok=True)
    with zipfile.ZipFile(app_zip) as z:
        names = z.namelist()
        tgt_abs = os.path.abspath(target)
        for i, nm in enumerate(names):
            dst = os.path.abspath(os.path.join(target, nm))
            if not (dst == tgt_abs or dst.startswith(tgt_abs + os.sep)):
                log("[오류] 이상한 경로가 들어 있어 멈춤: %s" % nm)
                return False, None
            z.extract(nm, target)
            if progress and i % 50 == 0:
                progress(i / max(1, len(names)))
    exe = os.path.join(target, EXE_NAME)
    if not os.path.isfile(exe):
        log("[오류] 풀기 실패: %s 가 없어요." % exe)
        return False, None
    log("  파일 %d개 설치됨" % len([n for n in names if not n.endswith("/")]))
    if progress:
        progress(1.0)

    # 2) 바로 가기
    if shortcut:
        log("[2/3] 바탕 화면 바로 가기")
        make_shortcut(exe, target, log)
    else:
        log("[2/3] 바로 가기: 건너뜀")

    # 3) 지오드 모드
    log("[3/3] 지오드 모드")
    if not mod:
        log("  건너뜀")
    elif not os.path.isfile(mod_src):
        log("  설치 파일에 모드가 없어서 건너뜀 (앱만 설치됨)")
    elif mods_dir:
        os.makedirs(mods_dir, exist_ok=True)
        shutil.copy2(mod_src, os.path.join(mods_dir, MOD_FILE))
        log("  모드 넣음: %s" % os.path.join(mods_dir, MOD_FILE))
    else:
        games = find_games()
        if not games:
            log("  지오메트리 대시를 찾지 못했어요. 모드는 건너뜀 (앱은 설치됨).")
        ok = [g for g in games if g[2]]
        for d, x, has in games:
            log("  찾음: %s\\%s  [%s]" % (d, x, "지오드 있음" if has else "지오드 없음"))
        if games and not ok:
            log("  지오드 로더가 깔린 지메가 없어요. 지오드(v5.11.2, 지메 2.2081용)를 먼저 깔고 다시 실행하세요.")
        for d, x, has in ok:
            dst = os.path.join(d, "geode", "mods", MOD_FILE)
            try:
                shutil.copy2(mod_src, dst)
                log("  모드 넣음: %s" % dst)
            except OSError as e:
                log("  [주의] 모드를 못 넣었어요 (%s): %s" % (dst, e))
    log("")
    log("설치 끝. 모드는 지오드 v5.11.2 이상 + 지메 2.2081 에서만 켜집니다.")
    log("지메가 켜져 있었다면 껐다가 다시 켜야 모드가 보입니다.")
    return True, exe


# ───────────────────────── 창 ─────────────────────────
def run_gui():
    import tkinter as tk
    from tkinter import ttk, filedialog

    root = tk.Tk()
    root.title(APP_NAME + " 설치")
    root.geometry("640x460")
    root.resizable(False, False)

    frm = ttk.Frame(root, padding=14)
    frm.pack(fill="both", expand=True)
    ttk.Label(frm, text=APP_NAME + " 설치", font=("Malgun Gothic", 14, "bold")).pack(anchor="w")
    ttk.Label(frm, text="앱과 지오드 모드를 설치합니다. 게임 파일은 고치지 않고, 인터넷도 쓰지 않습니다.",
              font=("Malgun Gothic", 9)).pack(anchor="w", pady=(2, 10))

    row = ttk.Frame(frm)
    row.pack(fill="x")
    ttk.Label(row, text="설치 폴더", font=("Malgun Gothic", 9)).pack(side="left")
    var_dir = tk.StringVar(value=default_dir())
    ent = ttk.Entry(row, textvariable=var_dir)
    ent.pack(side="left", fill="x", expand=True, padx=6)

    def pick():
        d = filedialog.askdirectory(initialdir=os.path.dirname(var_dir.get()) or None)
        if d:
            var_dir.set(os.path.join(os.path.normpath(d), "GD_FWC"))
    ttk.Button(row, text="바꾸기", command=pick).pack(side="left")

    var_sc = tk.BooleanVar(value=True)
    var_mod = tk.BooleanVar(value=True)
    ttk.Checkbutton(frm, text="바탕 화면에 바로 가기 만들기", variable=var_sc).pack(anchor="w", pady=(8, 0))
    ttk.Checkbutton(frm, text="지오드 모드 넣기 (게임 안 실시간 표시)", variable=var_mod).pack(anchor="w")

    bar = ttk.Progressbar(frm, maximum=100)
    bar.pack(fill="x", pady=(10, 6))
    txt = tk.Text(frm, height=12, font=("Malgun Gothic", 9), state="disabled", wrap="word")
    txt.pack(fill="both", expand=True)

    btns = ttk.Frame(frm)
    btns.pack(fill="x", pady=(10, 0))
    b_run = ttk.Button(btns, text="실행하기")
    b_close = ttk.Button(btns, text="닫기", command=root.destroy)
    b_go = ttk.Button(btns, text="설치")
    b_close.pack(side="right")
    b_go.pack(side="right", padx=6)
    state = {"exe": None}

    def log(s):
        def f():
            txt.configure(state="normal")
            txt.insert("end", s + "\n")
            txt.see("end")
            txt.configure(state="disabled")
        root.after(0, f)

    def prog(p):
        root.after(0, lambda: bar.configure(value=p * 100))

    def work():
        try:
            ok, exe = install(log, var_dir.get().strip(), var_sc.get(), var_mod.get(), progress=prog)
        except Exception as e:
            ok, exe = False, None
            log("[오류] %s" % e)
            log(traceback.format_exc(limit=2))
        state["exe"] = exe if ok else None

        def fin():
            b_go.configure(state="normal", text="다시 설치")
            if state["exe"]:
                b_run.pack(side="right")
        root.after(0, fin)

    def go():
        b_go.configure(state="disabled")
        threading.Thread(target=work, daemon=True).start()

    def run_app():
        if state["exe"]:
            subprocess.Popen([state["exe"]], cwd=os.path.dirname(state["exe"]))
            root.destroy()

    b_go.configure(command=go)
    b_run.configure(command=run_app)
    root.mainloop()


def main():
    a = sys.argv[1:]
    if "--quiet" in a:
        def val(k, d=None):
            return a[a.index(k) + 1] if k in a and a.index(k) + 1 < len(a) else d
        base = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))
        lines = []
        try:
            ok, _ = install(lines.append, val("--dir", default_dir()), "--no-shortcut" not in a, "--no-mod" not in a, val("--mods-dir"))
        except Exception as e:
            ok = False
            lines.append("[오류] %s\n%s" % (e, traceback.format_exc(limit=3)))
        try:
            with open(os.path.join(base, "fwc_setup_log.txt"), "w", encoding="utf-8") as f:
                f.write("\n".join(lines) + "\n")
        except OSError:
            pass
        sys.exit(0 if ok else 1)
    run_gui()


if __name__ == "__main__":
    main()
