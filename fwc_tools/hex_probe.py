import sys, os, json, importlib.util
sys.argv = [sys.argv[0], '/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr', 'plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
f = pr.D + 'Hexagon Force.gdr.json'; m = pr.gdmacro.load(f); lv = pr.LV[16]
h, objs = pr.gdlevel.parse_objects(lv)
obs = pr.engine_objects(lv)
for o in sorted(obs, key=lambda o: o.x):
    if o.kind in ('mode', 'portal') and 11500 <= o.x <= 14600: print('portal id', o.id, o.kind, o.arg, 'x %.0f y %.0f' % (o.x, o.y))
raw = json.load(open(f))
ff = [p for p in raw['frameFixes']]
print('keys', ff[9000].keys() if len(ff) > 9000 else ff[-1].keys())
r = pr.analyze.analyze(lv, m, log=lambda s: None); tr = r.track_full
print('death', r.death_tick, r.death_cause, 'offset', r.offset)
last = None
for p in ff:
    p1 = p.get('p1', {}); p2 = p.get('p2', {})
    if 'x' not in p1 or not (12600 <= p1['x'] <= 14100): continue
    t = p['frame']
    if t % 24 == 0 or (9100 <= t <= 9175 and t % 4 == 0):
        e = tr[t + 1] if t + 1 < len(tr) else None
        print(t, 'game p1 x %.0f y %.1f' % (p1['x'], p1['y'] - 90), '| p2 y %s' % ('%.1f' % (p2['y'] - 90) if 'y' in p2 else '-'), '| eng', 'y %.1f %s' % (e[1], e[2]) if e else '-')
