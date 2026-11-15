"""막힌 곳 그림: 실패한 레벨마다, 엔진이 게임과 어긋나기 시작하는 곳부터 죽는 곳까지를 그림.
쓰는 법: python plot_stuck.py 결과.png 한글글꼴.ttc"""
import sys, os, json
sys.argv_keep = sys.argv[:]
OUT, FONT_ = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else None)
sys.argv = [sys.argv[0], '/tmp/_unused.png'] + ([FONT_] if FONT_ else [])
import importlib.util
spec = importlib.util.spec_from_file_location('pr', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'plot_repro_lib.py'))
pr = importlib.util.module_from_spec(spec); spec.loader.exec_module(pr)
plt, verify_main, gdmacro, analyze, Line2D, Patch = pr.plt, pr.verify_main, pr.gdmacro, pr.analyze, pr.Line2D, pr.Patch
YS = verify_main.Y_SHIFT

def first_div(tr, fx, thr=10.0):
    def d(t, sh):
        g = fx.get(t - sh); return None if g is None or t >= len(tr) else tr[t][1] + YS - g['y']
    def med(sh):
        v = sorted(abs(x) for x in (d(t, sh) for t in range(0, min(2000, len(tr)))) if x is not None)
        return v[len(v) // 2] if v else 1e9
    sh = min(range(-2, 3), key=med)
    return next((t for t in range(len(tr)) if d(t, sh) is not None and abs(d(t, sh)) > thr), None), sh

def panel(ax, name, title):
    f = pr.D + name + '.gdr.json'
    m = gdmacro.load(f); lv = pr.LV[int(m.level_id)]
    fxl = [p for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})]
    fx = {p['frame']: p['p1'] for p in fxl}
    try:
        r = analyze.analyze(lv, m, log=lambda s: None); tr = r.track_full; err = None
    except Exception as e:
        r = None; tr = []; err = str(e)
    obs = pr.engine_objects(lv)
    if r is not None and getattr(r, 'died', False):
        fd, sh = first_div(tr, fx)
        xd = tr[fd][0] if fd is not None else r.death_x - 300
        x0 = max(0, min(xd, r.death_x - 250) - 220); x1 = r.death_x + 260
        if x1 - x0 > 1500: x0 = x1 - 1500
    else:
        x0, x1, fd = 0, 1300, None
    ax.axhspan(-400, 0, facecolor='#e9ecef', zorder=0); ax.axhline(0, color='#343a40', linewidth=1.2, zorder=1)
    pr.draw_objects(ax, obs, x0, x1)
    gx = [p['p1']['x'] for p in fxl if x0 <= p['p1']['x'] <= x1]; gy = [p['p1']['y'] - YS for p in fxl if x0 <= p['p1']['x'] <= x1]
    ex = [t[0] for t in tr if x0 <= t[0] <= x1]; ey = [t[1] for t in tr if x0 <= t[0] <= x1]
    ax.plot(gx, gy, color='#1c7ed6', linewidth=5.5, alpha=0.5, solid_capstyle='round', zorder=7)
    if ex: ax.plot(ex, ey, color='#111111', linewidth=1.5, zorder=8)
    ys = gy + ey + [0]; ylo, yhi = min(ys) - 50, max(ys) + 120
    if r is not None and getattr(r, 'died', False):
        ax.plot([tr[-1][0]], [tr[-1][1]], marker='X', color='#e03131', markeredgecolor='white', markersize=17, linestyle='none', zorder=9)
        ax.annotate('엔진이 죽은 자리 (%.2f초)\n%s' % (r.death_tick / 240.0, r.death_cause.split(' (')[0]), xy=(tr[-1][0], tr[-1][1]), xytext=(tr[-1][0] - 40, yhi - 12),
                    fontsize=9.5, color='#a51111', fontweight='bold', ha='right', va='top', arrowprops=dict(arrowstyle='->', color='#a51111', lw=1.3), zorder=10)
        if fd is not None and x0 <= tr[fd][0] <= x1:
            ax.axvline(tr[fd][0], color='#f08c00', linestyle='--', linewidth=1.8, zorder=6)
            ax.text(tr[fd][0] + 8, ylo + 10, '여기부터 어긋남\n(%.2f초)' % (fd / 240.0), color='#c46a00', fontsize=9.5, fontweight='bold', va='bottom', zorder=10)
    if err:
        ax.text((x0 + x1) / 2, (ylo + yhi) / 2, '엔진 계산 불가\n"%s"\n(검은 선 없음. 파란 띠는 실제 게임 기록)' % err[:40], fontsize=12, color='#a51111', fontweight='bold', ha='center', va='center',
                bbox=dict(facecolor='white', edgecolor='#a51111', alpha=0.9), zorder=11)
    ax.set_xlim(x0, x1); ax.set_ylim(ylo, yhi); ax.set_aspect('equal', adjustable='box')
    ax.set_title(title, fontsize=12, loc='left', fontweight='bold'); ax.set_xlabel('가로 위치'); ax.set_ylabel('높이')

fig, axes = plt.subplots(3, 2, figsize=(21, 15.5))
items = [('Clubstep', '① Clubstep (14번)'), ('Deadlocked', '② Deadlocked (20번)'), ('Hexagon Force', '③ Hexagon Force (16번): 듀얼 구간'),
         ('Dash', '④ Dash (22번): 스파이더 구간 (기둥 발판은 고쳐서 통과)'), ('Fingerdash', '⑤ Fingerdash (21번): 시작 구간')]
axes.flat[5].axis('off')
for ax, (n, t) in zip(axes.flat, items): panel(ax, n, t)
handles = [Line2D([0], [0], color='#1c7ed6', lw=6, alpha=0.5, label='실제 게임에서 지나간 길'), Line2D([0], [0], color='#111111', lw=1.6, label='엔진이 계산한 길'),
           Line2D([0], [0], color='#f08c00', lw=1.8, linestyle='--', label='어긋나기 시작한 곳'),
           Line2D([0], [0], marker='X', color='#e03131', markeredgecolor='white', markersize=13, linestyle='none', label='엔진이 죽은 자리'),
           Patch(facecolor=pr.C_SOLID, label='블록'), Patch(facecolor=pr.C_HAZ, label='가시·톱니'), Patch(facecolor=pr.C_SLOPE, hatch='///', label='경사'),
           Patch(facecolor=pr.C_JUMP, label='점프 패드·오브 (실제 색은 게임 색)'), Patch(facecolor=pr.C_MODE, alpha=0.55, label='모드 포탈'), Patch(facecolor=pr.C_OTHER, alpha=0.45, label='중력·크기·속도 포탈')]
fig.legend(handles=handles, loc='upper center', ncol=5, fontsize=11, frameon=True, bbox_to_anchor=(0.5, 0.975))
fig.suptitle('지금 막힌 곳 5군데 (트리거로 옮겨 오는 물체는 그림에 안 보임)', fontsize=15, y=0.995, fontweight='bold')
fig.tight_layout(rect=(0, 0.02, 1, 0.93)); fig.savefig(OUT, dpi=100)
print('saved', OUT)
