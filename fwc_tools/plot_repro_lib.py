"""엔진 재현 경로와 실제 게임 기록(frameFixes)을 레벨 위에 겹쳐 그림.
레벨 물체는 엔진이 실제로 쓰는 판정 상자를 종류별 색으로 그림 (장식은 안 그림).
쓰는 법: python plot_repro.py 결과.png 한글글꼴.ttc"""
import sys, os, json
APP = '/home/claude/full/fwc_app'
sys.path.insert(0, APP)
import verify_main, gdmacro, gdlevel, analyze
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import Rectangle, Circle, Patch
from matplotlib.lines import Line2D

FONT = sys.argv[2] if len(sys.argv) > 2 else None
if FONT:
    font_manager.fontManager.addfont(FONT); plt.rcParams['font.family'] = font_manager.FontProperties(fname=FONT).get_name()
plt.rcParams['axes.unicode_minus'] = False
LV = verify_main.load_main_levels('/home/claude/ref/main/LevelData_v2.plist')
D = '/home/claude/ref/gdmacros/'
P = analyze._load_engine()[0]
MODE_NAME = {12: '큐브', 13: '비행(배)', 47: '공', 111: 'UFO', 660: '웨이브', 745: '로봇', 1331: '스파이더', 1933: '스윙', 286: '듀얼', 287: '듀얼 끝'}
OTHER_NAME = {10: '중력↓', 11: '중력↑', 99: '보통 크기', 101: '미니', 200: '속도 0.5', 201: '속도 1', 202: '속도 2', 203: '속도 3', 1334: '속도 4', 747: '순간이동'}
C_SOLID, C_HAZ, C_SLOPE, C_JUMP, C_MODE, C_OTHER = '#5b6472', '#e03131', '#8d99ae', '#f2b705', '#2f9e44', '#7048e8'


# 패드·오브는 게임 색 그대로 (노랑=점프, 파랑=중력 뒤집기, 분홍=낮은 점프, 빨강=높은 점프, 초록=뒤집으며 점프, 검정=아래로, 청록·보라=대시)
JUMP_COLOR = {'yellow': '#f2b705', 'blue': '#1971c2', 'pink': '#e64980', 'red': '#e8590c', 'green': '#2f9e44', 'black': '#212529', 'dash': '#12b886', 'gravity_dash': '#7048e8', 'spider': '#7950f2'}


def engine_objects(level_string):
    """엔진(analyze와 같은 방식)이 만든 레벨 물체 목록 (중복 제거)"""
    header, objs = gdlevel.parse_objects(level_string)
    P.apply_header(level_string)
    cwd = os.getcwd(); os.chdir(analyze.ENGINE)
    try: L = P.Level(objs)
    finally: os.chdir(cwd)
    seen = {}
    for cell in L.cells.values():
        for o in cell: seen[o.uid] = o
    return list(seen.values())


def draw_objects(ax, obs, x0, x1):
    for o in obs:
        if o.maxx < x0 - 40 or o.minx > x1 + 40 or o.miny > 900: continue
        w, h = o.maxx - o.minx, o.maxy - o.miny
        if o.kind == 'solid': ax.add_patch(Rectangle((o.minx, o.miny), w, h, facecolor=C_SOLID, edgecolor='#3b424d', linewidth=0.4, zorder=2))
        elif o.kind == 'hazard': ax.add_patch(Rectangle((o.minx, o.miny), w, h, facecolor=C_HAZ, edgecolor='#a51111', linewidth=0.4, zorder=3))
        elif o.kind == 'saw': ax.add_patch(Circle((o.x, o.y), o.radius, facecolor=C_HAZ, edgecolor='#a51111', linewidth=0.4, zorder=3))
        elif o.kind == 'slope': ax.add_patch(Rectangle((o.minx, o.miny), w, h, facecolor=C_SLOPE, edgecolor='#3b424d', linewidth=0.4, hatch='///', zorder=2))
        elif o.kind == 'pad': ax.add_patch(Rectangle((o.minx, o.miny), w, max(h, 6), facecolor=JUMP_COLOR.get(o.arg, C_JUMP), edgecolor='#333333', linewidth=0.5, zorder=4))
        elif o.kind == 'orb': ax.add_patch(Circle((o.x, o.y), 15, facecolor='none', edgecolor=JUMP_COLOR.get(o.arg, C_JUMP), linewidth=2.4, zorder=4))
        elif o.kind == 'mode':
            ax.add_patch(Rectangle((o.minx, o.miny), w, h, facecolor=C_MODE, edgecolor='#1b5e28', alpha=0.55, linewidth=0.8, zorder=1))
            ax.text(o.x, o.maxy + 6, MODE_NAME.get(o.arg, '모드'), color='#1b5e28', fontsize=9, ha='center', va='bottom', zorder=6, fontweight='bold')
        elif o.kind == 'portal':
            ax.add_patch(Rectangle((o.minx, o.miny), w, h, facecolor=C_OTHER, edgecolor='#3b2a8a', alpha=0.45, linewidth=0.8, zorder=1))
            ax.text(o.x, o.maxy + 6, OTHER_NAME.get(o.arg, '포탈'), color='#3b2a8a', fontsize=8, ha='center', va='bottom', zorder=6)


def panel(ax, name, x0, x1, title, note=None):
    f = D + name + '.gdr.json'
    m = gdmacro.load(f); lv = LV[int(m.level_id)]
    r = analyze.analyze(lv, m, log=lambda s: None)
    fx = [p for p in json.load(open(f))['frameFixes'] if 'x' in p.get('p1', {})]
    obs = engine_objects(lv)
    ax.axhspan(-200, 0, facecolor='#e9ecef', zorder=0); ax.axhline(0, color='#343a40', linewidth=1.2, zorder=1)
    draw_objects(ax, obs, x0, x1)
    gx = [p['p1']['x'] for p in fx if x0 <= p['p1']['x'] <= x1]; gy = [p['p1']['y'] - verify_main.Y_SHIFT for p in fx if x0 <= p['p1']['x'] <= x1]
    ex = [t[0] for t in r.track if x0 <= t[0] <= x1]; ey = [t[1] for t in r.track if x0 <= t[0] <= x1]
    ax.plot(gx, gy, color='#1c7ed6', linewidth=5.5, alpha=0.5, solid_capstyle='round', zorder=7)
    ax.plot(ex, ey, color='#111111', linewidth=1.4, zorder=8)
    ys = gy + ey + [0]
    ylo, yhi = min(ys) - 45, max(ys) + 110
    if getattr(r, 'died', False) and x0 <= r.death_x <= x1:
        near = min(fx, key=lambda p: abs(p['p1']['x'] - r.death_x)); dy = near['p1']['y'] - verify_main.Y_SHIFT
        ax.plot([r.death_x], [dy], marker='X', color='#e03131', markeredgecolor='white', markersize=17, linestyle='none', zorder=9)
        if note: ax.annotate(note, xy=(r.death_x, dy), xytext=(r.death_x + 90, yhi - 28), fontsize=10, color='#a51111', fontweight='bold',
                             arrowprops=dict(arrowstyle='->', color='#a51111', lw=1.4), zorder=10, va='top')
    ax.set_xlim(x0, x1); ax.set_ylim(ylo, yhi); ax.set_aspect('equal', adjustable='box')
    ax.text(x0 + 12, ylo + 8, '진행 방향 →', fontsize=10, color='#495057', va='bottom', zorder=10)
    ax.set_title(title, fontsize=12.5, loc='left', fontweight='bold'); ax.set_xlabel('가로 위치 (게임 단위, 블록 1칸 = 30)'); ax.set_ylabel('높이')
    return r


