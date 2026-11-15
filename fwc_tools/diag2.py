"""처음 어긋나는 순간 진단. 쓰는 법: python diag2.py "레벨 이름" [기준 차이=2.0]
엔진과 게임 높이가 처음 벌어지는 틱을 찾고, 그 주변 위치와 근처 물체(엔진이 아는 것 / 무시한 것)를 출력"""
import sys, os, json
APP = '/home/claude/full/fwc_app'; sys.path.insert(0, APP)
import verify_main, gdmacro, gdlevel, analyze
P = analyze._load_engine()[0]
LV = verify_main.load_main_levels('/home/claude/ref/main/LevelData_v2.plist')
name = sys.argv[1]; thr = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
f = '/home/claude/ref/gdmacros/%s.gdr.json' % name
m = gdmacro.load(f); lv = LV[int(m.level_id)]
r = analyze.analyze(lv, m, log=lambda s: None)
fx = {p['frame']: p['p1'] for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
tr = r.track_full
YS = verify_main.Y_SHIFT
def d(t, sh):
    g = fx.get(t - sh); return None if g is None or t >= len(tr) else tr[t][1] + YS - g['y']
# 틱 어긋남(sh) 고르기: 처음 2000틱에서 차이 중앙값이 가장 작은 것
best = min(range(-2, 3), key=lambda sh: sorted(abs(v) for v in (d(t, sh) for t in range(0, min(2000, len(tr)))) if v is not None)[len([1 for t in range(0, min(2000, len(tr))) if d(t, sh) is not None]) // 2])
first = next((t for t in range(len(tr)) if d(t, best) is not None and abs(d(t, best)) > thr), None)
print(name, '| 죽음' if getattr(r, 'died', False) else '| 끝까지', getattr(r, 'death_cause', ''), '| 틱맞춤', best, '| 처음 벌어지는 틱', first, '(%.2f초)' % (first / 240.0) if first else '')
if first is None: sys.exit()
print('  틱    게임x   게임y | 엔진x   엔진y 모드 | 차이')
for t in list(range(first - 8, first + 1)) + list(range(first + 4, first + 40, 6)):
    if t < 0 or t >= len(tr): continue
    g = fx.get(t - best)
    if g: print('  %5d %8.1f %7.1f | %8.1f %7.1f %-6s | %+6.1f' % (t, g['x'], g['y'] - YS, tr[t][0], tr[t][1], tr[t][2], tr[t][1] + YS - g['y']))
gx, gy = fx[first - best]['x'], fx[first - best]['y'] - YS
header, objs = gdlevel.parse_objects(lv); P.apply_header(lv)
cwd = os.getcwd(); os.chdir(analyze.ENGINE)
try: L = P.Level(objs)
finally: os.chdir(cwd)
known = {}
for c in L.cells.values():
    for o in c: known[(o.id, round(o.x, 1), round(o.y, 1))] = o
print('  근처 물체 (게임 위치 x %.0f y %.0f 기준, 가로 ±75 세로 ±75):' % (gx, gy))
for o in sorted(objs, key=lambda o: float(o['2'])):
    x, y, i = float(o['2']), float(o['3']), int(o['1'])
    if abs(x - gx) <= 75 and abs(y - gy) <= 75:
        k = known.get((i, round(x, 1), round(y, 1)))
        extra = {a: b for a, b in o.items() if a not in ('1', '2', '3', '155', '21', '22', '23', '24', '25', '20', '61', '64', '67')}
        print('    id %-5d x %7.1f y %6.1f  %s  %s' % (i, x, y, ('엔진: ' + k.kind) if k else '엔진: 무시(장식 취급)', extra if extra else ''))
