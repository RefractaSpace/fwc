"""wave_sim.py — 하드 웨이브 오프라인 계산 (보수적 모델: 벽·경사·톱니·가시에 닿으면 죽음)
단위: GD 유닛, 틱 = 1/240 s, 4x 속도 → 틱당 x +2.4
"""
import math, sys, json
from collections import defaultdict

DXT = 576.0 / 240.0
WAVE_HALF = {1: 5.0, 0: 3.0}            # 일반 10x10, 미니 6x6 (미니는 추정)
SLOPE45 = {326, 1338}; SLOPE26 = {327, 1339}
SOLIDS = {328: (22, 22), 329: (43, 22), 468: (30, 1.5), 1: (30, 30)}
SPIKES = {8: (6, 12), 39: (6, 5.6)}
SAWS = {1734: 32.0}

def load(path):
    objs = []
    for p in open(path, encoding="utf-8").read().split(';')[1:]:
        f = p.split(','); o = dict(zip(f[0::2], f[1::2]))
        if 'o' in o or '1' not in o: continue
        objs.append(o)
    return objs

def transform(pts, o):
    sx = float(o.get('128', o.get('32', 1))); sy = float(o.get('129', o.get('32', 1)))
    fx = -1 if o.get('4') == '1' else 1; fy = -1 if o.get('5') == '1' else 1
    a = math.radians(-float(o.get('6', 0)))           # GD: 양수 = 시계 방향
    c, s = math.cos(a), math.sin(a)
    cx, cy = float(o['2']), float(o['3'])
    out = []
    for x, y in pts:
        x, y = x * sx * fx, y * sy * fy
        out.append((cx + x * c - y * s, cy + x * s + y * c))
    return out

def shapes(objs):
    out = []
    for o in objs:
        i = int(o['1'])
        if i in SLOPE45:
            poly = transform([(-15, -15), (15, -15), (15, 15)], o)
        elif i in SLOPE26:
            poly = transform([(-30, -15), (30, -15), (30, 15)], o)
        elif i in SOLIDS or i in SPIKES:
            w, h = SOLIDS.get(i) or SPIKES[i]
            poly = transform([(-w/2, -h/2), (w/2, -h/2), (w/2, h/2), (-w/2, h/2)], o)
        elif i in SAWS:
            r = SAWS[i] * float(o.get('32', 1))
            out.append(('circle', (float(o['2']), float(o['3']), r), float(o['2']) - r, float(o['2']) + r))
            continue
        else:
            continue
        xs = [p[0] for p in poly]
        out.append(('poly', poly, min(xs), max(xs)))
    return out

def sat_box_poly(cx, cy, h, poly):
    box = [(cx-h, cy-h), (cx+h, cy-h), (cx+h, cy+h), (cx-h, cy+h)]
    for shape in (box, poly):
        n = len(shape)
        for k in range(n):
            x1, y1 = shape[k]; x2, y2 = shape[(k+1) % n]
            ax, ay = y2 - y1, x1 - x2
            p1 = [ax*x + ay*y for x, y in box]; p2 = [ax*x + ay*y for x, y in poly]
            if max(p1) <= min(p2) + 1e-9 or max(p2) <= min(p1) + 1e-9:
                return False
    return True

def box_circle(cx, cy, h, c):
    x, y, r = c
    dx = max(abs(cx - x) - h, 0); dy = max(abs(cy - y) - h, 0)
    return dx*dx + dy*dy < r*r

class World:
    def __init__(self, sh):
        self.grid = defaultdict(list)
        self.cache = {}
        for s in sh:
            for g in range(int(s[2] // 60) - 1, int(s[3] // 60) + 2):
                self.grid[g].append(s)
    def hit(self, x, y, h):
        key = (round(x, 3), y, h)
        c = self.cache.get(key)
        if c is not None:
            return c
        r = self._hit(x, y, h)
        self.cache[key] = r
        return r
    def _hit(self, x, y, h):
        for kind, geo, x0, x1 in self.grid[int(x // 60)]:
            if x1 < x - h or x0 > x + h: continue
            if kind == 'poly' and sat_box_poly(x, y, h, geo): return True
            if kind == 'circle' and box_circle(x, y, h, geo): return True
        return False
