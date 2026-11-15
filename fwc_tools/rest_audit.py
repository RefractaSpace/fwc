"""실제 기록에서 '가만히 얹혀 있는' 순간을 찾아, 바로 아래/위 물체와의 틈을 잰다 (물체 표 검사용). 쓰는 법: python rest_audit.py 레벨이름 레벨번호 [물체ID]"""
import sys, json, importlib.util, collections
name, lvid = sys.argv[1], int(sys.argv[2]); want = int(sys.argv[3]) if len(sys.argv) > 3 else None
sys.argv = [sys.argv[0], '/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr', '/home/claude/viz/plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
obs = [o for o in pr.engine_objects(pr.LV[lvid]) if o.kind == 'solid' and not getattr(o, 'groups', None)]
ff = [p for p in json.load(open(pr.D + name + '.gdr.json'))['frameFixes'] if 'x' in p.get('p1', {})]
obs.sort(key=lambda o: o.minx)
res = collections.defaultdict(list)
i = 0
while i < len(ff) - 8:
    y = ff[i]['p1']['y'] - 90
    j = i
    while j + 1 < len(ff) and abs(ff[j + 1]['p1']['y'] - 90 - y) < 1e-3: j += 1
    if j - i >= 6:
        x0, x1 = ff[i]['p1']['x'], ff[j]['p1']['x']; xm = (x0 + x1) / 2
        best = None
        for o in obs:
            if o.maxx < xm - 9 or o.minx > xm + 9: continue
            for half in (15.0, 9.0):
                for side, gap in (('위에 얹힘', (y - half) - o.maxy), ('아래 붙음', o.miny - (y + half))):
                    if abs(gap) < 12 and (best is None or abs(gap) < abs(best[0])): best = (gap, o.id, side, half, o.y, o.maxy - o.miny)
        if best and (want is None or best[1] == want): res[(best[1], best[2], best[3])].append((round(best[0], 2), round(xm), round(y, 2), best[4]))
    i = j + 1
for k in sorted(res):
    gaps = [g[0] for g in res[k]]
    print('ID', k[0], k[1], '반높이', k[2], '횟수', len(gaps), '틈 값들', sorted(set(gaps))[:8], '예', res[k][0])
