"""Dash(22번) 시작 구간 그림. 쓰는 법: python plot_dash.py 결과.png 한글글꼴.ttc"""
import sys, os, json
OUT, FONT_ = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else None)
sys.argv = [sys.argv[0], '/tmp/_unused.png'] + ([FONT_] if FONT_ else [])
import importlib.util
spec = importlib.util.spec_from_file_location('pr', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'plot_repro_lib.py'))
pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
plt, verify_main, gdmacro, analyze, Line2D, Patch = pr.plt, pr.verify_main, pr.gdmacro, pr.analyze, pr.Line2D, pr.Patch
YS = verify_main.Y_SHIFT
f = pr.D + 'Dash.gdr.json'; m = gdmacro.load(f); lv = pr.LV[22]
r = analyze.analyze(lv, m, log=lambda s: None); tr = r.track_full
fxl = [p for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})]
obs = pr.engine_objects(lv)
presses = [a for a, d, p2, b in m.inputs if d and b == 1 and not p2]
fxd = {p['frame']: p['p1'] for p in fxl}

def panel(ax, x0, x1, title):
    ax.axhspan(-400, 0, facecolor='#e9ecef', zorder=0); ax.axhline(0, color='#343a40', linewidth=1.2, zorder=1)
    pr.draw_objects(ax, obs, x0, x1)
    gx = [p['p1']['x'] for p in fxl if x0 <= p['p1']['x'] <= x1]; gy = [p['p1']['y'] - YS for p in fxl if x0 <= p['p1']['x'] <= x1]
    ex = [t[0] for t in tr if x0 <= t[0] <= x1]; ey = [t[1] for t in tr if x0 <= t[0] <= x1]
    ax.plot(gx, gy, color='#1c7ed6', linewidth=5.5, alpha=0.5, solid_capstyle='round', zorder=7)
    ax.plot(ex, ey, color='#111111', linewidth=1.6, zorder=8)
    px = [(fxd[t]['x'], fxd[t]['y'] - YS) for t in presses if t in fxd and x0 <= fxd[t]['x'] <= x1]
    if px: ax.plot([a for a, b in px], [b for a, b in px], marker='v', color='#0b7285', markersize=9, linestyle='none', zorder=9)
    ys = gy + ey + [0]; ylo, yhi = min(ys) - 50, max(ys) + 110
    if x0 <= tr[-1][0] <= x1:
        ax.plot([tr[-1][0]], [tr[-1][1]], marker='X', color='#e03131', markeredgecolor='white', markersize=17, linestyle='none', zorder=10)
        ax.annotate('엔진이 죽은 자리 (%.2f초)\n%s' % (r.death_tick / 240.0, r.death_cause), xy=(tr[-1][0], tr[-1][1]), xytext=(tr[-1][0] + 40, yhi - 10), fontsize=10, color='#a51111',
                    fontweight='bold', va='top', arrowprops=dict(arrowstyle='->', color='#a51111', lw=1.3), zorder=11)
    ax.set_xlim(x0, x1); ax.set_ylim(ylo, yhi); ax.set_aspect('equal', adjustable='box')
    ax.set_title(title, fontsize=12.5, loc='left', fontweight='bold'); ax.set_xlabel('가로 위치'); ax.set_ylabel('높이')

fig, axes = plt.subplots(2, 1, figsize=(17, 12.5))
panel(axes[0], 150, 760, '위: 시작 직후 확대 (엔진은 %.2f초에 죽음)' % (r.death_tick / 240.0))
panel(axes[1], 150, 1750, '아래: 더 넓게 본 모습 (엔진 선은 죽은 자리에서 끝남. 그 뒤는 실제 게임 기록만)')
handles = [Line2D([0], [0], color='#1c7ed6', lw=6, alpha=0.5, label='실제 게임에서 지나간 길'), Line2D([0], [0], color='#111111', lw=1.6, label='엔진이 계산한 길'),
           Line2D([0], [0], marker='v', color='#0b7285', markersize=9, linestyle='none', label='매크로가 누른 순간'),
           Line2D([0], [0], marker='X', color='#e03131', markeredgecolor='white', markersize=13, linestyle='none', label='엔진이 죽은 자리'),
           Patch(facecolor=pr.C_SOLID, label='블록'), Patch(facecolor=pr.C_HAZ, label='가시·톱니'), Patch(facecolor=pr.C_SLOPE, hatch='///', label='경사'),
           Line2D([0], [0], marker='o', markerfacecolor='none', markeredgecolor='#e8590c', markeredgewidth=2.4, markersize=11, linestyle='none', label='빨간 오브'),
           Line2D([0], [0], marker='o', markerfacecolor='none', markeredgecolor='#f2b705', markeredgewidth=2.4, markersize=11, linestyle='none', label='노란 오브'),
           Line2D([0], [0], marker='o', markerfacecolor='none', markeredgecolor='#1971c2', markeredgewidth=2.4, markersize=11, linestyle='none', label='파란 오브'),
           Line2D([0], [0], marker='o', markerfacecolor='none', markeredgecolor='#2f9e44', markeredgewidth=2.4, markersize=11, linestyle='none', label='초록 오브'),
           Patch(facecolor='#e8590c', label='빨간 패드'), Patch(facecolor='#f2b705', label='노란 패드'), Patch(facecolor=pr.C_MODE, alpha=0.55, label='모드 포탈'), Patch(facecolor=pr.C_OTHER, alpha=0.45, label='중력·크기·속도 포탈')]
fig.legend(handles=handles, loc='upper center', ncol=8, fontsize=10.5, frameon=True, bbox_to_anchor=(0.5, 0.965))
fig.suptitle('Dash (22번) 시작 구간: 엔진이 아는 물체만 그림 (장식, 엔진이 모르는 물체, 트리거로 옮겨 오는 물체는 안 보임)', fontsize=14.5, y=0.995, fontweight='bold')
fig.tight_layout(rect=(0, 0.01, 1, 0.915)); fig.savefig(OUT, dpi=105)
print('saved', OUT, '| died', r.died, r.death_cause, '| presses', presses[:8])
