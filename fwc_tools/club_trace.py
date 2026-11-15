import sys, os, json
APP = '/home/claude/full/fwc_app'; sys.path.insert(0, APP)
os.environ['FWC_STRIG'] = '0'
import verify_main, gdmacro, gdlevel, analyze
P = analyze._load_engine()[0]
LV = verify_main.load_main_levels('/home/claude/ref/main/LevelData_v2.plist')
f = '/home/claude/ref/gdmacros/Clubstep.gdr.json'; m = gdmacro.load(f); lv = LV[14]
header, objs = gdlevel.parse_objects(lv); P.apply_header(lv)
P.DUAL_BAND_OVERRIDE = []; P.DUAL_BOUNDS_PLAYER = True; P.SPIDER_ENTRY_HOLD = True; P.TWIN_SPAWN_PUSH = True
objs = [dict(o, _noswap="1") if o["1"] == "392" else o for o in objs]
hs = gdlevel.header_start(header); P.STATE_TRIG = False
cwd = os.getcwd(); os.chdir(analyze.ENGINE); L = P.Level(objs); os.chdir(cwd)
p = P.Player(0.0, 15.0, speed=hs["speed"], mini=hs["mini"], upside=hs["flip"], mode=hs["mode"]); p.hit_ground(); p.bounds_around(15.0)
off = int(sys.argv[1]) if len(sys.argv) > 1 else 0
inp = m.held_by_tick(offset=off, merge=True)
fx = {q['frame']: q['p1'] for q in json.load(open(f))['frameFixes'] if 'x' in q.get('p1', {})}
for t in range(0, 9660):
    if p.dead: print('dead at', t, getattr(p, 'cause', '')); break
    P.step(p, L, inp[t])
    if 9576 <= t <= 9612 or (t % 6 == 0 and 9612 < t < 9660):
        g = fx.get(t, {})
        print(t, 'in', int(inp[t]), 'x %.1f y %.2f v %.3f' % (p.x, p.y, p.y_vel), 'up' if p.upside_down else 'dn', 'mini' if p.mini else 'big', 'gnd' if p.on_ground else 'air', 'hold', int(p.holding_jump), 'latch', int(p.spider_hold), p.mode, '| game x %.1f y %.2f' % (g.get('x', 0), g.get('y', 90) - 90))
