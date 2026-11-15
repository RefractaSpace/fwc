"""analyze.py — 레벨 + 매크로 → 누름마다 프레임 윈도우, 칸별 개수, σ/s (프로그램의 계산 부분, 내가 짠 코드)

순서:
  1) 레벨 문자열 → 오브젝트, 헤더 설정 (gdlevel)
  2) 매크로 → 틱마다 누름 (gdmacro, 240틱만)
  3) 우리 엔진(gdplay)으로 매크로를 그대로 재생 → 어디까지 살아남나
     (엔진이 실제 게임과 다르면 매크로가 중간에 '죽음' → 그 전까지만 계산하고 알려 줌)
  4) C++ 윈도우 계산기(windows / windows.exe, MEASURE_ONLY=1: 매크로를 고치지 않고 재기만)
     누름(뗌)마다 몇 틱 일찍·늦게 눌러도 살아서 같은 길로 돌아오나 = 윈도우
  5) σ/s (NaNDL 식, precision 계산과 같음)
"""
import os, sys, json, math, subprocess, tempfile, time
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(HERE, "engine")
if ENGINE not in sys.path: sys.path.insert(0, ENGINE)

MODES = ["cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"]
KINDS = ["solid", "hazard", "saw", "slope", "orb", "pad", "portal", "mode"]
ORBA = ["yellow", "pink", "red", "blue", "green", "black", "dash", "gravity_dash", "spider"]
PADA = ["yellow", "pink", "red", "blue", "spider"]
BINS = [("17+", 17, 10 ** 9), ("13+", 13, 16), ("9+", 9, 12), ("7-8", 7, 8), ("5-6", 5, 6), ("4", 4, 4), ("3", 3, 3), ("2", 2, 2), ("1", 1, 1)]


def bin_name(w):
    for n, a, b in BINS:
        if a <= w <= b: return n
    return "1"


class Result:
    pass


def _load_engine():
    cwd = os.getcwd(); os.chdir(ENGINE)               # 엔진은 gd_table.json을 지금 폴더에서 읽음
    try:
        import gdplay as P, partrun as R
    finally:
        os.chdir(cwd)
    return P, R


def _windows_exe():
    if os.environ.get("FWC_WINDOWS_BIN"): return os.environ["FWC_WINDOWS_BIN"]   # 시험용
    for d, n in ((("bin", "windows.exe"),) if os.name == "nt" else (("bin_linux", "windows"), ("bin", "windows"))):
        p = os.path.join(HERE, d, n)
        if os.path.exists(p): return p
    raise FileNotFoundError("윈도우 계산기(bin/windows.exe)가 없음")


# ---------------------------------------------------------------- 레벨 → C++ 텍스트 (파이프라인과 같은 export_lib 사용)
def export_level_txt(P, L, start, path):
    import export_lib                                   # engine 폴더 (파이프라인 cpp/export_lib.py 복사본)
    export_lib.dict_to_txt(export_lib.level_dict(P, L, start), path)


def export_rules_txt(R, part, path):
    R_ = repr
    f = open(path, "w", encoding="utf-8")
    f.write("XEND %s\n" % R_(float(part.x_end)))
    f.write("STARTMODE %d\n" % MODES.index(part.start.mode))
    f.write("ENDMODES %s\n" % " ".join(str(MODES.index(m)) for m in (part.end_modes or MODES)))
    f.write("ENVMODES \nBAN \n")
    f.write("COSTS %d %d %s %s\n" % (R.PRESS_COST, R.PRESS_COST, R_(14.0), R_(1.0)))
    for c, v in part.floor_c.items(): f.write("FLOORC %d %s\n" % (c, R_(float(v))))
    for px, md in part.mode_marks: f.write("MARK %s %d\n" % (R_(float(px)), MODES.index(md)))
    f.close()


# ---------------------------------------------------------------- σ/s (NaNDL 식)
def precision(windows_ticks, times, respawn=1.0, f=240.0, target=86400.0):
    erf = np.vectorize(math.erf)                               # scipy 없이 (프로그램 크기 줄이기)
    w = np.array(windows_ticks, float) / f; t = np.array(times, float)
    if len(w) == 0: return 0.0
    def et(L):
      with np.errstate(over="ignore", divide="ignore", invalid="ignore"):
        p = np.clip(erf(0.5 * w * L / math.sqrt(2.0)), 1e-300, 1.0)
        r = np.concatenate(([1.0], np.cumprod(p)[:-1])); PC = float(np.prod(p))
        if PC <= 0: return math.inf
        v = (t[-1] * PC + float(np.sum(t * r * (1 - p))) + respawn) / PC
        return v if math.isfinite(v) else math.inf
    lo, hi = 1e-3, 1e5
    if et(lo) <= target: return lo
    for _ in range(200):
        mid = math.sqrt(lo * hi)
        if et(mid) > target: lo = mid
        else: hi = mid
        if hi / lo < 1 + 1e-10: break
    return math.sqrt(lo * hi)


# ---------------------------------------------------------------- 전체
_TP_RECOLLIDE = "1"; _PAD_ONE = "1"; _MOVE_LAG = "1"; _TP_KEEP = "1"; _SNAP_CUBE = "1"; _SLOPE_TOP = "1"; _SHIP_ORB = "1"; _WAVE_SLOPE = "1"; _SPORB = "1"
def _run_tool(args, env_extra):
    env = dict(os.environ, **env_extra); env["TP_RECOLLIDE"] = _TP_RECOLLIDE; env["SPORB_HOLD"] = _SPORB; env["PAD_ONE"] = _PAD_ONE; env["MOVE_LAG"] = _MOVE_LAG; env["TP_KEEP"] = _TP_KEEP; env["SNAP_CUBE"] = _SNAP_CUBE; env["SLOPE_TOP"] = _SLOPE_TOP; env["SHIP_ORB"] = _SHIP_ORB; env["WAVE_SLOPE"] = _WAVE_SLOPE
    kw = {"creationflags": 0x08000000} if os.name == "nt" else {}   # 검은 창 안 띄움
    pr = subprocess.run([_windows_exe()] + args, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", **kw)
    if pr.returncode != 0:
        raise RuntimeError("윈도우 계산기 오류: " + (pr.stderr or "").strip()[-500:])
    return pr


def analyze(level_string, macro, offset=None, noswap392=True, log=print, sac_patch=False):
    """offset = None이면 자동: -2~+2틱 중 매크로가 가장 오래 살아남는 값 (같으면 0에 가까운 값)"""
    import gdlevel
    P, R = _load_engine()
    t0 = time.time()
    res = Result(); res.warnings = []
    if abs(macro.tps - 240.0) > 1e-6:
        raise ValueError("이 매크로는 %.0f틱으로 녹화됨. 지금은 240틱 매크로만 계산할 수 있어요." % macro.tps)
    if macro.platformer:
        raise ValueError("플랫포머 매크로는 아직 지원하지 않아요.")
    header, objs = gdlevel.parse_objects(level_string)
    P.apply_header(level_string)
    P.DUAL_BAND_OVERRIDE = []                                  # 특정 레벨용 값은 끔
    P.SPIDER_ENTRY_HOLD = True; P.TWIN_SPAWN_PUSH = True       # 지금까지 쇼케이스로 맞춘 엔진 설정 (추정 포함)
    # 실제 게임 기록·게임 소스로 확인한 규칙 3가지. 평소 분석에서는 켜고, sac 자체 시험(sac_patch)에서는 끔:
    #   sac 기준 경로는 게임에서 녹화한 것이 아니라 예전 엔진 규칙으로 만든 경로라서, 예전 규칙에서만 끝까지 감
    legacy = bool(sac_patch)
    P.DUAL_BOUNDS_PLAYER = os.environ.get("FWC_DBP", "1" if legacy else "0") == "1"   # 듀얼 바닥·천장: 꺼짐 = 듀얼 포탈 높이 기준 (게임 소스 + Hexagon Force)
    P.TP_OBB = os.environ.get("TP_OBB", "0" if legacy else "1") == "1"                 # 비스듬한 텔레포트 포탈을 돌린 상자로 (Deadlocked 30초)
    global _TP_RECOLLIDE; _TP_RECOLLIDE = os.environ.get("TP_RECOLLIDE", "0" if legacy else "1")   # 텔레포트한 틱에 도착 자리 패드·포탈 처리 (Deadlocked 30초)
    P.TP_RECOLLIDE = _TP_RECOLLIDE == "1"
    P.ORB_MIN36 = os.environ.get("ORB_MIN36", "0" if legacy else "1") == "1"   # 줄인 오브 판정 크기 유지 (end 6 실제 매크로)
    global _WAVE_SLOPE; _WAVE_SLOPE = os.environ.get("WAVE_SLOPE", "0" if legacy else "1"); P.WAVE_SLOPE_SLIDE = _WAVE_SLOPE == "1"   # 웨이브 경사 미끄러짐 (end 6 실제 매크로)
    global _SPORB; _SPORB = os.environ.get("SPORB_HOLD", "0" if legacy else "1"); P.SPORB_HOLD = _SPORB == "1"   # 거미 오브에 쓴 누름으로 다음 틱에 또 점프 안 함
    global _SHIP_ORB; _SHIP_ORB = os.environ.get("SHIP_ORB", "0" if legacy else "1"); P.SHIP_ORB_CLICK = _SHIP_ORB == "1"   # 비행기 오브는 새로 눌러야 작동 (end 6 실제 매크로)
    global _SLOPE_TOP; _SLOPE_TOP = os.environ.get("SLOPE_TOP", "0" if legacy else "1"); P.SLOPE_TOP_SKIP = _SLOPE_TOP == "1"   # 경사 꼭대기 끝 규칙 (Deadlocked 60초)
    global _SNAP_CUBE; _SNAP_CUBE = os.environ.get("SNAP_CUBE", "0" if legacy else "1"); P.SNAP_CUBE_ONLY = _SNAP_CUBE == "1"   # 계단 스냅 큐브 전용 (게임 소스 + Deadlocked 56초)
    global _TP_KEEP; _TP_KEEP = os.environ.get("TP_KEEP", "0" if legacy else "1"); P.TP_KEEP_BOUNDS = _TP_KEEP == "1"   # 텔레포트 때 바닥·천장 유지 (Deadlocked 37초)
    global _MOVE_LAG; _MOVE_LAG = os.environ.get("MOVE_LAG", "0" if legacy else "1")   # 가로 위치 무브 시작 시점 (Deadlocked 26.6초)
    global _PAD_ONE; _PAD_ONE = os.environ.get("PAD_ONE", "0" if legacy else "1"); P.PAD_ONE_PER_TICK = _PAD_ONE == "1"   # 패드는 한 틱에 하나만 (Deadlocked 30초)
    P.PAD_OBB = os.environ.get("PAD_OBB", "0" if legacy else "1") == "1"               # 비스듬한 패드를 돌린 상자로 (Deadlocked 30초)
    if sac_patch:                                              # 시험용: sacrilegium 쇼케이스 고침 그대로
        cwd = os.getcwd(); os.chdir(ENGINE)
        try:
            import showcase_patch, gdwave2
            objs = showcase_patch.apply(objs, gdwave2.num)
        finally: os.chdir(cwd)
        P.DUAL_BAND_OVERRIDE = [(31860.0, 32490.0, 1170.0, 1590.0)]
    elif noswap392:
        objs = [dict(o, _noswap="1") if o["1"] == "392" else o for o in objs]
    hs = gdlevel.header_start(header)
    two_p = dict(zip(header.split(",")[0::2], header.split(",")[1::2])).get("kA10", "0") == "1"   # 2인용 레벨 (gddocs: kA10, 추정)
    log("레벨: 오브젝트 %d개, 시작 %s" % (len(objs), hs["mode"]))
    P.STATE_TRIG = os.environ.get("FWC_STRIG", "1") != "0"     # 트리거를 틱마다 상태로 계산 (생성·닿기·충돌·개수 등 전부). 0이면 예전 방식
    cwd = os.getcwd(); os.chdir(ENGINE)
    try: L = P.Level(objs)
    finally: os.chdir(cwd)
    if P.STATE_TRIG:
        log("트리거: %d개 계산, 움직이거나 켜고 꺼지는 그룹 %d개" % (len(L.trigs), len(L.dyn_groups)))
        if L.todo:
            res.warnings.append("아직 계산 못 하는 트리거가 있음 (결과가 게임과 다를 수 있음): " + ", ".join("%s %d개" % kv for kv in sorted(L.todo.items())))
        if L.guess:
            res.warnings.append("추정으로 계산한 트리거가 있음 (게임과 조금 다를 수 있음): " + ", ".join("%s %d개" % kv for kv in sorted(L.guess.items())))
    start = P.Player(0.0, 15.0, speed=hs["speed"], mini=hs["mini"], upside=hs["flip"], mode=hs["mode"])
    start.hit_ground(); start.bounds_around(15.0)
    if hs["dual"]: res.warnings.append("듀얼로 시작하는 레벨은 아직 확인 안 됨")
    tmp = tempfile.mkdtemp(prefix="fwc_")
    export_level_txt(P, L, start, tmp + "/level.txt")
    open(tmp + "/probe_rules.txt", "w", encoding="utf-8").write("XEND 1e18\nSTARTMODE %d\nENDMODES %s\nENVMODES \nBAN \nCOSTS 24 24 14.0 1.0\n"
                                             % (MODES.index(start.mode), " ".join(str(i) for i in range(8))))
    if offset is None:
        best = None
        for off in (0, -1, 1, -2, 2):
            inp = macro.held_by_tick(offset=off, merge=not two_p)
            open(tmp + "/probe_in.txt", "w", encoding="utf-8").write(" ".join("1" if h else "0" for h in inp))
            _run_tool([tmp + "/level.txt", tmp + "/probe_rules.txt", tmp + "/probe_in.txt", tmp + "/probe.txt"], {"PROBE": "1"})
            a = open(tmp + "/probe.txt", encoding="utf-8", errors="replace").read().split()
            alive, dead = int(a[1]), a[2] == "1"
            log("  틱 맞춤 %+d: %s %.2f초" % (off, "죽음" if dead else "끝까지", alive / 240.0))
            score = (not dead, alive)
            if best is None or score > best[0]: best = (score, off)
        offset = best[1]
        log("틱 맞춤 자동 선택: %+d" % offset)
    res.offset = offset
    inputs = macro.held_by_tick(offset=offset, merge=not two_p)
    if two_p and any(p2 for _, _, p2, _ in macro.inputs):
        res.warnings.append("2인용 레벨: 플레이어 2 입력은 빼고 플레이어 1만 계산함")
    # 3) 그대로 재생 (C++ 계산기로: 파이썬보다 훨씬 빠름. 엔진은 같음 — 파이프라인에서 틱마다 같은 결과 확인)
    open(tmp + "/play_in.txt", "w", encoding="utf-8").write(" ".join("1" if h else "0" for h in inputs))
    _run_tool([tmp + "/level.txt", tmp + "/probe_rules.txt", tmp + "/play_in.txt", tmp + "/play.txt"], {"PROBE": "1", "TRACK": "1", "TRACK2": os.environ.get("FWC_TRACK2", "0"), "TRACKG": os.environ.get("FWC_TRACKG", "0")})
    track = []; lxs = []; died = False; cause = ""
    for l in open(tmp + "/play.txt", encoding="utf-8", errors="replace"):
        a_ = l.split()
        if not a_: continue
        if a_[0] == "T":
            track.append((float(a_[1]), float(a_[2]), MODES[int(a_[3])]))
            lxs.append(float(a_[4]) if len(a_) > 4 else float(a_[1]))  # 진행 거리 (방향 반대·게임 회전이면 세계 x와 다름)
        elif a_[0] == "G": res.__dict__.setdefault("trackg", []).append((float(a_[2]), float(a_[3])))   # 시험용 (FWC_TRACKG=그룹번호): 틱마다 그 그룹 위치
        elif a_[0] == "D": res.__dict__.setdefault("track2", {})[int(a_[1])] = [float(v) for v in a_[2:]]   # 시험용 (FWC_TRACK2=1)
        elif a_[0] == "ALIVE": died = a_[2] == "1"
        elif a_[0] == "CAUSE": cause = l[6:].strip()
    res.died = died; res.death_cause = cause
    res.track_full = list(track)                               # 자르기 전 전체 재생 기록 (검증용: 죽는 순간까지)
    n = len(track)
    if died:
        res.death_tick = n - 1; res.death_x = track[-1][0]
        n = max(0, n - 241)                                    # 죽기 1초 전까지만 윈도우 계산 (그 뒤는 엔진이 게임과 다름)
        res.warnings.append("엔진에서 매크로가 %.2f초(x %.0f)에 죽음: %s → 그 1초 전까지만 계산" % (res.death_tick / 240.0, res.death_x, cause))
    if n < 2: raise ValueError("계산할 구간이 너무 짧아요 (매크로가 처음부터 엔진과 안 맞음)")
    inputs = inputs[:n]; track = track[:n]
    x_end = lxs[n - 1]
    part = R.Part(L, "매크로", start, x_end, objs)
    # 모드 표시: 레벨 포탈 대신 매크로 경로에서 모드가 실제로 바뀐 자리 (사용자 경로는 포탈을 건너뛸 수도 있음)
    marks = []; prev = start.mode
    for x_, y_, m_ in track:
        if m_ != prev: marks.append((x_, m_)); prev = m_
    part.mode_marks = marks
    log("재생: %d틱 (%.1f초), 끝 x %.0f" % (n, n / 240.0, x_end))
    # 4) 윈도우
    export_rules_txt(R, part, tmp + "/rules.txt")
    open(tmp + "/in.txt", "w", encoding="utf-8").write(" ".join("1" if h else "0" for h in inputs))
    log("윈도우 계산 중… (CPU %d개)" % (os.cpu_count() or 1))
    _run_tool([tmp + "/level.txt", tmp + "/rules.txt", tmp + "/in.txt", tmp + "/w.txt", "720", "150"], {"MEASURE_ONLY": "1", "PRESS_ONLY": "1"})
    Lw = open(tmp + "/w.txt", encoding="utf-8", errors="replace").read().split("\n")
    wins = []
    for l in Lw[1:]:
        a = l.split()
        if not a or a[0] != "W" or a[2] != "press": continue      # 떼기는 안 잼 (PRESS_ONLY)
        wins.append({"tick": int(a[1]), "kind": a[2], "early": int(a[3]), "late": int(a[4]), "window": int(a[5]), "mode": a[6], "pair": a[7] == "pair"})
    presses = [w for w in wins if w["kind"] == "press"]
    res.windows = wins
    res.counts = {nm: 0 for nm, _, _ in BINS}
    for w in presses: res.counts[bin_name(w["window"])] += 1
    # 5) σ/s (누름마다 그때까지)
    run = []
    for k in range(1, len(presses) + 1):
        run.append(precision([w["window"] for w in presses[:k]], [w["tick"] / 240.0 for w in presses[:k]]))
    res.precision_run = run
    res.precision = run[-1] if run else 0.0
    res.track = track
    res.n = n; res.level_end = _level_end(objs)
    res.seconds = time.time() - t0
    log("끝: 누름 %d개, σ/s %.2f (%.1f초 걸림)" % (len(presses), res.precision, res.seconds))
    return res


def _level_end(objs):
    e = [float(o["2"]) for o in objs if o["1"] == "3600"]
    return e[0] if e else max(float(o["2"]) for o in objs)


def save_json(res, path):
    json.dump({"windows": res.windows, "counts": res.counts, "precision": res.precision, "precision_run": res.precision_run,
               "warnings": res.warnings, "ticks": res.n, "level_end": res.level_end,
               "track": [(round(x, 2), round(y, 2), m) for x, y, m in res.track]}, open(path, "w", encoding="utf-8"), ensure_ascii=False)
