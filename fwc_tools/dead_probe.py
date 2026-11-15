import sys, json, importlib.util, os
os.environ['FWC_TRACKG'] = '11'; os.environ['FWC_TRACK2'] = '1'
sys.argv=[sys.argv[0],'/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr','/home/claude/viz/plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
f = pr.D+'Deadlocked.gdr.json'; m = pr.gdmacro.load(f); r = pr.analyze.analyze(pr.LV[20], m, log=lambda s: None); tr = r.track_full; g = r.trackg; t2 = r.track2
print('Deadlocked:', '죽음 %.2f초 x %.0f %s' % (r.death_tick/240, r.death_x, r.death_cause) if r.died else '끝까지 감', 'offset', r.offset)
ff = {p['frame']: p['p1'] for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
for t in list(range(6380, 6390)) + list(range(6404, 6420, 3)):
    d = g[0][1] - g[t][1]; v = t2[t]; gm = ff.get(t + 1 + r.offset, {})
    print(t, 'x %.1f y %.3f v %.3f' % (tr[t][0], tr[t][1], v[2]), '| 겹침 %.3f' % (tr[t][1] + 15 - (390 - d)), '| 게임 x %.1f y %.3f' % (gm.get('x', 0), gm.get('y', 90) - 90), '차 %+.3f' % (tr[t][1] - (gm.get('y', 90) - 90)))
