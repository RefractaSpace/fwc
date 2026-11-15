import sys, os, json, importlib.util
sys.argv = [sys.argv[0], '/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr', 'plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
P, analyze, gdmacro, gdlevel = pr.P, pr.analyze, pr.gdmacro, pr.gdlevel
f = pr.D + 'Dash.gdr.json'; m = gdmacro.load(f); lv = pr.LV[22]
header, objs = gdlevel.parse_objects(lv); P.apply_header(lv)
cwd = os.getcwd(); os.chdir(analyze.ENGINE); L = P.Level(objs); os.chdir(cwd)
print('STATE_TRIG', P.STATE_TRIG)
PIL = {}
for xi in range(4900, 5400):
    xx = xi / 10.0; top = [o for o in L.moved(xx) if o.groups == [39]]
    PIL[xi] = (max(o.maxy for o in top), min(o.minx for o in top), max(o.maxx for o in top))
r = analyze.analyze(lv, m, log=lambda s: None); tr = r.track_full
print('STATE_TRIG after', P.STATE_TRIG)
fx = {p['frame']: p['p1'] for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
print('track len', len(tr), 'death_tick', r.death_tick, 'sample', tr[-1])
n = len(tr)
for k in range(n - 12, n):
    t = tr[k]; tick = r.death_tick - (n - 1 - k)
    tops, mnx, mxx = PIL[int(round(t[0] * 10))]; g = fx.get(tick)
    print('tick', tick, 'engine x %.2f y %.2f bottom %.2f' % (t[0], t[1], t[1] - 15), '| pillar top %.2f box x %d~%d' % (tops, mnx, mxx), '| game x %.2f y %.2f' % (g['x'], g['y'] - 90) if g else '')
