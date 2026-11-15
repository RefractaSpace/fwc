"""실제 기록에서 UFO가 올라가다 갑자기 멈춘 순간(머리 부딪힘)을 찾아, 위쪽 고정 블록과의 겹침을 잰다."""
import sys, os, json, importlib.util
os.environ['UFO_CORE'] = '0'
names = sys.argv[1:]
sys.argv = [sys.argv[0], '/tmp/_u.png']
spec = importlib.util.spec_from_file_location('pr', '/home/claude/viz/plot_repro_lib.py'); pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
for spec_ in names:
    name, lvid = spec_.rsplit(':', 1); lvid = int(lvid)
    m = pr.gdmacro.load(pr.D + name + '.gdr.json'); r = pr.analyze.analyze(pr.LV[lvid], m, log=lambda s: None); tr = r.track_full
    obs = [o for o in pr.engine_objects(pr.LV[lvid]) if o.kind == 'solid' and not getattr(o, 'groups', None)]
    ff = {p['frame']: p['p1'] for p in json.load(open(pr.D + name + '.gdr.json'))['frameFixes'] if 'x' in p.get('p1', {})}
    n = 0
    for t in sorted(ff):
        if t + 2 >= len(tr) or tr[t + 1][2] not in ('ufo',) or t - 2 not in ff or t + 1 not in ff: continue
        d0 = ff[t - 1]['y'] - ff[t - 2]['y']; d1 = ff[t]['y'] - ff[t - 1]['y']; d2 = ff[t + 1]['y'] - ff[t]['y']
        if d0 > 0.8 and d1 < d0 - 0.4 and d2 <= 0.05:
            y = ff[t]['y'] - 90; x = ff[t]['x']
            for half in (15.0, 9.0):
                for o in obs:
                    if o.maxx < x - half or o.minx > x + half: continue
                    ov = (y + half) - o.miny
                    if -1 < ov < 9 and o.y > y: print('  ', name, 'tick', t, 'x %.0f y %.2f dy %.2f→%.2f→%.2f' % (x, y, d0, d1, d2), '반높이', half, '겹침 %.3f' % ov, 'ID', o.id); n += 1
    print('==', name, '찾은 수', n)
