"""엔진과 실제 기록이 처음 크게 벌어지는 곳을 찾아 틱별 상태와 주변 물체를 보여 준다. 쓰는 법: python div_trace.py 레벨이름 레벨번호 [시작틱] [허용차]"""
import sys, json, importlib.util, os
os.environ['FWC_TRACK2']='1'
name, lvid = sys.argv[1], int(sys.argv[2]); a0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0; tol = float(sys.argv[4]) if len(sys.argv) > 4 else 5.0
sys.argv=[sys.argv[0],'/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr','/home/claude/viz/plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
f = pr.D+name+'.gdr.json'; m = pr.gdmacro.load(f); r = pr.analyze.analyze(pr.LV[lvid], m, log=lambda s: None); tr = r.track_full; t2 = r.track2
ff = {p['frame']: p for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})}
inp = m.held_by_tick(offset=r.offset, merge=True); sh = 1 + r.offset
print(name, '|', ('죽음 %.2f초 x %.0f %s' % (r.death_tick/240, r.death_x, r.death_cause)) if r.died else '끝까지 감', '| len', len(tr), 'offset', r.offset)
def gy(u): return ff[u]['p1']['y'] - 90
k = next((t for t in range(max(a0, 3), len(tr) - 2) if all(u in ff for u in (t+sh-1, t+sh, t+sh+1)) and min(abs(tr[t][1] - gy(u)) for u in (t+sh-1, t+sh, t+sh+1)) > tol), None)
print('처음 %.0f 넘게 벌어지는 틱' % tol, k, ('%.2f초' % (k/240)) if k else '')
if k is None: k = len(tr) - 12
M = ['cube','ship','ball','ufo','wave','robot','spider','swing']
for t in list(range(k - 12, k + 4)) + list(range(k + 4, min(len(tr), k + 100), 8)):
    if t < 0 or t >= len(tr) or t + sh not in ff: continue
    v = t2[t]; g = ff[t + sh]['p1']; g2 = ff[t + sh].get('p2', {})
    print(t, 'in', int(inp[t]), 'eng x %.1f y %.2f v %.2f %s %s gnd%d fl %.0f ce %.0f' % (tr[t][0], tr[t][1], v[2], tr[t][2], 'up' if v[3] else 'dn', v[6], v[4], v[5]), ('| P2 y %.2f %s %s' % (v[9], 'up' if v[11] else 'dn', M[int(v[17])])) if v[0] else '', '| game x %.1f y %.2f' % (g['x'], g['y'] - 90), ('P2 y %.2f' % (g2['y'] - 90)) if 'y' in g2 else '')
h, objs = pr.gdlevel.parse_objects(pr.LV[lvid]); T = json.load(open('/home/claude/full/fwc_app/engine/gd_table.json')); x0 = tr[k][0]; y0 = gy(k + sh)
KNOWN = set(T) | {str(i) for i in list(pr.P.ORBS) + list(pr.P.PADS) + list(pr.P.PORTAL_BOX) + list(pr.P.MODE_PORTALS)} | {'747','901','1049','1346','1347','1616','1268','200','201','202','203','1334'}
n = 0
for o in sorted(objs, key=lambda o: float(o['2'])):
    if abs(float(o['2']) - x0) < 75 and abs(float(o['3']) - y0) < 110 and o['1'] in KNOWN:
        print('  obj', {kk: o[kk] for kk in sorted(o, key=lambda s: int(s) if s.isdigit() else 999) if kk not in ('155','21','22','24','25','20','61','64','67','23','41','43','44')}); n += 1
        if n > 28: break
