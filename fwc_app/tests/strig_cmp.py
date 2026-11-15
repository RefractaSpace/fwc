"""strig_cmp.py — 예전 엔진(x로 계산)과 새 엔진(상태 트리거)으로 같은 매크로를 재생해서 틱마다 비교
사용: python3 strig_cmp.py 레벨.txt 매크로 [sac]
"""
import os, sys, subprocess, tempfile, importlib
APP = "/tmp/claude-0/-home-claude/e6151f8d-bc98-5784-a97e-871fe4a96554/scratchpad/fwc_app"
sys.path.insert(0, APP); sys.path.insert(0, APP + "/engine")
OLD = APP + "/bin_linux/windows"
NEW = os.path.dirname(os.path.abspath(__file__)) + "/windows_strig"

def export(level_text, strig, sac, tmp):
    os.environ["STRIG"] = "1" if strig else "0"
    import gdlevel
    cwd = os.getcwd(); os.chdir(APP + "/engine")
    try:
        for m in ("gdplay", "partrun", "export_lib"):
            if m in sys.modules: del sys.modules[m]
        import gdplay as P, export_lib, showcase_patch, gdwave2
        header, objs = gdlevel.parse_objects(level_text)
        P.apply_header(level_text)
        P.DUAL_BAND_OVERRIDE = []
        P.DUAL_BOUNDS_PLAYER = True; P.SPIDER_ENTRY_HOLD = True; P.TWIN_SPAWN_PUSH = True
        if sac:
            objs = showcase_patch.apply(objs, gdwave2.num)
            P.DUAL_BAND_OVERRIDE = [(31860.0, 32490.0, 1170.0, 1590.0)]
        else:
            objs = [dict(o, _noswap="1") if o["1"] == "392" else o for o in objs]
        hs = gdlevel.header_start(header)
        L = P.Level(objs)
        st = P.Player(0.0, 15.0, speed=hs["speed"], mini=hs["mini"], upside=hs["flip"], mode=hs["mode"])
        st.hit_ground(); st.bounds_around(15.0)
        path = tmp + ("/lv_new.txt" if strig else "/lv_old.txt")
        export_lib.dict_to_txt(export_lib.level_dict(P, L, st), path)
        return path, st, L
    finally:
        os.chdir(cwd)

def track(exe, lv, rules, inp, out):
    subprocess.run([exe, lv, rules, inp, out], env=dict(os.environ, PROBE="1", TRACK="1"), check=True)
    T = []; alive = None; cause = ""
    for l in open(out, encoding="utf-8", errors="replace"):
        a = l.split()
        if not a: continue
        if a[0] == "T": T.append((float(a[1]), float(a[2]), int(a[3])))
        elif a[0] == "ALIVE": alive = (int(a[1]), a[2] == "1")
        elif a[0] == "CAUSE": cause = l[6:].strip()
    return T, alive, cause

if __name__ == "__main__":
    import gdmacro
    lvtxt = open(sys.argv[1]).read().strip()
    m = gdmacro.load(sys.argv[2]); sac = len(sys.argv) > 3 and sys.argv[3] == "sac"
    off = int(os.environ.get("OFF", "0"))
    tmp = tempfile.mkdtemp(prefix="strig_")
    lo, st, _ = export(lvtxt, False, sac, tmp)
    ln, _, Ln = export(lvtxt, True, sac, tmp)
    print("상태 트리거: 트리거 %d개, 움직이는 그룹 %d개, 안 되는 것 %s" % (len(Ln.trigs), len(Ln.dyn_groups), Ln.todo))
    MODES = ["cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"]
    open(tmp + "/r.txt", "w").write("XEND 1e18\nSTARTMODE %d\nENDMODES %s\nENVMODES \nBAN \nCOSTS 24 24 14.0 1.0\n" % (MODES.index(st.mode), " ".join(map(str, range(8)))))
    inp = m.held_by_tick(offset=off)
    open(tmp + "/in.txt", "w").write(" ".join("1" if h else "0" for h in inp))
    To, ao, co = track(OLD, lo, tmp + "/r.txt", tmp + "/in.txt", tmp + "/to.txt")
    Tn, an, cn = track(NEW, ln, tmp + "/r.txt", tmp + "/in.txt", tmp + "/tn.txt")
    print("예전: %d틱 %s %s" % (ao[0], "죽음" if ao[1] else "끝까지", co))
    print("새것: %d틱 %s %s" % (an[0], "죽음" if an[1] else "끝까지", cn))
    first = None; maxd = 0.0; ndiff = 0
    for i, (a, b) in enumerate(zip(To, Tn)):
        d = max(abs(a[0] - b[0]), abs(a[1] - b[1]))
        if d > 0: ndiff += 1
        maxd = max(maxd, d)
        if d > 1e-6 and first is None: first = (i, a, b)
    print("비교 틱 %d, 다른 틱 %d, 최대 차이 %.3g" % (min(len(To), len(Tn)), ndiff, maxd))
    if first: print("처음 달라진 곳: 틱 %d 예전 %s 새것 %s" % first)
    print(tmp)
