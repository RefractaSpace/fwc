"""trigmove.py — 이동 트리거(901)·중력 트리거(2066)를 '플레이어 x만으로' 계산 (내가 짠 코드)

생각: 플레이어 가로 위치 x는 버튼과 상관없이 시간에 따라 정해짐(속도 포탈만 바꿈).
그래서 '트리거를 지난 뒤 몇 틱 지났나'를 x로부터 계산할 수 있음 → 플레이어 상태에 아무것도 안 더해도 됨.
  - 이동: 대상 그룹을 (dx, dy) 만큼 duration 초 동안 easing 곡선으로. 키 58(=플레이어 X 따라가기)이면
          가로는 그동안 플레이어가 움직인 만큼 같이 움직임 (그 축만, 세로는 그대로 easing)
  - 키 뜻: 10 시간, 28/29 dx/dy(블럭당 30), 30 easing 종류(0~18), 85 easing 세기(없으면 2), 51 대상 그룹,
          58/59 플레이어 X/Y 따라가기, 141 카메라 X 따라가기(=플레이어 X로 봄), 143 따라가기 배율, 544 바로 도착,
          62 생성(spawn)으로만 / 11 닿을 때만 → 이 둘은 x로 안 켜지므로 여기선 안 다룸
    (키 뜻 출처: GD-Remake 설명에 적힌 gddocs·2.2081 디컴파일 인용. 내가 직접 확인한 건 아님)
  - 시작: 플레이어 x가 트리거 x에 닿은 틱부터, 1/240초마다 진행 (추정)
  - 중력 트리거(2066) 키 148: 그 x부터 중력에 곱하는 배율 (추정 — 다음 중력 트리거까지 유지)
  - easing 식: cocos2d-x 표준 식 (GD가 쓰는 엔진)
"""
import math, bisect

def _bounce(t):
    if t < 1 / 2.75: return 7.5625 * t * t
    if t < 2 / 2.75: t -= 1.5 / 2.75; return 7.5625 * t * t + 0.75
    if t < 2.5 / 2.75: t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375
    t -= 2.625 / 2.75; return 7.5625 * t * t + 0.984375

def ease(kind, rate, t):
    if kind == 0: return t
    if kind == 1:
        t *= 2.0
        return 0.5 * t ** rate if t < 1.0 else 1.0 - 0.5 * (2.0 - t) ** rate
    if kind == 2: return t ** rate
    if kind == 3: return t ** (1.0 / rate) if rate else t
    if kind in (4, 5, 6):
        if t in (0.0, 1.0): return t
        per = rate or 0.3
        if kind == 4:
            per = rate or 0.45; s = per / 4.0; u = t * 2.0 - 1.0
            if u < 0: return -0.5 * 2.0 ** (10.0 * u) * math.sin((u - s) * 2 * math.pi / per)
            return 2.0 ** (-10.0 * u) * math.sin((u - s) * 2 * math.pi / per) * 0.5 + 1.0
        s = per / 4.0
        if kind == 5:
            u = t - 1.0; return -(2.0 ** (10.0 * u)) * math.sin((u - s) * 2 * math.pi / per)
        return 2.0 ** (-10.0 * t) * math.sin((t - s) * 2 * math.pi / per) + 1.0
    if kind == 7: return (1.0 - _bounce(1.0 - t * 2.0)) * 0.5 if t < 0.5 else _bounce(t * 2.0 - 1.0) * 0.5 + 0.5
    if kind == 8: return 1.0 - _bounce(1.0 - t)
    if kind == 9: return _bounce(t)
    if kind == 10:
        if t in (0.0, 1.0): return t
        t *= 2.0
        return 0.5 * 2.0 ** (10.0 * (t - 1.0)) if t < 1.0 else 0.5 * (2.0 - 2.0 ** (-10.0 * (t - 1.0)))
    if kind == 11: return 0.0 if t == 0.0 else 2.0 ** (10.0 * (t - 1.0)) - 0.001
    if kind == 12: return 1.0 if t == 1.0 else 1.0 - 2.0 ** (-10.0 * t)
    if kind == 13: return -0.5 * (math.cos(math.pi * t) - 1.0)
    if kind == 14: return 1.0 - math.cos(t * math.pi / 2.0)
    if kind == 15: return math.sin(t * math.pi / 2.0)
    o = 1.70158
    if kind == 16:
        o2 = o * 1.525; t *= 2.0
        if t < 1.0: return t * t * ((o2 + 1.0) * t - o2) / 2.0
        t -= 2.0; return t * t * ((o2 + 1.0) * t + o2) / 2.0 + 1.0
    if kind == 17: return t * t * ((o + 1.0) * t - o)
    if kind == 18: t -= 1.0; return t * t * ((o + 1.0) * t + o) + 1.0
    return t

def fnum(o, k, d=0.0):
    try: return float(o.get(k, d))
    except ValueError: return d

class Speed:
    """속도 포탈로 정한 '1틱에 x 몇 칸' 지도. 포탈은 플레이어 몸(30칸)의 오른쪽 끝이 포탈 왼쪽 끝에 닿을 때 바뀐다고 봄 (추정)"""
    def __init__(self, objs, speed_table, speed_id, dt, box):
        rows = []
        for o in objs:
            i = int(o['1'])
            if i in speed_id and o.get('121') != '1':
                sp, _, _, mul = speed_table[speed_id[i]]
                rows.append((fnum(o, '2') - box[i][0] * fnum(o, '32', 1) / 2.0 - 15.0, sp * mul * dt))
        rows.sort()
        self.xs = [r[0] for r in rows]; self.v = [r[1] for r in rows]
        if not self.v: self.xs, self.v = [-1e18], [speed_table[1][0] * speed_table[1][3] * dt]
    def _v(self, i): return self.v[max(i, 0)]
    def ticks(self, x0, x1):
        """x0 → x1 까지 걸리는 틱 (x1 >= x0)"""
        if x1 <= x0: return 0.0
        i = bisect.bisect_right(self.xs, x0) - 1; t = 0.0; x = x0
        while True:
            nx = self.xs[i + 1] if i + 1 < len(self.xs) else 1e18
            if x1 <= nx: return t + (x1 - x) / self._v(i)
            t += (nx - x) / self._v(i); x = nx; i += 1
    def advance(self, x0, n):
        """x0에서 n틱 뒤의 x"""
        i = bisect.bisect_right(self.xs, x0) - 1; x = x0
        while True:
            nx = self.xs[i + 1] if i + 1 < len(self.xs) else 1e18
            need = (nx - x) / self._v(i)
            if n <= need: return x + n * self._v(i)
            n -= need; x = nx; i += 1

class Move:
    __slots__ = ("tx", "g", "dx", "dy", "ticks", "easing", "rate", "lock_x", "mod_x", "forever", "x_end")

class Moves:
    def __init__(self, objs, spd):
        self.spd = spd; self.by_group = {}; self.all = []
        for o in objs:
            if o['1'] != '901' or fnum(o, '62') or fnum(o, '11') or o.get('121') == '1': continue
            g = int(fnum(o, '51'))
            if g <= 0: continue
            m = Move(); m.tx = fnum(o, '2'); m.g = g
            m.dx, m.dy = fnum(o, '28'), fnum(o, '29')
            d = fnum(o, '10')
            m.forever = (d == -1.0)
            m.ticks = 0.0 if (fnum(o, '544') or d <= 0) else d * 240.0
            m.easing = int(fnum(o, '30')); m.rate = fnum(o, '85', 2.0)
            m.lock_x = bool(fnum(o, '58') or fnum(o, '141')); m.mod_x = fnum(o, '143', 1.0)
            m.x_end = 1e18 if m.forever else spd.advance(m.tx, m.ticks)
            self.by_group.setdefault(g, []).append(m); self.all.append(m)
        for v in self.by_group.values(): v.sort(key=lambda m: m.tx)
        self.groups = set(self.by_group)
    def offset(self, g, x):
        """그룹 g가 플레이어 x일 때 원래 자리에서 얼마나 옮겨졌나 (ox, oy)"""
        ox = oy = 0.0
        for m in self.by_group.get(g, ()):
            if x < m.tx: break
            if m.ticks <= 0: e = 1.0
            else:
                tl = self.spd.ticks(m.tx, x) if x < m.x_end else m.ticks
                e = ease(m.easing, m.rate, min(1.0, tl / m.ticks))
            if m.lock_x: ox += (min(x, m.x_end) - m.tx) * m.mod_x
            else: ox += m.dx * e
            oy += m.dy * e
        return ox, oy

class GravMods:
    def __init__(self, objs):
        r = sorted((fnum(o, '2'), fnum(o, '148', 1.0)) for o in objs if o['1'] == '2066' and not fnum(o, '62') and not fnum(o, '11'))
        self.xs = [a for a, _ in r]; self.v = [b for _, b in r]
    def at(self, x):
        i = bisect.bisect_right(self.xs, x) - 1
        return self.v[i] if i >= 0 else 1.0
