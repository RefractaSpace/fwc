import sys, os, json, importlib.util
sys.argv = [sys.argv[0], '/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr', 'plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
analyze, gdmacro = pr.analyze, pr.gdmacro
f = pr.D + 'Clubstep.gdr.json'; m = gdmacro.load(f); lv = pr.LV[14]
obs = pr.engine_objects(lv)
for o in sorted(obs, key=lambda o: o.x):
    if 12380 <= o.x <= 12620 and 150 <= o.y <= 340: print('obj id', o.id, o.kind, o.arg if hasattr(o,'arg') else '', 'x %.1f y %.1f' % (o.x, o.y), 'box %.0f %.0f %.0f %.0f' % (o.minx, o.maxx, o.miny, o.maxy), 'rot', getattr(o,'rot',None))
r = analyze.analyze(lv, m, log=lambda s: None); tr = r.track_full
fx = {p['frame']: p['p1'] for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
print('inputs', [(a, d) for a, d, p2, b in m.inputs if 9540 <= a <= 9680 and b == 1])
for t in range(9590, min(len(tr), 9660), 3):
    g = fx.get(t - 1) or {}
    print(t, 'eng x %.1f y %.2f %s' % (tr[t][0], tr[t][1], tr[t][2]), '| game x %.1f y %.2f' % (g.get('x', 0), g.get('y', 90) - 90), {k: v for k, v in g.items() if k not in ('x', 'y')})
print(r.death_tick, r.death_cause)
