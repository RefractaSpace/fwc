"""gdwave2.py — 레벨 전체에서 웨이브 구간을 자동으로 찾아 프레임 윈도우 계산 (내가 짠 코드)
충돌 규칙·히트박스: GD 2.2 (GD-Remake의 설명과 히트박스 표 데이터를 gd_table.json으로 옮김, 그 코드는 실행 안 함)
  - 웨이브 10x10 (미니 x0.6) / 벽: 안쪽 히트박스 x0.3 / 가시·톱니: 바깥 / 경사: 중심 기준 / 가시 경사: 면 +4
  - 바닥·천장: 웨이브 포탈 y로 다시 맞춤 = max(0, 30*floor((y-150)/30)) ~ +300, 닿으면 미끄러짐
  - 포탈: 플레이어 바깥 히트박스가 포탈 왼쪽 끝에 닿는 x에서 작동한다고 봄 (y는 나중에 '실제로 닿았는지' 확인만)
  - 텔레포트 포탈(747): 출구 y = 포탈 y + 키 54 로 이동 (추정, GD-Remake와 같음), 바닥·천장 다시 맞춤
  - 오브·패드·트리거(이동·텔레포트 트리거)는 계산 안 함 → 들어 있는 구간은 표시만
구간 = 웨이브 포탈부터 다음 '웨이브가 아닌' 모드 포탈(또는 듀얼 포탈)까지. 웨이브 포탈을 다시 지나면 바닥·천장만 다시 맞춤.
"""
import json, math, sys
from collections import defaultdict
from wave_sim import transform, sat_box_poly, box_circle

T_TABLE = {int(k): v for k, v in json.load(open('gd_table.json')).items()}
MODE = {12: 'cube', 13: 'ship', 47: 'ball', 111: 'ufo', 660: 'wave', 745: 'robot', 1331: 'spider', 1933: 'swing'}
SPEEDS = {200: 0.7 * 5.980002 * 60 / 240, 201: 0.9 * 5.770002 * 60 / 240, 202: 1.1 * 5.870002 * 60 / 240,
          203: 1.3 * 6.000002 * 60 / 240, 1334: 1.6 * 6.000002 * 60 / 240}
SPEED_NAME = {200: '0.5x', 201: '1x', 202: '2x', 203: '3x', 1334: '4x'}
SIZE = {99: 1, 101: 0}
GRAV = {10: False, 11: True}
# 포탈 히트박스 (폭, 높이, dx)  — 표 데이터: 모드 34x86, 중력 25x75, 크기 31x90, 속도 51x56(추정 아님, 표), 텔레포트 25x90 dx 12
PORTAL_BOX = {'mode': (34, 86, 0), 'grav': (25, 75, 0), 'size': (31, 90, 0), 'speed': (51, 56, 0), 'tp': (25, 90, 12), 'dual': (34, 86, 0)}
ORBS_PADS = {36, 84, 141, 1022, 1330, 1333, 1704, 1751, 1594, 3004, 3005, 35, 67, 140, 1332, 3027}
WAVE_HALF = {1: 5.0, 0: 3.0}
INNER = 0.3
HAZ_SLOPE_OFF = 4.0

def num(o, k, d=0.0):
    try: return float(o.get(k, d))
    except ValueError: return d

def load_level(path):
    objs = []
    for p in open(path, encoding='utf-8').read().split(';')[1:]:
        f = p.split(','); o = dict(zip(f[0::2], f[1::2]))
        if '1' in o and '2' in o and '3' in o:
            try: int(o['1']); float(o['2']); float(o['3'])
            except ValueError: continue
            objs.append(o)
    return objs

def rects_touch(a0, a1, a2, a3, b0, b1, b2, b3):
    return not (a1 < b0 or b1 < a0 or a3 < b2 or b3 < a2)

class World:
    def __init__(self, objs):
        self.grid = defaultdict(list); self.cache = {}; self.draw = []; self.last_x = -1e9
        for o in objs:
            i = int(o['1']); d = T_TABLE.get(i)
            if not d: continue
            kind, w, h, ox, oy = d
            x, y = num(o, '2'), num(o, '3')
            sx = num(o, '128', num(o, '32', 1)); sy = num(o, '129', num(o, '32', 1))
            if kind.startswith('slope') or kind.startswith('slope_hazard'):
                w, h = w * sx, h * sy
                rot = num(o, '6'); turns = int(round(rot / 90.0)) % 4
                if abs(rot / 90.0 - round(rot / 90.0)) > 1e-6: continue          # 비스듬한 경사: GD-Remake도 무시
                if turns % 2: w, h = h, w
                fx, fy = o.get('4') == '1', o.get('5') == '1'
                orient = (turns + (2 if (fx and fy) else 1 if fx else 3 if fy else 0)) % 4
                s = ('slope', (x - w/2, x + w/2, y - h/2, y + h/2, orient, 'hazard' in kind)); x0, x1 = x - w/2, x + w/2
                bw = 30 if kind.endswith('45') else 60
                self.draw.append(('spike' if 'hazard' in kind else 'wall', transform([(-bw/2, -15), (bw/2, -15), (bw/2, 15)], o)))
            elif kind in ('solid', 'hazard'):
                pts = [(ox - w/2, oy - h/2), (ox + w/2, oy - h/2), (ox + w/2, oy + h/2), (ox - w/2, oy + h/2)]
                poly = transform(pts, o)
                xs = [p[0] for p in poly]; x0, x1 = min(xs), max(xs)
                s = (kind, poly); self.draw.append(('wall' if kind == 'solid' else 'spike', poly))
            elif kind == 'saw':
                r = w * max(sx, sy); s = ('saw', (x, y, r)); x0, x1 = x - r, x + r
                self.draw.append(('saw', (x, y, r)))
            else:
                continue
            for g in range(int(x0 // 60) - 1, int(x1 // 60) + 2):
                self.grid[g].append((s, x0, x1))

    def hit(self, x, y, mode, mg=0.0):
        key = (round(x, 4), y, mode, mg)
        c = self.cache.get(key)
        if c is None:
            c = self.cache[key] = self._hit(x, y, mode, mg)
        return c

    def _hit(self, x, y, mode, mg):
        hh = WAVE_HALF[mode] + mg; hi = WAVE_HALF[mode] * INNER + mg
        for (kind, g), x0, x1 in self.grid[int(x // 60)]:
            if x1 < x - hh or x0 > x + hh: continue
            if kind == 'solid':
                if sat_box_poly(x, y, hi, g): return True
            elif kind == 'hazard':
                if sat_box_poly(x, y, hh, g): return True
            elif kind == 'saw':
                if box_circle(x, y, hh, g): return True
            elif self._slope(x, y, hh, hi, g):
                return True
        return False

    @staticmethod
    def _slope(x, y, hh, hi, g):
        minx, maxx, miny, maxy, orient, hazard = g
        ceiling = orient >= 2
        if orient in (1, 2) and x < minx and rects_touch(x-hi, x+hi, y-hi, y+hi, minx, minx+1, miny, maxy): return True
        if ceiling:
            if y >= maxy and rects_touch(x-hi, x+hi, y-hi, y+hi, minx, maxx, maxy-1, maxy): return True
        elif y <= miny + 1 and rects_touch(x-hi, x+hi, y-hi, y+hi, minx, maxx, miny, miny+1): return True
        if not rects_touch(x-hh, x+hh, y-hh, y+hh, minx, maxx, miny, maxy): return False
        ratio = (maxy - miny) / (maxx - minx); rad = hh / math.cos(math.atan(ratio))
        surf = miny + (x - minx) * ratio if orient in (0, 2) else maxy - (x - minx) * ratio
        if ceiling:
            if hazard: surf -= HAZ_SLOPE_OFF
            return y > min(max(surf - rad, miny - hh), maxy)
        if hazard: surf += HAZ_SLOPE_OFF
        return y < max(min(surf + rad, maxy + hh), miny)

def bounds_for(py):
    f = max(0.0, 30 * math.floor((py - 150) / 30)); return f, f + 300

class Run:
    """웨이브 구간 하나: 틱마다 x, 속도, 크기, 중력, 바닥·천장, 텔레포트를 미리 계산"""
    def __init__(self, objs, start, world, x_cap=None):
        self.W = world
        P = sorted([o for o in objs if int(o['1']) in MODE or int(o['1']) in SPEEDS or int(o['1']) in SIZE
                    or int(o['1']) in GRAV or o['1'] in ('286', '287', '747')], key=lambda o: num(o, '2'))
        sx = num(start, '2')
        before = [o for o in P if num(o, '2') < sx]
        def last(ids, d):
            for o in reversed(before):
                if int(o['1']) in ids: return int(o['1'])
            return d
        sp = last(SPEEDS, 201); sz = SIZE.get(last(SIZE, 99), 1); gv = GRAV.get(last(GRAV, 10), False)
        self.start_state = (SPEED_NAME[sp], 'mini' if sz == 0 else '일반', '반전' if gv else '보통')
        self.x0 = sx; self.y0 = num(start, '3'); self.lo, self.hi = bounds_for(self.y0)
        after = [o for o in P if num(o, '2') >= sx and o is not start]
        end = next((o for o in after if (int(o['1']) in MODE and o['1'] != '660') or o['1'] in ('286', '287')), None)
        self.x1 = num(end, '2') if end else max(o for o in [num(oo, '2') for oo in objs])
        self.end_kind = (MODE.get(int(end['1'])) or 'dual') if end else '끝'
        if x_cap: self.x1 = min(self.x1, x_cap)
        evs = [o for o in after if num(o, '2') < self.x1]
        # 틱 시간표
        self.xs, self.dxs, self.size, self.flip, self.bounds, self.tp, self.portal_log = [], [], [], [], [], {}, []
        x, dx, m, fl, b = sx, SPEEDS[sp], sz, gv, (self.lo, self.hi)
        k = 0; t = 0
        while x < self.x1:
            x += dx
            hh = WAVE_HALF[m]
            while k < len(evs):
                o = evs[k]; i = int(o['1']); kind = ('speed' if i in SPEEDS else 'size' if i in SIZE else 'grav' if i in GRAV
                                                   else 'tp' if i == 747 else 'mode')
                bw, bh, bdx = PORTAL_BOX[kind]
                if x + hh < num(o, '2') + bdx - bw / 2: break
                if i in SPEEDS: dx = SPEEDS[i]
                elif i in SIZE: m = SIZE[i]
                elif i in GRAV: fl = GRAV[i]
                elif i == 747:
                    ny = num(o, '3') + num(o, '54'); self.tp[t] = ny; b = bounds_for(ny)
                elif i == 660: b = bounds_for(num(o, '3'))
                self.portal_log.append((t, kind, i, num(o, '2'), num(o, '3'), bh))
                k += 1
            self.xs.append(x); self.dxs.append(dx); self.size.append(m); self.flip.append(fl); self.bounds.append(b)
            t += 1
        self.n = len(self.xs)

    def step(self, t, y, hold):
        m = self.size[t]; k = 1 if m == 1 else 2
        if t in self.tp: y = self.tp[t]
        up = hold != self.flip[t]; e = 2 * WAVE_HALF[m]; lo, hi = self.bounds[t]
        ny = y + (self.dxs[t] * k if up else -self.dxs[t] * k)
        return round(min(max(ny, lo + e), hi - e), 6)

    def track(self, holds):
        y, out = round(self.y0, 6), []
        for t, hd in enumerate(holds):
            y = self.step(t, y, hd); out.append((self.xs[t], y, self.size[t], hd))
        return out

MARGINS = (0, 1, 2, 3, 4, 5, 6, 8, 10)

def search(run, T):
    layers = [{(round(run.y0, 6), False): ((99, 0, 0), None)}]
    for t in range(run.n):
        x = run.xs[t]; m = run.size[t]; nxt = {}
        for (y, hp), (sc, _) in layers[-1].items():
            for hold in (False, True):
                ny = run.step(t, y, hold)
                if run.W.hit(x, ny, m): continue
                mg = 0
                for g in MARGINS:
                    if run.W.hit(x, ny, m, g): break
                    mg = g
                s2 = (min(sc[0], min(mg, T)), sc[1] - (hold != hp), sc[2] + mg)
                k = (ny, hold)
                if k not in nxt or s2 > nxt[k][0]: nxt[k] = (s2, (y, hp))
        if not nxt:
            raise RuntimeError("틱 %d (x %.1f)에서 살아남는 경로 없음" % (t, x))
        layers.append(nxt)
    key = max(layers[-1], key=lambda k: layers[-1][k][0]); sc = layers[-1][key][0]
    path = []
    for t in range(len(layers) - 1, 0, -1):
        path.append(key); key = layers[t][key][1]
    path.reverse()
    return [h for _, h in path], sc

def rebuild(evs, n):
    h, s, j = [], False, 0; evs = sorted(evs)
    for t in range(n):
        while j < len(evs) and evs[j] == t: s = not s; j += 1
        h.append(s)
    return h

def alive(run, holds, ref=None, end=0):
    y = round(run.y0, 6)
    for t, hd in enumerate(holds):
        y = run.step(t, y, hd)
        if run.W.hit(run.xs[t], y, run.size[t]): return False
        if ref is not None and t > end and y == ref[t][1] and hd == ref[t][3]: return True
    return True

def windows(run, ev, MAXS=20):
    holds = rebuild(ev, run.n); ref = run.track(holds); res = []
    for i, t in enumerate(ev):
        press = holds[t]; nxt = ev[i + 1] if i + 1 < len(ev) else None
        moves = [t] + ([nxt] if nxt is not None else [])
        prev_t = ev[i - 1] if i > 0 else -1
        after = ev[i + len(moves)] if i + len(moves) < len(ev) else run.n
        def ok(d):
            a = [mv + d for mv in moves]
            if a[0] <= prev_t or a[-1] >= after or min(a) < 0: return False
            return alive(run, rebuild([e for e in ev if e not in moves] + a, run.n), ref, max(a) + 1)
        e = 0
        while e < MAXS and ok(-(e + 1)): e += 1
        l = 0
        while l < MAXS and ok(l + 1): l += 1
        res.append({"tick": t, "kind": "press" if press else "release", "early": e, "late": l, "window": e + l + 1})
    return res

BINS = [("17+", 17, 99), ("13+", 13, 16), ("9+", 9, 12), ("7-8", 7, 8), ("5-6", 5, 6),
        ("4", 4, 4), ("3", 3, 3), ("2", 2, 2), ("1", 1, 1)]

def solve(run, T=4.0):
    holds, sc = search(run, T)
    ev = sorted([t for t in range(1, run.n) if holds[t] != holds[t - 1]] + ([0] if holds[0] else []))
    for _ in range(2):
        res = windows(run, ev)
        for k, r in enumerate(res):
            sh = (r["late"] - r["early"]) // 2
            if sh == 0: continue
            moves = [ev[k]] + ([ev[k + 1]] if k + 1 < len(ev) else [])
            ev2 = sorted(set([e for e in ev if e not in moves] + [m + sh for m in moves]))
            if len(ev2) == len(ev) and alive(run, rebuild(ev2, run.n)): ev = ev2
    res = windows(run, ev); assert alive(run, rebuild(ev, run.n))
    return ev, res, sc

def portal_misses(run, ev):
    """경로가 실제로 포탈 히트박스 높이 안을 지났는지 (회전·크기 무시, 추정)"""
    tr = run.track(rebuild(ev, run.n)); miss = []
    for t, kind, i, px, py, bh in run.portal_log:
        y = tr[max(t - 1, 0)][1] if t > 0 else run.y0
        if abs(y - py) > bh / 2 + WAVE_HALF[run.size[t]]:
            miss.append((kind, i, round(px), round(py), round(y)))
    return miss
