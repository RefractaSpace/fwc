import sys, json, importlib.util, os
name, lvid = sys.argv[1], int(sys.argv[2])
sys.argv=[sys.argv[0],'/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr','/home/claude/viz/plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
f = pr.D+name+'.gdr.json'; m = pr.gdmacro.load(f); r = pr.analyze.analyze(pr.LV[lvid], m, log=lambda s: None); tr = r.track_full
ff = {p['frame']: p['p1'] for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
sh = 1 + r.offset; prev = None; k0 = min(ff)
print(name, 'offset', r.offset, 'first game frames', [(t, round(ff[t]['x'], 3)) for t in range(k0, k0 + 3)], 'eng', [round(tr[t][0], 3) for t in range(0, 4)])
n = 0
for t in range(1, len(tr) - 1):
    if t + sh not in ff or t + sh - 1 not in ff: continue
    d = ff[t + sh]['x'] - tr[t][0]
    if prev is None or abs(d - prev) > 0.02:
        print(t, '%.2f초' % (t / 240), 'eng x %.2f %s' % (tr[t][0], tr[t][2]), '게임x-엔진x = %+.3f' % d, '| 틱당 x: 엔진 %.4f 게임 %.4f' % (tr[t][0] - tr[t-1][0], ff[t+sh]['x'] - ff[t+sh-1]['x'])); n += 1
        if n > 40: break
    prev = d
