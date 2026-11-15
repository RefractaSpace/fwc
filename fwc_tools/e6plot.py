import sys, json, os
os.environ['FWC_TRACK2'] = '1'
sys.path.insert(0, '/home/claude/full/fwc_app'); os.chdir('/home/claude/full/fwc_app')
import analyze, gdmacro, gdlevel
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
x0, x1, y0, y1, OFF = map(float, sys.argv[1:6]); out = sys.argv[6]
lv = open('/home/claude/ref/usermacro/sac_end6.txt', encoding='utf-8').read().strip(); m = gdmacro.load('/home/claude/ref/usermacro/dsssss.gdr2')
r = analyze.analyze(lv, m, offset=int(OFF), log=lambda s: None); tr = r.track_full
T = json.load(open('engine/gd_table.json')); h, objs = gdlevel.parse_objects(lv)
fig, ax = plt.subplots(figsize=(16, 16 * (y1 - y0) / (x1 - x0) + 1))
col = {'solid': '#888', 'hazard': 'red', 'saw': 'red', 'slope': '#b8860b'}
for o in objs:
    x, y = float(o['2']), float(o['3'])
    if not (x0 - 60 < x < x1 + 60 and y0 - 60 < y < y1 + 60): continue
    t = (T.get(o['1']) or ['?'])[0]; s = float(o.get('32', 1) or 1)
    if t in col:
        ax.add_patch(plt.Rectangle((x - 15 * s, y - 15 * s), 30 * s, 30 * s, fc=col[t], ec='k', alpha=0.45, lw=0.5))
        ax.text(x, y, o['1'], fontsize=6, ha='center', va='center')
    elif t != '?' or o['1'] in ('3004', '67', '747', '1704', '1594', '1022', '1330', '36', '84', '141', '1333', '10', '11', '12', '13', '47', '111', '660', '745', '1331', '35', '140', '1332', '3005', '99', '101', '286', '287', '200', '201', '202', '203', '1334'):
        ax.plot([x], [y], 'o', ms=9, mfc='none', mec='blue'); ax.text(x, y + 9, o['1'] + ' ' + t, fontsize=7, ha='center', color='blue')
xs = [p[0] for p in tr]; ys = [p[1] for p in tr]
ax.plot(xs, ys, '-', color='green', lw=1.5)
inp = m.held_by_tick(offset=r.offset, merge=True)
for t in range(1, len(tr)):
    if inp[t] and not inp[t - 1] and x0 < xs[t] < x1: ax.plot([xs[t]], [ys[t]], 'v', color='purple', ms=7); ax.text(xs[t], ys[t] - 12, str(t), fontsize=6, color='purple', ha='center')
ax.plot(xs[-1:], ys[-1:], 'x', color='k', ms=14, mew=3)
ax.set_xlim(x0, x1); ax.set_ylim(y0, y1); ax.set_aspect('equal'); ax.grid(alpha=0.3)
ax.set_title('engine path (green), press (purple), death (X)  offset %d' % r.offset)
fig.savefig(out, dpi=70, bbox_inches='tight'); print(r.death_tick, r.death_cause)
