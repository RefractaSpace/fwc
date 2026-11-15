import sys, os, json
sys.path.insert(0, '/home/claude/full/fwc_app'); os.chdir('/home/claude/full/fwc_app')
os.environ['FWC_TRACK2'] = '1'
import analyze, gdmacro
lv = open('tests/sac3.txt', encoding='utf-8').read().strip(); m = gdmacro.load('tests/sac_route.gdr2')
def run(env):
    for k in ('BALL_ORB_EATS', 'FWC_DBP'): os.environ.pop(k, None)
    os.environ.update(env)
    r = analyze.analyze(lv, m, log=lambda s: None, sac_patch=True)
    return r
import pickle
out = {}
for name, env in (('base', {}), ('orb', {'BALL_ORB_EATS': 'blue,green'}), ('dbp0', {'FWC_DBP': '0'})):
    r = run(env); out[name] = (r.track_full, r.track2, r.died, getattr(r, 'death_tick', None), r.death_cause, r.offset)
    print(name, 'died', r.died, getattr(r, 'death_tick', None), r.death_cause)
out['inp'] = m.held_by_tick(offset=out['base'][5], merge=True)
pickle.dump(out, open('/tmp/sac_dual.pkl', 'wb'))
