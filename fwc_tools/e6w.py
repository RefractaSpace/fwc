import sys, json, os
os.environ['FWC_TRACK2'] = '1'
sys.path.insert(0, '/home/claude/full/fwc_app'); os.chdir('/home/claude/full/fwc_app')
import analyze, gdmacro, gdlevel
a, b, OFF = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
lv = open('/home/claude/ref/usermacro/sac_end6.txt', encoding='utf-8').read().strip(); m = gdmacro.load('/home/claude/ref/usermacro/dsssss.gdr2')
r = analyze.analyze(lv, m, offset=OFF, log=lambda s: None); tr = r.track_full; t2 = r.track2
print('죽음 %.2f초' % (r.death_tick / 240) if r.died else '끝', r.death_cause if r.died else '', len(tr))
inp = m.held_by_tick(offset=r.offset, merge=True)
for t in range(a, min(b, len(tr))):
    v = t2[t]; print(t, 'in', int(inp[t]), 'x %.1f y %.2f v %.2f %s %s gnd%d fl %.0f ce %.0f hold%d latch%d' % (tr[t][0], tr[t][1], v[2], tr[t][2], 'up' if v[3] else 'dn', v[6], v[4], v[5], v[7], v[8]))
if len(sys.argv) > 4:
    h, objs = gdlevel.parse_objects(lv); x0, x1, y0, y1 = map(float, sys.argv[4:8])
    for o in sorted(objs, key=lambda o: float(o['2'])):
        if x0 < float(o['2']) < x1 and y0 < float(o['3']) < y1:
            print('  ', {k: o[k] for k in sorted(o, key=lambda s: int(s) if s.isdigit() else 999) if k not in ('155','21','22','24','25','20','61','64','67','23','41','43','44','57')})
