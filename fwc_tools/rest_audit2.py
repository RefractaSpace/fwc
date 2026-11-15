"""블록 아래 붙어 있던 순간의 모드·중력을 엔진 재생에서 가져와 같이 보여 준다."""
import sys, os, json, importlib.util, collections
os.environ['FWC_TRACK2'] = '1'
names = sys.argv[1:]
sys.argv = [sys.argv[0], '/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr', '/home/claude/viz/plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
import verify_main
for spec_ in names:
    name, lvid = spec_.rsplit(':', 1); lvid = int(lvid)
    m = pr.gdmacro.load(pr.D + name + '.gdr.json'); r = pr.analyze.analyze(pr.LV[lvid], m, log=lambda s: None)
    tr, t2 = r.track_full, r.track2
    obs = [o for o in pr.engine_objects(pr.LV[lvid]) if o.kind == 'solid' and not getattr(o, 'groups', None)]
    ff = [p for p in json.load(open(pr.D + name + '.gdr.json'))['frameFixes'] if 'x' in p.get('p1', {})]
    res = collections.Counter(); i = 0
    while i < len(ff) - 8:
        y = ff[i]['p1']['y'] - 90; j = i
        while j + 1 < len(ff) and abs(ff[j + 1]['p1']['y'] - 90 - y) < 1e-3: j += 1
        if j - i >= 6:
            xm = (ff[i]['p1']['x'] + ff[j]['p1']['x']) / 2; tk = ff[(i + j) // 2]['frame'] + 1
            if tk < len(tr) and tk in t2:
                mode = tr[tk][2]; up = int(t2[tk][3])
                for o in obs:
                    if o.maxx < xm - 9 or o.minx > xm + 9: continue
                    for half in (15.0, 9.0):
                        gap = o.miny - (y + half)
                        if abs(gap) < 8: res[(mode, '중력 뒤집힘' if up else '보통 중력', half, round(gap, 2))] += 1
        i = j + 1
    print('==', name)
    for k, c in sorted(res.items()): print('  ', k, c)
