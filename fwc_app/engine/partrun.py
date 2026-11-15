"""partrun.py — 파트(스타트포스 ~ 다음 스타트포스) 단위 경로 찾기 + 프레임 윈도우 (gdplay 물리)
경로 찾기: 틱마다 누름/뗌 두 갈래, 같은 상태는 합침.
  비용 = 새로 누를 때마다 24 + (큐브 모드에서) 누르고 있는 틱마다 1  → 쇼케이스처럼 짧고 적은 입력을 고름
  상태가 너무 많으면 (모드, y 1유닛, 세로속도 0.5, 누름, 중력, 크기, 속도) 칸마다 비용이 가장 낮은 것만 남기고,
  그중 포탈·오브를 더 많이 쓴 상태부터 cap개만 남김
구간을 크게 벗어나면(그 자리 근처 가장 낮은 오브젝트보다 150 아래) 실패로 봄. 끝에서는 정해진 모드여야 함.
윈도우: 누름은 바로 다음 뗌과 같이, 뗌은 바로 다음 누름과 같이 옮겨서 살아남는 범위 (최대 ±20틱)
"""
import math
import gdplay as P

PRESS_COST = 24
MAXS = 20
ALIVE_HORIZON = 0   # 0이면 끝까지 확인 (파트 1~3 방식)

class Part:
    def __init__(self, L, name, start, x_end, objs, forced=None, end_modes=None):
        self.L, self.name, self.start, self.x_end = L, name, start, x_end
        self.forced = forced or {}          # {틱: True/False} 정해진 입력 (사용자 확인 등)
        self.end_modes = end_modes          # 끝날 때 이 모드여야 함 (포탈을 놓친 경로 거르기)
        # 그 자리 근처(±300) 가장 낮은 오브젝트보다 150 아래로 떨어지면 구간을 벗어난 것으로 봄
        low = {}
        for o in objs:
            if o['1'] in ('31', '747'): continue
            c = int(float(o['2']) // 60)
            y = float(o['3'])
            low[c] = min(low.get(c, 1e9), y)
        self.floor_c = {}
        cs = sorted(low)
        for c in range(cs[0] - 5, cs[-1] + 6):
            m = min((low[k] for k in range(c - 5, c + 6) if k in low), default=None)
            if m is not None: self.floor_c[c] = m - 150
        # 순수 모드용: 그 자리 근처(±300) 가장 높은 오브젝트보다 150 위로 올라가면 구간을 벗어난 것 (floor_c와 같은 방식)
        high = {}
        for o in objs:
            if o['1'] in ('31', '747'): continue
            c = int(float(o['2']) // 60); high[c] = max(high.get(c, -1e9), float(o['3']))
        self.ceil_c = {}
        for c in range(cs[0] - 5, cs[-1] + 6):
            m = max((high[k] for k in range(c - 5, c + 6) if k in high), default=None)
            if m is not None: self.ceil_c[c] = m + 150
        self.use_ceil = False; self.any_mode = False

        # 모드 포탈을 지났는데 모드가 안 바뀐 상태(포탈을 놓친 경로)는 버림
        self.mode_marks = sorted((float(o['2']), P.MODE_OF[int(o['1'])]) for o in objs
                                 if o['1'].isdigit() and int(o['1']) in P.MODE_OF and float(o['2']) > start.x + 1)

    def allowed_modes(self, x):
        """그 x에서 될 수 있는 모드. 포탈 근처(x-60 ~ x+45)에서는 바뀌기 전·후 둘 다 허용 (회전한 포탈은 일찍 닿음)"""
        m = self.start.mode; ok = {m}
        for px, md in self.mode_marks:
            if x > px + 45:
                m = md; ok = {m}
            elif x > px - 60:
                ok = ok | {md}
            else:
                break
        return ok

    def y_floor_at(self, x):
        return self.floor_c.get(int(x // 60), -1e9)

    def out(self, p):
        if self.use_ceil and p.y > self.ceil_c.get(int(p.x // 60), 1e9): return True
        if self.any_mode: return p.y < self.y_floor_at(p.x)
        ok = self.allowed_modes(p.x)   # 듀얼: 둘 중 하나가 그 모드면 됨 (C++ 탐색과 같게)
        return p.y < self.y_floor_at(p.x) or (p.mode not in ok and not (p.dual and p.dual.mode in ok))

    def done(self, p):
        return p.x >= self.x_end and (self.end_modes is None or p.mode in self.end_modes)

def bucket(p):
    tw = p.dual   # 듀얼: 둘째 상태도 구분 (안 하면 첫째가 같은 상태끼리 합쳐질 때 둘째가 살 길이 지워짐)
    return (p.mode, int(p.y // 1.0), int(p.y_vel * 2), p.holding_jump, p.jump_unused, p.upside_down, p.mini,
            p.speed, p.on_ground, p.is_dashing, len(p.used),
            (tw.mode, int(tw.y // 2.0), int(tw.y_vel * 2), tw.upside_down, tw.on_ground, tw.holding_jump, len(tw.used)) if tw else None)

def search(part, max_ticks=40000, cap=2500, log=None):
    """틱마다 누름/뗌 두 갈래로 펼치고 같은 상태는 합침. 메모리 절약: 지난 틱 기록은 상태 키 대신 번호로만 저장"""
    layer = {part.start.key(): (0, part.start, 0)}       # 키 → (비용, 상태, 이번 틱 번호)
    hist = []                                            # hist[t][번호] = (이전 틱 번호, 누름)
    def trace(tt_end, i):
        out = []
        for tt in range(tt_end, -1, -1):
            pi, h = hist[tt][i]; out.append(h); i = pi
        out.reverse(); return out
    for t in range(max_ticks):
        nxt = {}
        choices = (part.forced[t],) if t in part.forced else (False, True)
        for k, (sw, p, pid) in layer.items():
            for hold in choices:
                q = p.copy()
                P.step(q, part.L, hold)
                if q.dead or part.out(q) or (q.x >= part.x_end and not part.done(q)): continue
                pc = part.press_cost(q) if hasattr(part, "press_cost") else PRESS_COST
                s2 = sw + (pc if (hold and not p.holding_jump) else 0) + (1 if (hold and q.mode == "cube") else 0)
                tc = getattr(part, "tick_cost", None)
                if tc: s2 += tc(q)
                kk = q.key()
                if kk not in nxt or s2 < nxt[kk][0]:
                    nxt[kk] = (s2, q, (pid, hold))
        if not nxt:
            far = max(layer.values(), key=lambda v: v[1].x)
            partial = trace(len(hist) - 1, far[2]) if hist else []
            err = RuntimeError("틱 %d에서 살아남는 경로 없음 (가장 멀리 x %.1f y %.1f, %s)" % (t, far[1].x, far[1].y, far[1].mode))
            err.partial = partial
            raise err
        if len(nxt) > cap:
            best = {}
            for kk, v in nxt.items():
                b = bucket(v[1])
                if b not in best or v[0] < nxt[best[b]][0]: best[b] = kk
            # 남길 순서: 포탈·오브를 더 많이 쓴 상태 먼저 (레벨이 의도한 길을 따라가는 상태), 그다음 비용
            # 절반은 포탈·오브를 더 많이 쓴 상태 먼저, 나머지 절반은 비용(누름 수 등)이 낮은 상태 먼저
            # (오브를 '안 누르고' 지나가야 하는 길이 지워지지 않게 — 쇼케이스 파트 4 x 27406 빨강 오브)
            k1 = sorted(best.values(), key=lambda kk: (-len(nxt[kk][1].used), nxt[kk][0]))[:cap // 2]
            s1 = set(k1)
            k2 = [kk for kk in sorted(best.values(), key=lambda kk: nxt[kk][0]) if kk not in s1][:cap - len(k1)]
            keys = k1 + k2
            nxt = {kk: nxt[kk] for kk in keys}
        back = []; layer = {}
        for i, (kk, (s2, q, link)) in enumerate(nxt.items()):
            back.append(link); layer[kk] = (s2, q, i)
        hist.append(back)
        if log and (t % 500 == 0 or (len(nxt) >= cap and t % 50 == 0)):
            far = max(layer.values(), key=lambda v: v[1].x)[1]
            log("  틱 %d: 상태 %d개, 가장 앞 x %.0f (%s)" % (t, len(layer), far.x, far.mode))
        done = [(v[0], v[2]) for v in layer.values() if part.done(v[1])]
        if done:
            _, i = min(done)
            return trace(len(hist) - 1, i)
    raise RuntimeError("시간 초과")

def simulate(part, inputs):
    p = part.start.copy(); states = []
    for h in inputs:
        P.step(p, part.L, h)
        states.append(p.copy())
        if p.dead: return False, states
    return part.done(p), states

def events_of(inputs):
    ev = []; prev = False
    for t, h in enumerate(inputs):
        if h != prev: ev.append(t)
        prev = h
    return ev

def inputs_of(ev, n):
    out, s, j = [], False, 0; ev = sorted(ev)
    for t in range(n):
        while j < len(ev) and ev[j] == t: s = not s; j += 1
        out.append(s)
    return out

def alive_from(part, ref, inputs, start_t, end_t):
    p = part.start.copy() if start_t == 0 else ref[start_t - 1].copy()
    for t in range(start_t, len(inputs)):
        P.step(p, part.L, inputs[t])
        if p.dead or part.out(p): return False
        if t > end_t and t < len(ref) and p.key() == ref[t].key(): return True
        if part.done(p): return True
        # 계산 시간 줄이기: 다시 같아지지 않아도 바꾼 곳 뒤 ALIVE_HORIZON틱(3초) 동안 살아 있으면 성공으로 봄 (추정 — 파트 4부터)
        if ALIVE_HORIZON and t > end_t + ALIVE_HORIZON: return True
    while not part.done(p) and p.tick < len(inputs) + 600:
        P.step(p, part.L, inputs[-1] if inputs else False)
        if p.dead: return False
    return part.done(p)

def windows(part, ev, n):
    inputs = inputs_of(ev, n)
    ok, ref = simulate(part, inputs)
    assert ok, "기준 경로가 죽음"
    res = []
    for i, t in enumerate(ev):
        press = inputs[t]
        nxt = ev[i + 1] if i + 1 < len(ev) else None
        moves = [t] + ([nxt] if nxt is not None else [])
        prev_t = ev[i - 1] if i > 0 else -1
        after = ev[i + len(moves)] if i + len(moves) < len(ev) else n
        def ok_(d):
            a = [m + d for m in moves]
            if a[0] <= prev_t or a[-1] >= after or min(a) < 0: return False
            if any(part.forced.get(x) is not None for x in a + moves): return False
            inp = inputs_of([e for e in ev if e not in moves] + a, n)
            return alive_from(part, ref, inp, max(0, min(a + moves) - 1), max(a + moves) + 1)
        e = 0
        while e < MAXS and ok_(-(e + 1)): e += 1
        l = 0
        while l < MAXS and ok_(l + 1): l += 1
        res.append({"tick": t, "kind": "press" if press else "release", "early": e, "late": l, "window": e + l + 1,
                    "mode": ref[t].mode})
    return res

def solve(part, log=None):
    inputs = search(part, log=log)
    n = len(inputs); ev = events_of(inputs)
    for _ in range(2):
        res = windows(part, ev, n)
        for k, r in enumerate(res):
            sh = (r["late"] - r["early"]) // 2
            if sh == 0: continue
            moves = [ev[k]] + ([ev[k + 1]] if k + 1 < len(ev) else [])
            ev2 = sorted(set([e for e in ev if e not in moves] + [m + sh for m in moves]))
            if len(ev2) == len(ev) and simulate(part, inputs_of(ev2, n))[0]:
                ev = ev2
    ok, states = simulate(part, inputs_of(ev, n)); assert ok
    res = windows(part, ev, n)
    return ev, res, states


# ---------------------------------------------------------------- 윈도우 방식 2 (사용자 지적 반영)
# 순간 입력 모드(큐브·UFO·볼·스파이더): 누르는 순간이 중요하고 누르고 있는 길이는 거의 상관없음.
#   → 누름은 '혼자' 옮겨서 잼 (뗌은 그대로). 공중에서 미리 눌러 두는 버퍼 클릭도 그대로 살아남으면 윈도우에 들어감.
#   → 뗌도 '혼자' 옮겨서 잼.
#   → 계산 경로의 짧은 톡(1틱) 누름은, 경로가 안 바뀌는 범위에서 뗌을 최대한 늦춤 (사람처럼 누르고 있기).
# 계속 조종 모드(쉽·웨이브·스윙·로봇): 누름·뗌 길이가 경로를 바꾸므로 기존 방식(다음 입력과 같이 옮김) 유지.
IMPULSE = {"cube", "ufo", "ball", "spider"}

def phys_key(p):
    tw = p.dual   # 듀얼: 둘째도 같아야 '같은 움직임' (안 보면 누름을 늘렸을 때 둘째만 달라져 나중에 죽음)
    return (p.x, p.y, p.y_vel, p.mode, p.upside_down, p.mini, p.speed, p.used, p.is_dashing, p.on_ground,
            (tw.y, tw.y_vel, tw.mode, tw.upside_down, tw.mini, tw.used, tw.is_dashing, tw.on_ground) if tw else None)

def extend_holds(part, ev, n):
    inputs = inputs_of(ev, n)
    ok, ref = simulate(part, inputs); assert ok
    ev = list(ev)
    for i in range(0, len(ev) - 1):
        if not inputs_of(ev, n)[ev[i]]: continue                # 누름만
        p_t, r_t = ev[i], ev[i + 1]
        if ref[p_t].mode not in IMPULSE: continue
        nxt_p = ev[i + 2] if i + 2 < len(ev) else n
        best = r_t
        for r2 in range(nxt_p - 1, r_t, -1):
            if part.forced.get(r2) is not None: continue
            ev2 = ev[:i + 1] + [r2] + ev[i + 2:]
            inp2 = inputs_of(ev2, n)
            q = ref[p_t - 1].copy() if p_t > 0 else part.start.copy()
            same = True
            for t in range(p_t, min(nxt_p, n)):
                P.step(q, part.L, inp2[t])
                if q.dead or phys_key(q) != phys_key(ref[t]): same = False; break
            if same:
                best = r2; break
        ev[i + 1] = best
    return ev

def windows2(part, ev, n):
    inputs = inputs_of(ev, n)
    ok, ref = simulate(part, inputs)
    assert ok, "기준 경로가 죽음"
    res = []
    for i, t in enumerate(ev):
        press = inputs[t]
        mode = ref[t].mode
        alone = mode in IMPULSE
        nxt = ev[i + 1] if i + 1 < len(ev) else None
        moves = [t] if (alone or nxt is None) else [t, nxt]
        prev_t = ev[i - 1] if i > 0 else -1
        after = ev[i + len(moves)] if i + len(moves) < len(ev) else n
        def ok_(d):
            a = [m + d for m in moves]
            if a[0] <= prev_t or a[-1] >= after or min(a) < 0: return False
            if any(part.forced.get(x) is not None for x in a): return False
            inp = inputs_of([e for e in ev if e not in moves] + a, n)
            return alive_from(part, ref, inp, max(0, min(a + moves) - 1), max(a + moves) + 1)
        e = 0
        while e < MAXS and ok_(-(e + 1)): e += 1
        l = 0
        while l < MAXS and ok_(l + 1): l += 1
        res.append({"tick": t, "kind": "press" if press else "release", "early": e, "late": l, "window": e + l + 1,
                    "mode": mode, "method": "혼자" if alone else "다음 입력과 같이"})
    return res

def finish2(part, ev, n):
    """기존 경로 → 누르고 있기 늘림 → 윈도우 가운데로 2번 → 최종 윈도우"""
    ev = extend_holds(part, ev, n)
    for _ in range(2):
        res = windows2(part, ev, n)
        for k, r in enumerate(res):
            sh = (r["late"] - r["early"]) // 2
            if sh == 0: continue
            moves = [ev[k]] if r["method"] == "혼자" or k + 1 >= len(ev) else [ev[k], ev[k + 1]]
            ev2 = sorted(set([e for e in ev if e not in moves] + [m + sh for m in moves]))
            if len(ev2) == len(ev) and simulate(part, inputs_of(ev2, n))[0]:
                ev = ev2
        ev = extend_holds(part, ev, n)
    ok, states = simulate(part, inputs_of(ev, n)); assert ok
    return ev, windows2(part, ev, n), states


# ---------------------------------------------------------------- 윈도우 방식 3: 버퍼 클릭 반영
# 누름을 앞으로 당길 때 바로 앞 뗌이 막으면, 그 뗌도 같이 앞으로 당김 (새 누름 1틱 전까지).
#   = 공중에서 미리 눌러서 착지할 때까지 누르고 있는 버퍼 클릭.
# 뗌을 뒤로 미룰 때 다음 누름이 막으면, 그 누름도 같이 뒤로 (계속 누르고 있는 것과 같음).
# 쉽·로봇 등 계속 조종 모드는 '혼자'와 '다음 입력과 같이' 둘 다 재서 큰 쪽 (사람이 어느 쪽으로 틀려도 되는지).
def _shift_ok(part, ref, ev, n, idx_moves, d):
    ev2 = list(ev)
    for k in idx_moves: ev2[k] += d
    lo = min(idx_moves); hi = max(idx_moves)
    # 앞쪽으로 밀어서 겹치면 이전 이벤트들을 같이 밀기 (버퍼)
    k = lo - 1
    while k >= 0 and ev2[k] >= ev2[k + 1]:
        ev2[k] = ev2[k + 1] - 1; k -= 1
    k = hi + 1
    while k < len(ev2) and ev2[k] <= ev2[k - 1]:
        ev2[k] = ev2[k - 1] + 1; k += 1
    if ev2[0] < 0 or ev2[-1] >= n: return False
    # 같이 밀린 이벤트는 '같은 종류'의 쌍이어야 의미가 있음: 한 쌍(뗌+누름)이 통째로 사라지는 경우만 허용 안 함
    if any(part.forced.get(x) is not None for x in ev2 if x not in ev): return False
    changed = [ev[k] for k in range(len(ev)) if ev2[k] != ev[k]] + [ev2[k] for k in range(len(ev)) if ev2[k] != ev[k]]
    inp = inputs_of(ev2, n)
    return alive_from(part, ref, inp, max(0, min(changed) - 1), max(changed) + 1)

def windows3(part, ev, n):
    inputs = inputs_of(ev, n)
    ok, ref = simulate(part, inputs); assert ok
    res = []
    for i, t in enumerate(ev):
        mode = ref[t].mode
        methods = [("혼자", [i])]
        if mode not in IMPULSE and i + 1 < len(ev): methods.append(("다음 입력과 같이", [i, i + 1]))
        best = None
        for name, idx in methods:
            e = 0
            while e < MAXS and _shift_ok(part, ref, ev, n, idx, -(e + 1)): e += 1
            l = 0
            while l < MAXS and _shift_ok(part, ref, ev, n, idx, l + 1): l += 1
            if best is None or e + l > best[1] + best[2]: best = (name, e, l)
        res.append({"tick": t, "kind": "press" if inputs[t] else "release", "early": best[1], "late": best[2],
                    "window": best[1] + best[2] + 1, "mode": mode, "method": best[0]})
    return res

def finish3(part, ev, n):
    ev = extend_holds(part, ev, n)
    for _ in range(2):
        res = windows3(part, ev, n)
        for k, r in enumerate(res):
            sh = (r["late"] - r["early"]) // 2
            if sh == 0: continue
            idx = [k] if r["method"] == "혼자" else [k, k + 1]
            ev2 = list(ev)
            for j in idx: ev2[j] += sh
            if ev2 == sorted(set(ev2)) and simulate(part, inputs_of(ev2, n))[0]: ev = ev2
        ev = extend_holds(part, ev, n)
    ok, states = simulate(part, inputs_of(ev, n)); assert ok
    return ev, windows3(part, ev, n), states


# ---------------------------------------------------------------- 윈도우 방식 4: 옮긴 뒤 '다시 따라잡기' 허용
# 사람은 입력 하나가 1틱 어긋나도 그 뒤 입력을 조금씩 맞춰서 다시 원래 길로 돌아옴.
# 그래서: 옮긴 입력까지는 그대로 진행 → 그 뒤는 자유롭게 (누름/뗌 아무거나) 진행해서
#   REJOIN 틱 안에 원래 경로와 같은 모드, 높이 차이 15 이하(반 칸)로 돌아오면 '살아남음'.
REJOIN_MIN, REJOIN_MAX = 120, 360
import os as _os
REJOIN_CAP = int(_os.environ.get("REJOIN_CAP", "1500"))   # 따라잡기 탐색에서 남기는 상태 수 (작을수록 빠름)

NEAR_REJOIN = _os.environ.get("NEAR_REJOIN", "1") == "1"
def _near(q, r, tol=1.0):
    """거의 같은 상태면 '다시 따라잡음'으로 봄 (듀얼은 둘째까지 x·y가 정확히 같아지기 어려워서) — 추정"""
    def one(a, b):
        return (a.mode == b.mode and a.upside_down == b.upside_down and a.on_ground == b.on_ground and a.mini == b.mini
                and abs(a.y - b.y) <= tol and abs(a.y_vel - b.y_vel) <= 1.0 and len(a.used) == len(b.used))
    if not one(q, r): return False
    if bool(q.dual) != bool(r.dual): return False
    return one(q.dual, r.dual) if q.dual else True

def _recover(part, ref, inputs, start_t, end_t, ev_after):
    p = part.start.copy() if start_t == 0 else ref[start_t - 1].copy()
    for t in range(start_t, end_t + 1):
        P.step(p, part.L, inputs[t])
        if p.dead or part.out(p): return False
        if part.done(p): return True
    # 목표 틱: 그 뒤 입력 2개가 지난 뒤 (120~360틱)
    nx = [e for e in ev_after if e > end_t]
    T = nx[1] + 30 if len(nx) > 1 else end_t + REJOIN_MIN
    T = min(max(T, end_t + REJOIN_MIN), end_t + REJOIN_MAX, len(ref) - 1)
    layer = {bucket(p): p}
    for t in range(end_t + 1, T + 1):
        nxt = {}
        for q0 in layer.values():
            for h in (False, True):
                q = q0.copy(); P.step(q, part.L, h)
                if q.dead or part.out(q): continue
                if part.done(q): return True
                if q.key() == ref[t].key(): return True
                if NEAR_REJOIN and _near(q, ref[t]): return True
                b = bucket(q)
                if b not in nxt: nxt[b] = q
        if not nxt: return False
        if len(nxt) > REJOIN_CAP:
            r = ref[t]
            nxt = dict(sorted(nxt.items(), key=lambda kv: abs(kv[1].y - r.y) + (0 if kv[1].mode == r.mode else 1e6))[:REJOIN_CAP])
        layer = nxt
    r = ref[T]
    return any(q.mode == r.mode and abs(q.y - r.y) <= 15 and q.upside_down == r.upside_down for q in layer.values())

def _shift_ok4(part, ref, ev, n, idx_moves, d):
    ev2 = list(ev)
    for k in idx_moves: ev2[k] += d
    lo = min(idx_moves); hi = max(idx_moves)
    k = lo - 1
    while k >= 0 and ev2[k] >= ev2[k + 1]:
        ev2[k] = ev2[k + 1] - 1; k -= 1
    k = hi + 1
    while k < len(ev2) and ev2[k] <= ev2[k - 1]:
        ev2[k] = ev2[k - 1] + 1; k += 1
    if ev2[0] < 0 or ev2[-1] >= n: return False
    if any(part.forced.get(x) is not None for x in ev2 if x not in ev): return False
    ch = [k for k in range(len(ev)) if ev2[k] != ev[k]]
    a, b = min(ev[k] for k in ch), max(ev[k] for k in ch)
    a, b = min(a, min(ev2[k] for k in ch)), max(b, max(ev2[k] for k in ch))
    inp = inputs_of(ev2, n)
    if alive_from(part, ref, inp, max(0, a - 1), b + 1): return True
    return _recover(part, ref, inp, max(0, a - 1), b, ev2)

def windows4(part, ev, n):
    inputs = inputs_of(ev, n)
    ok, ref = simulate(part, inputs); assert ok
    res = []
    for i, t in enumerate(ev):
        mode = ref[t].mode
        methods = [("혼자", [i])]
        if mode not in IMPULSE and i + 1 < len(ev): methods.append(("다음 입력과 같이", [i, i + 1]))
        best = None
        for name, idx in methods:
            e = 0
            while e < MAXS and _shift_ok4(part, ref, ev, n, idx, -(e + 1)): e += 1
            l = 0
            while l < MAXS and _shift_ok4(part, ref, ev, n, idx, l + 1): l += 1
            if best is None or e + l > best[1] + best[2]: best = (name, e, l)
        res.append({"tick": t, "kind": "press" if inputs[t] else "release", "early": best[1], "late": best[2],
                    "window": best[1] + best[2] + 1, "mode": mode, "method": best[0]})
    return res


# ---------------------------------------------------------------- 밀길(의도하지 않은 길) 막기
# 비행 모드가 화면 바닥·천장(보이지 않는 경계)을 타고 미끄러지는 길은 대부분 밀길.
# (파트 1 웨이브가 다이아몬드 위 천장을 탄 것, 파트 2 웨이브가 천장을 탄 것 모두 이 경우)
# → 웨이브·쉽·스윙이 경계에 닿으면 그 상태는 버림.
# 막기 상자 blocks = [(x0, x1, y0, y1, 모드 또는 None)]: 그 상자 안에 들어가면 버림 (사용자가 직접 막는 기능)
class PartNoBound(Part):
    BAN = {"wave", "ship", "swing"}
    def __init__(self, *a, blocks=None, skip_portals=None, envelope=None, **k):
        super().__init__(*a, **k); self.blocks = blocks or []
        # 안 타는 모드 포탈 (쇼케이스에서 지나치는 장식/함정 포탈): 그 x 근처(±5) 모드 표시를 지움
        for sx in (skip_portals or []):
            self.mode_marks = [(px, md) for px, md in self.mode_marks if abs(px - sx) > 5]
        # 오브젝트 범위 안에 머물기 (envelope = 모드 집합): 그 x ±90 안 오브젝트들의 가장 아래~가장 위 밖으로 나가면 버림
        self.envelope = envelope or set()
        self.env = {}
        if self.envelope:
            seen = set(); col = {}
            for c in self.L.cells.values():
                for o in c:
                    if o.uid in seen or o.kind not in ("solid", "hazard", "slope", "saw", "portal", "mode", "orb", "pad"): continue
                    seen.add(o.uid)
                    for cx in range(int(o.minx // 30), int(o.maxx // 30) + 1):
                        lo, hi = col.get(cx, (1e9, -1e9)); col[cx] = (min(lo, o.miny), max(hi, o.maxy))
            for cx in col:
                lo = min(col[k][0] for k in range(cx - 3, cx + 4) if k in col)
                hi = max(col[k][1] for k in range(cx - 3, cx + 4) if k in col)
                self.env[cx] = (lo, hi)
    def must_touch(self, items):
        """꼭 닿아야 하는 오브·패드·포탈: [(종류, arg, 중심 x)] → 그 물체를 지나 30 넘게 갔는데 안 썼으면 버림"""
        self.musts = []
        seen = set()
        for c in self.L.cells.values():
            for o in c:
                if o.uid in seen: continue
                seen.add(o.uid)
                for kind, arg, cx in items:
                    if o.kind == kind and getattr(o, "arg", None) == arg and abs((o.minx + o.maxx) / 2 - cx) < 20:
                        self.musts.append((o.maxx + 30, o.uid))
        assert len(self.musts) == len(items), "꼭 닿아야 할 물체를 못 찾음"
    NEAR = 14.0          # 가시와 이 거리(유닛)보다 가까우면 비용
    NEAR_W = 1.0         # 1유닛 가까워질 때마다 틱당 비용 (3 → 1: 가시 피하려고 잔지그재그 치던 것 줄임)
    WAVE_PRESS = 120     # 웨이브 누름 1번 비용 (보통 24): 짧게 톡톡 누르는 잔지그재그 대신 길게 꺾는 길을 고름
    def press_cost(self, q):
        return self.WAVE_PRESS if q.mode == "wave" else PRESS_COST
    def tick_cost(self, q):
        """웨이브 전용: 가시·톱니 히트박스와의 거리가 NEAR보다 가까우면 틱마다 비용.
        → 가시 두 개 사이 틈(히트박스는 작지만 실제로는 위험해 보이는 곳)을 억지로 지나는 밀길 대신 통로 가운데를 택함"""
        if q.mode != "wave": return 0
        h = q.half; best = self.NEAR
        self.L.cur_x = q.x
        for o in self.L.query(q.x - h - self.NEAR, q.x + h + self.NEAR, q.y - h - self.NEAR, q.y + h + self.NEAR):
            if o.kind not in ("hazard", "saw"): continue
            if o.kind == "saw":
                d = math.hypot(o.x - q.x, o.y - q.y) - o.radius - h
            else:
                dx = max(o.minx - (q.x + h), (q.x - h) - o.maxx, 0.0)
                dy = max(o.miny - (q.y + h), (q.y - h) - o.maxy, 0.0)
                d = math.hypot(dx, dy)
            if d < best: best = d
        return (self.NEAR - best) * self.NEAR_W if best < self.NEAR else 0
    def must_avoid(self, items):
        """닿으면 안 되는 물체: [(종류, arg, 중심 x)] → 그 물체를 쓴(닿은) 상태는 버림"""
        self.avoids = set()
        seen = set()
        for c in self.L.cells.values():
            for o in c:
                if o.uid in seen: continue
                seen.add(o.uid)
                for kind, arg, cx in items:
                    if o.kind == kind and getattr(o, "arg", None) == arg and abs((o.minx + o.maxx) / 2 - cx) < 20:
                        self.avoids.add(o.uid)
        assert len(self.avoids) >= len(items), "닿으면 안 될 물체를 못 찾음"
    def out(self, q):
        if getattr(self, "avoids", None) and (q.used & self.avoids): return True
        for xm, uid in getattr(self, "musts", []):
            if q.x > xm and uid not in q.used: return True
        if q.mode in self.envelope:
            e = self.env.get(int(q.x // 30))
            if e and not (e[0] - 15 <= q.y <= e[1] + 15): return True
        if q.mode in self.BAN and q.ceiling is not None:
            ext = q.half * 2.0 if q.mode == "wave" else q.half
            if q.y + ext >= q.ceiling - 0.01 or q.y - ext <= q.floor + 0.01: return True
        for x0, x1, y0, y1, m in self.blocks:
            if x0 <= q.x <= x1 and y0 <= q.y <= y1 and (m is None or q.mode == m): return True
        return Part.out(self, q)
