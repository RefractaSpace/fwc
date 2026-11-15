import sys, os
sys.path.insert(0, '/home/claude/full/fwc_app'); os.chdir('/home/claude/full/fwc_app')
os.environ['FWC_TRACK2'] = '1'
import analyze, gdmacro
lv = open('tests/sac3.txt', encoding='utf-8').read().strip(); m = gdmacro.load('tests/sac_route.gdr2')
out = {}
for name, v in (('old', '0'), ('new', '1')):
    os.environ['UFO_CORE'] = v
    r = analyze.analyze(lv, m, log=lambda s: None, sac_patch=True); out[name] = r
    print(name, r.died, getattr(r, 'death_tick', None), r.death_cause)
a, b = out['old'].track2, out['new'].track2; tr = out['old'].track_full
M = ['cube','ship','ball','ufo','wave','robot','spider','swing']
k = next(t for t in sorted(set(a) & set(b)) if any(abs(u - v) > 1e-3 for u, v in zip(a[t], b[t])))
print('first diff', k, '%.2f초' % (k / 240), 'x %.0f' % tr[k][0], tr[k][2])
inp = m.held_by_tick(offset=out['old'].offset, merge=True)
def row(v): return 'P1 y %.2f v %.2f %s g%d | P2 y %.2f v %.2f %s g%d %s' % (v[1], v[2], 'up' if v[3] else 'dn', v[6], v[9], v[10], 'up' if v[11] else 'dn', v[14], M[int(v[17])])
for t in range(k - 4, k + 10):
    print(t, 'in', int(inp[t]), 'x %.1f' % tr[t][0], 'OLD', row(a[t])); print(' ' * 22, 'NEW', row(b[t]) if t in b else '-')
