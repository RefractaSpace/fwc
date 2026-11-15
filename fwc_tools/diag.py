"""죽은 자리 진단: 엔진이 죽었다고 한 물체의 판정 상자와, 그 순간 실제 게임 위치를 나란히 출력. 쓰는 법: python diag.py "레벨 이름" """
import sys, os, json, re
APP = '/home/claude/full/fwc_app'; sys.path.insert(0, APP)
import verify_main, gdmacro, gdlevel, analyze
P = analyze._load_engine()[0]
LV = verify_main.load_main_levels('/home/claude/ref/main/LevelData_v2.plist')
name = sys.argv[1]; f = '/home/claude/ref/gdmacros/%s.gdr.json' % name
m = gdmacro.load(f); lv = LV[int(m.level_id)]
r = analyze.analyze(lv, m, log=lambda s: None)
fx = {p['frame']: p['p1'] for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
print(name, 'died' if getattr(r, 'died', False) else 'ok', getattr(r, 'death_cause', ''), 'tick', getattr(r, 'death_tick', None), 'offset', r.offset, 'track', len(r.track))
if not getattr(r, 'died', False): sys.exit()
header, objs = gdlevel.parse_objects(lv); P.apply_header(lv)
cwd = os.getcwd(); os.chdir(analyze.ENGINE)
try: L = P.Level(objs)
finally: os.chdir(cwd)
mo = re.search(r'ID (\d+), x (\d+)', r.death_cause)
dt = r.death_tick
if mo:
    oid, ox = int(mo.group(1)), float(mo.group(2))
    seen = {}
    for c in L.cells.values():
        for o in c: seen[o.uid] = o
    near = sorted((o for o in seen.values() if o.id == oid and abs(o.x - ox) <= 1.0), key=lambda o: o.y)
    for o in near: print('  물체 id %d kind %s  x %.1f y %.1f  상자 x[%.1f..%.1f] y[%.1f..%.1f] rot %s' % (o.id, o.kind, o.x, o.y, o.minx, o.maxx, o.miny, o.maxy, o.rot))
    raw = [o for o in objs if o['1'] == str(oid) and abs(float(o['2']) - ox) <= 1.0]
    for o in raw[:4]: print('  원본 키:', {k: v for k, v in o.items() if k not in ('155',)})
print('  틱   게임x    게임y(보정)  | 엔진x   엔진y  모드')
for t in range(dt - 14, dt + 6):
    g = fx.get(t - r.offset) or fx.get(t)
    e = r.track_full[t] if t < len(r.track_full) else None
    print('  %5d %8.1f %8.1f   | %s' % (t, g['x'] if g else -1, (g['y'] - verify_main.Y_SHIFT) if g else -1, ('%8.1f %7.1f %s' % (e[0], e[1], e[2])) if e else '(엔진 기록 없음)'))
