import sys, json, importlib.util, os
os.environ['FWC_TRACK2']='1'
a0, a1, st = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
sys.argv=[sys.argv[0],'/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr','/home/claude/viz/plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
f = pr.D+'Deadlocked.gdr.json'; m = pr.gdmacro.load(f); r = pr.analyze.analyze(pr.LV[20], m, log=lambda s: None); tr = r.track_full; t2 = r.track2
ff = {p['frame']: p['p1'] for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
inp = m.held_by_tick(offset=r.offset, merge=True)
print('Deadlocked:', '죽음 %.2f초 x %.0f %s' % (r.death_tick/240, r.death_x, r.death_cause) if r.died else '끝까지 감', 'len', len(tr), 'offset', r.offset)
k = next((t for t in range(a0, len(tr)) if t in ff and min(abs(tr[t][1] - (ff[u]["y"] - 90)) for u in (t-2,t-1,t,t+1,t+2) if u in ff) > 6), None)
print('6 넘게 벌어지는 첫 틱', k)
for t in range(a0, min(a1, len(tr)), st):
    v = t2[t]; g = ff[t]
    print(t, 'in', int(inp[t]), 'eng x %.1f y %.2f v %.2f %s %s%s gnd%d' % (tr[t][0], tr[t][1], v[2], tr[t][2], 'up' if v[3] else 'dn', '', v[6]), '| game x %.1f y %.2f' % (g['x'], g['y'] - 90))
