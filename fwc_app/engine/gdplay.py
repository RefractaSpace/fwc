"""gdplay.py — GD 2.2 모든 모드 물리 (큐브·쉽·볼·UFO·웨이브·로봇·스윙, 스파이더는 근사) + 오브·패드 (내가 짠 코드)
gdcube.py를 넓힌 것.

규칙·상수 출처: GD-Remake(gd/constants.py, gd/player.py, gd/sim.py)의 설명과 상수를 읽고 옮김.
그쪽 출처는 OSGD(2.2081 함수 재구성), camila314/gdp(2.2 디컴파일), gd-sim(게임과 비교 검증된 시뮬레이터).
GD-Remake 코드는 실행하지 않았음.

다루는 것
  - 큐브(일반·미니, 중력 반전), 블록(착지·머리·벽 충돌), 가시·톱니, 경사(근사), 땅(y=0)
  - H 블록 1859: 겹쳐 있으면 큐브가 블록 아랫면에 머리를 박아도 안 죽고 튕김
  - 오브: 노랑 36, 분홍 141, 빨강 1333, 파랑 84, 초록 1022, 검정 1330, 대시 1704, 중력 대시 1751
  - 패드: 노랑 35, 분홍 140, 빨강 1332, 파랑 67
  - 포탈: 속도 200~203·1334, 크기 99·101, 중력 10·11
  - 텔레포트 포탈 747: 출구 y = 포탈 y + 키 54 로 이동 (위치 규칙은 추정)
  - 모드 포탈: 모드 바꾸기 + 쉽·UFO·웨이브·볼·스윙은 포탈 y로 바닥·천장 다시 맞춤
  - D 블록 1755: 웨이브가 겹쳐 있으면 블록에 닿아도 안 죽고 비행 모드처럼 튕김 (동작은 추정)
다루지 않는 것 (추정·생략): 회전한 히트박스(아이콘 회전), 트리거, 듀얼, 스파이더 순간이동(중력 반전으로만 근사) (계단 스냅 x 보정은 넣음)
단위: 위치 = 유닛(1블록 30), 속도 = 게임 내부 단위(1 = 틱당 0.225 유닛)
"""
import array, json, math

_F = array.array("f", [0.0])
def f32(x):
    _F[0] = x
    return _F[0]

def round3(v):
    """setYVelocity: 소수 셋째 자리 반올림 (C round)"""
    whole = int(v)
    if whole != v:
        frac = (v - whole) * 1000.0
        frac = math.floor(frac + 0.5) if frac >= 0 else -math.floor(-frac + 0.5)
        v = whole + frac / 1000.0
    return v

# ---------------------------------------------------------------- 상수
DT = 0.25
DT_SCALED = f32(DT * 0.9)
SPEED_TABLE = (   # 속도, 점프 초기속도, 중력, 배율
    (f32(0.7), f32(10.620032), f32(0.940199), f32(5.980002)),
    (f32(0.9), f32(11.180032), f32(0.958199), f32(5.770002)),
    (f32(1.1), f32(11.420032), f32(0.957199), f32(5.870002)),
    (f32(1.3), f32(11.230032), f32(0.961199), f32(6.000002)),
    (f32(1.6), f32(11.230032), f32(0.961199), f32(6.000002)),
)
SPEED_ID = {200: 0, 201: 1, 202: 2, 203: 3, 1334: 4}
MINI_SCALE = 0.6
MINI_JUMP_MOD = 0.8
CUBE_TERMINAL = 15.0
GRAVITY_FLIP_KEEP = 0.5
PLAYER_SIZE = 30.0
INNER = 0.3
SNAP_THRESHOLD = 10.0
FLIP_GRACE_TICKS = int(round(0.1 * 240))
SNAP_TABLE = {0: (90.0, 90.0, 120.0, 60.0, 1.0), 1: (120.0, 90.0, 150.0, 90.0, 1.0),
              2: (150.0, 90.0, 195.0, 120.0, 2.0), 3: (180.0, 90.0, 225.0, 135.0, 2.0),
              4: (120.0, 120.0, 150.0, 90.0, 1.0)}
ORB_PINK, ORB_RED, ORB_BLUE, ORB_BLACK = 0.72, 1.38, 0.8, 15.0
PAD_BASE = 16.0
PAD_MOD = {"yellow": 1.0, "pink": 0.65, "red": 1.25}
PAD_BLUE = 0.8
DASH_MAX_ANGLE = 70.0
SLOPE_VEL_FACTOR, SLOPE_VEL_CAP = 1.12, 1.54
SLOPE_FLY_BALL_MOD = 0.75
GRAVITY_FLYING = 0.958199
MINI_FLY_MOD = 0.85
GRAV_SCALE_BALL, GRAV_SCALE_ROBOT = 0.6, 0.9
BALL_SWITCH_KEEP = f32(0.6)
ROBOT_JUMP_MOD, ROBOT_BOOST_MAX, ROBOT_BOOST_RATE = 0.5, 1.5, 0.1
FLY_MAX_UP, FLY_MAX_DOWN = 8.0, 6.4
SHIP_THRUST, SHIP_FALL, SHIP_FALL_RISING = -1.0, 0.8, 1.2
SHIP_BOOST_HOLD_FALLING, SHIP_BOOST = 0.5, 0.4
UFO_JUMP, UFO_JUMP_MINI, UFO_GRAV_FALL, UFO_GRAV_RISE = 7.0, 8.0, 0.8, 1.2
SWING_GRAV, SWING_GRAV_MINI, SWING_FLIP_KEEP = 0.4, 0.6, 0.8
MODE_SWITCH_KEEP = 0.5
WAVE_SIZE = 10.0
SPIDER27 = True
import os as _os
SPIDER27_X = float(_os.environ.get("SPIDER27_X", "-1e18"))   # 레벨 전체 27 (파트 3·4도 27로 다시 찾음). 옛 경로 확인용: SPIDER27_X=31668.8
SPIDER_SIZE = 27.0   # 스파이더 히트박스 27 (GD 디컴파일 OSGD toggleSpiderMode, GD-Remake 상수에서 확인. 쇼케이스 79.5초 폭 26 가시 틈 통과)
SNAP_THRESHOLD_FLY = 6.0
ORB_PINK_M = {"ship": 0.37, "ufo": 0.42, "ball": 0.77}
ORB_RED_M = {"ship": 1.0, "ship_mini": 1.4, "ufo": 1.02, "ufo_mini": 1.36, "ball": 1.34, "robot": 1.28, "spider": 1.34}
ORB_GREEN_SHIP, ORB_ROBOT, ORB_BALL_SPIDER_KEEP, ORB_SWING_KEEP = 0.7, 0.9, 0.7, 0.6
ORB_BLACK_FLY, ORB_BLACK_UFO_MOD, ORB_BLACK_SPIDER_MOD = 14.0, 0.8, 1.1
PAD_MOD_M = {"pink": {"ship": 0.35, "ufo": 0.4, "ball": 0.7},
             "red": {"ship": 0.63, "ship_mini": 0.95, "ufo": 0.6, "ufo_mini": 0.98}}
PAD_BALL_KEEP = 0.6
FLYING = {"ship", "ufo", "wave", "swing"}
MODE_OF = {12: "cube", 13: "ship", 47: "ball", 111: "ufo", 660: "wave", 745: "robot", 1331: "spider", 1933: "swing"}
BOUNDS_H = {"ship": 300.0, "ufo": 300.0, "wave": 300.0, "ball": 240.0, "spider": 240.0, "swing": 300.0}

def pick(table, mode, mini, default):
    if mini and (mode + "_mini") in table: return table[mode + "_mini"]
    return table.get(mode, default)
SLOPE_JUMP_SHARE, SLOPE_JUMP_CAP = 0.25, 1.4
SLOPE_HAZARD_OFFSET = 4.0
# 대시 오브 기울기 직접 지정 {(x, y): 기울기(위로 +)} — 쇼케이스 영상에서 잰 값
#   (10289.5, 1125.7): 쇼케이스 31.55~31.75초 프레임에서 잰 기울기 약 0.35~0.6 (평균 0.5 근처) → 0.55 사용 (계산값 tan56°=1.48과 다름, 추정)
DASH_OVERRIDE = {}
# 오브 세기 배율 직접 지정 {(x, y): 배율} — 쇼케이스 영상에서 잰 값 (게임 규칙 미확인)
ORB_OVERRIDE = {}   # 경사 미끄러짐을 넣은 뒤로는 계산값(tan56°) 그대로 사용

ORBS = {36: "yellow", 141: "pink", 1333: "red", 84: "blue", 1022: "green", 1330: "black",
        1704: "dash", 1751: "gravity_dash", 3004: "spider"}
PADS = {35: ("yellow", 25, 4), 140: ("pink", 25, 5), 1332: ("red", 29, 7), 67: ("blue", 25, 6),
        3005: ("spider", 25, 6)}     # 스파이더 패드: 히트박스는 파랑 패드와 같다고 가정 (GD-Remake도 추정)
MODE_PORTALS = {12, 13, 47, 111, 660, 745, 1331, 1933, 286, 287}
# 부서지는 블록: 위에 올라설 수는 있지만(단단함), 박으면 죽는 대신 부서져서 지나감. 부서진 뒤에는 없는 물체.
# 근거: Electroman Adventures 매크로(2.206 실제 게임 위치 기록)가 x 1700 근처 143번 위에 서고, x 2055의 143번은 옆에서 뚫고 지나감
BREAKABLE = {143}
import os as _os0
SOLID_NO_OBB = True       # 블록(솔리드)은 비스듬히 돌려도 판정 상자가 안 돌아감 (2.206 디컴파일 GameObject::canRotateFree·updateIsOriented 기준). False = 예전 방식(기운 블록 = 닿으면 죽음)
BALL_ORB_EATS_PRESS = set(_os0.environ.get("BALL_ORB_EATS", "yellow,pink,red,blue,green,black").split(","))   # 공 모드에서 누름을 다 써 버리는 오브 종류 (기본 전부. 단 공중에서 쓴 오브만 — 땅에서 누르며 쓴 오브는 공 뒤집기도 같이 일어남: sac 63.48초. 시험용으로 환경변수로 바꿀 수 있음)
DUAL_PLAIN_PORTAL_Y = _os0.environ.get("DUAL_PLAIN", "1") != "0"   # 듀얼 바닥·천장 기준: 크기·회전 안 바꾼 듀얼 포탈은 포탈 높이 (2.206 디컴파일 updateDualGround + Hexagon Force 실제 기록). 크기·회전 바꾼 포탈은 예전대로 플레이어 높이 (쇼케이스 맞춤, 추정)
BALL_ONE_FLIP = True      # 공: 뒤집은 누름을 떼기 전에는 다시 안 뒤집힘 (False = 예전 방식: 누르고 있으면 닿을 때마다 뒤집힘)
PORTAL_BOX = {10: (25, 75), 11: (25, 75), 99: (31, 90), 101: (31, 90), 200: (35, 44), 201: (33, 56),
              202: (51, 56), 203: (65, 56), 1334: (69, 56)}

# 레벨 설정 (헤더 kA32 = Fix Gravity Bug, kA39 = Fix Radius Collision). apply_header()로 채움
SETTINGS = {"fix_gravity_bug": False, "fix_radius": False}

def apply_header(level_text):
    f = level_text.split(';')[0].split(','); h = dict(zip(f[0::2], f[1::2]))
    SETTINGS["fix_gravity_bug"] = h.get('kA32', '0') not in ('0', '')
    SETTINGS["fix_radius"] = h.get('kA39', '0') not in ('0', '')
    return dict(SETTINGS)

def num(o, k, d=0.0):
    try: return float(o.get(k, d))
    except ValueError: return d

def rects_touch(a0, a1, a2, a3, b0, b1, b2, b3):
    return not (a1 < b0 or b1 < a0 or a3 < b2 or b3 < a2)

def snap360(a):
    a = math.fmod(a, 360.0)
    if a > 180.0: a -= 360.0
    elif a <= -180.0: a += 360.0
    return a


# ---------------------------------------------------------------- 오브젝트
class Obj:
    __slots__ = ("uid", "id", "kind", "arg", "x", "y", "w", "h", "minx", "maxx", "miny", "maxy",
                 "rot", "flip_x", "upside", "orient", "hazard_slope", "radius", "corners", "tp_y", "groups")

def shifted(o, ox, oy):
    """이동 트리거로 옮겨진 복사본 (uid 같음 → 쓴 오브·포탈 기록 그대로)"""
    c = Obj.__new__(Obj)
    for k in Obj.__slots__:
        if hasattr(o, k): setattr(c, k, getattr(o, k))
    c.x = o.x + ox; c.y = o.y + oy; c.minx = o.minx + ox; c.maxx = o.maxx + ox; c.miny = o.miny + oy; c.maxy = o.maxy + oy
    if o.corners: c.corners = [(a + ox, b + oy) for a, b in o.corners]
    if o.id == 747: c.tp_y = o.tp_y + oy
    return c

import os as _os2, trigmove as _TM
TRIGGER_MOVES = _os2.environ.get("MOVES", "1") != "0"   # 이동 트리거(901)·중력 트리거(2066) 계산 (끄려면 MOVES=0)
# STRIG=1: 트리거를 "틱마다 상태로" 계산 (C++ 엔진에서만. 생성·닿기·충돌·개수·플레이어 Y 따라가기 등 전부)
#          파이썬 Level은 트리거 목록·움직이는 그룹만 정리해서 내보냄 (export_lib) — 파이썬 엔진 자체 재생은 이 모드를 모름
STATE_TRIG = _os2.environ.get("STRIG", "0") == "1"
# 상태 트리거에서 다루는 것 (ID: 이름). 키 뜻은 gddocs·2.2081 디컴파일(AI 복원본)·GD-Remake 설명 기준, 일부 추정
STRIG_IDS = {901: "이동", 1049: "토글", 1268: "생성", 1346: "회전", 1347: "따라가기", 1814: "플레이어 Y 따라가기",
             1616: "멈춤", 1595: "터치", 1611: "개수", 1811: "즉시 개수", 1817: "줍기", 1815: "충돌", 1912: "랜덤",
             2066: "중력", 3022: "텔레포트", 1594: "토글 오브", 1816: "충돌 블록",
             1275: "아이템", 1587: "아이템", 1589: "아이템", 1598: "아이템", 3601: "아이템", 1614: "아이템",
             # 2단계 (2.2)
             2067: "크기", 3619: "아이템 편집", 3620: "아이템 비교", 3609: "즉시 충돌", 3640: "충돌 상태 블록", 3643: "터치 토글 블록",
             2068: "고급 랜덤", 3618: "리셋", 3027: "텔레포트 오브", 3614: "타이머", 3615: "시간 이벤트", 3617: "타이머 조작", 3604: "이벤트",
             # 3단계
             1932: "플레이어 조작", 1935: "시간 왜곡", 3607: "순서", 3655: "오브젝트 조작",
             3016: "고급 따라가기", 3660: "고급 따라가기 편집", 3661: "따라가기 대상 바꾸기", 3033: "키프레임 애니메이션", 3032: "키프레임 점",
             2069: "힘 블록", 3645: "힘 원",
             3006: "영역 이동", 3007: "영역 회전", 3008: "영역 크기", 3011: "영역 이동 편집", 3012: "영역 회전 편집", 3013: "영역 크기 편집", 3024: "영역 멈춤",
             1917: "방향 반대", 2900: "게임 회전", 3017: "들어올 때 이동", 3018: "들어올 때 회전", 3019: "들어올 때 크기", 3023: "들어올 때 멈춤"}
STRIG_ITEMS = {1275, 1587, 1589, 1598, 3601, 1614}
# 그룹을 움직이거나 켜고 끄는 것 → 그 대상 그룹(키 51)의 물체는 '움직이는 물체'로 따로 둠
STRIG_DYN = {901, 1346, 1347, 1814, 1049, 1595, 1611, 1811, 1815, 1594, 2067, 3643, 3016, 3033, 3006, 3007, 3008, 3017, 3018, 3019} | STRIG_ITEMS
# 목록 값 키 (점으로 나뉜 숫자): 이벤트 목록, 고급 랜덤 확률 목록
STRIG_LISTS = {430, 152, 435}
# 물리에 영향이 있을 수 있는데 아직 안 되는 것 (분석 결과에 경고)
STRIG_TODO = {}   # 물리에 영향이 있는 트리거는 모두 계산함 (일부는 추정 — 아래 STRIG_GUESS)
STRIG_GUESS = {3017: "들어올 때 효과(화면 끝 위치 추정)", 3018: "들어올 때 효과(화면 끝 위치 추정)", 3019: "들어올 때 효과(화면 끝 위치 추정)",
               2069: "힘 블록(힘 크기 단위 추정)", 3645: "힘 원(힘 크기 단위 추정)", 2900: "게임 회전(방향 키 뜻 추정)", 1935: "시간 왜곡(추정)",
               1912: "랜덤(게임 난수 못 맞춤)", 2068: "고급 랜덤(게임 난수 못 맞춤)", 3016: "고급 따라가기(그룹 단위·난수 0)"}
# 이벤트 트리거에서 계산하는 이벤트 번호 (디컴파일 GJGameEvent): 나머지는 경고
STRIG_EVENTS = {4, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
                34, 35, 36, 37, 38, 39, 41, 42, 43, 45, 46, 47, 48, 49, 51, 52, 55, 56, 57, 58, 59, 63, 69, 70}

ROT_FROM_131 = True
SPORB_HOLD = True
SPIDER_ENTRY_HOLD = False   # 누른 채로 스파이더가 되면 새로 누르기 전엔 순간이동 안 함 (추정, 파트 3에서 켬)
TWIN_SPAWN_PUSH = False   # 누른 채로 듀얼 둘째가 생기면 둘째는 새로 누른 것으로 (추정, 파트 3에서 켬)
DUAL_BAND_OVERRIDE = [   # (x0, x1, 바닥, 천장): 듀얼 바닥·천장을 쇼케이스에서 잰 값으로 (추정)
    # 파트 6 듀얼 웨이브 x 31870~32490: 쇼케이스 70.5초 — 아래 웨이브는 D블록 윗면(1228)에, 위 웨이브는 D블록 아랫면(1528)에 붙어 감.
    # 공식(1200~1500)이면 위 웨이브가 1528까지 못 올라감 → 블록이 막도록 바닥·천장을 블록 바깥으로
    (31860.0, 32490.0, 1170.0, 1590.0),
]
SPIDER_RAY_SOLID = True   # 스파이더 순간이동 길 중간의 가시는 무시, 블럭·패드에서만 멈춤 (쇼케이스 79.4초: 폭 26 가시 틈을 폭 30 스파이더가 위로 지나감, 추정)
TOGGLE_ORBS = True   # 토글 오브(1594) 끄기: 겹친 오브를 쓰면 대상 그룹 가시·톱니 없앰 (파트 6, 추정)
DUAL_EXIT_SWAP = True   # 둘째가 듀얼 끄기 포탈(287)에 닿으면 둘째가 남고 첫째가 사라짐 (쇼케이스 파트4 끝: 아래 볼이 계속 감, 추정)
DUAL_OFF_FIRST = True   # 듀얼 끄기 포탈은 둘째를 움직이기 전에 처리 (쇼케이스 46.2초, 추정)
DUAL_BOUNDS_PLAYER = False   # 듀얼 바닥·천장 기준: 플레이어 y(True) / 듀얼 포탈 y(False) (실험)
MINI_WAVE_VEL2 = False   # 미니 웨이브 세로 속도 2배 저장 (실험: 쇼케이스와 안 맞아서 끔)
TP_BEFORE_UPDATE = False   # 텔레포트를 틱 처음(움직이기 전)에 (실험)
TP_PORTAL_DELAY = 0   # 텔레포트 트리거 뒤 이 틱 수 동안 포탈(모드·듀얼 등)을 안 받음 (실험)
PORTAL_SWAP_FIX = _os.environ.get('PSF', '1') != '0'   # 비스듬한 포탈: 가로·세로 바꾸기는 90도 단위일 때만 (돌린 상자가 회전을 다 처리)
SHIP_ORB_CLICK = _os.environ.get('SHIP_ORB', '1') != '0'   # 비행기 오브는 새로 눌러야 작동
HAZ_SHRINK = float(_os.environ.get('HAZ_SHRINK', '0'))   # 시험용: 가시 판정을 사방 이만큼 줄임
WAVE_SLOPE_SLIDE = _os.environ.get('WAVE_SLOPE', '1') != '0'   # 웨이브는 경사에 닿아도 죽지 않고 미끄러짐 (sacrilegium end 6 실제 매크로 17.7초·18.5초)
ORB_MIN36 = _os.environ.get('ORB_MIN36', '1') != '0'   # 줄인 오브 판정 크기 유지
ORB_BOX = float(_os.environ.get('ORB_BOX', '36')); ORB_NOSCALE = _os.environ.get('ORB_NOSCALE', '0') == '1'   # 오브 판정 크기 (시험용)
PAD_ONE_PER_TICK = _os.environ.get('PAD_ONE', '1') != '0'   # 패드는 한 틱에 하나만 작동
PAD_OBB = _os.environ.get('PAD_OBB', '1') != '0'   # 비스듬히 돌린 패드도 돌린 상자로 판정 (Deadlocked 30.2초 실제 기록: 29도 돌린 보이지 않는 패드)
TP_OBB = _os.environ.get('TP_OBB', '1') != '0'   # 비스듬한 텔레포트 포탈도 돌린 상자로 판정 (끄려면 TP_OBB=0)
PORTAL_OBB = 1     # 1: 돌린 상자, -1: 반대 방향(실험), 0: 안 돌림
PORTAL_REVERSE = False   # 같은 틱에 여러 포탈: 뒤에 놓인 것부터 처리 (추정 실험용)
SHARED_USED = False      # 듀얼: 한 플레이어가 쓴 포탈은 다른 플레이어도 못 씀 (추정 실험용)
# PURE=1: 쇼케이스에 맞추려고 넣은 추정 규칙을 모두 끔 (사용자 요청: 2.2081 소스 기준만)
PURE = _os.environ.get("PURE", "0") == "1"
if PURE:
    SPIDER_ENTRY_HOLD = False; TWIN_SPAWN_PUSH = False; DUAL_BOUNDS_PLAYER = False; DUAL_BAND_OVERRIDE = []
    DUAL_OFF_FIRST = False; DUAL_EXIT_SWAP = False

class Level:
    """레벨 오브젝트를 큐브 계산용으로 정리. 표(gd_table.json)에 없는 ID는 무시(장식)."""
    def __init__(self, objs, table_path="gd_table.json", sec=100.0):
        table = {int(k): v for k, v in json.load(open(table_path)).items()}
        self.sec = sec
        self.cells = {}
        self.hblocks = {}          # H 블록(1859) 30x30 영역, 칸별
        self.dblocks = {}          # D 블록(1755)
        uid = 0; rawuid = {}
        # 텔레포트 트리거(3022): 플레이어가 그 x를 지나가면 대상 그룹(키 51)의 첫 오브젝트 자리로 순간이동
        self.groups = {}
        for o in objs:
            for g in str(o.get('57', '')).replace('.', ' ').split():
                if g.isdigit() and int(g) not in self.groups:
                    self.groups[int(g)] = (num(o, '2'), num(o, '3'))
        self.teleports = sorted((num(o, '2'), int(o.get('51', 0)), k) for k, o in enumerate(objs) if o['1'] == '3022')
        # 옵션 트리거(2899) 키 165: 듀얼 중력 연결 끊기 1=끊음, -1=다시 연결 (사용자: 옵션 트리거로 중력 링크 토글. 키 번호는 추정)
        self.grav_unlink = sorted((num(o, '2'), int(float(o['165']))) for o in objs if o['1'] == '2899' and o.get('165', '0') not in ('0', ''))
        # 이동 트리거(901): 대상 그룹 물체는 칸(cells)에 안 넣고 movers에 따로 → 찾을 때 플레이어 x로 옮겨서 줌
        self.strig = STATE_TRIG
        if STATE_TRIG:
            self.moves = None; self.gmods = None
            self._strig_setup(objs)
            mg = self.dyn_groups
        else:
            self.moves = _TM.Moves(objs, _TM.Speed(objs, SPEED_TABLE, SPEED_ID, DT, PORTAL_BOX)) if TRIGGER_MOVES else None
            self.gmods = _TM.GravMods(objs) if TRIGGER_MOVES else None
            mg = self.moves.groups if self.moves else set()
        self.movers = []; self.hmovers = []; self.cur_x = None
        self.free_uids = set(); self.rev_uids = set()
        for o in objs:
            i = int(o['1'])
            if o.get('121') == '1':
                continue           # 키 121 = "No Touch"(닿아도 아무 일 없음) — 장식으로 봄 (키 뜻은 추정)
            ogr = [int(g) for g in str(o.get('57', '')).split('.') if g.isdigit() and int(g) in mg]
            if i in (1859, 1755):
                hx, hy = num(o, '2'), num(o, '3')
                hs = num(o, '128', num(o, '32', 1)); vs = num(o, '129', num(o, '32', 1))
                r = (hx - 15 * hs, hx + 15 * hs, hy - 15 * vs, hy + 15 * vs)
                if ogr:
                    self.hmovers.append((i, r, ogr)); continue
                tgt = self.hblocks if i == 1859 else self.dblocks
                for c in range(int(r[0] // 60) - 1, int(r[1] // 60) + 2):
                    tgt.setdefault(c, []).append(r)
                continue
            x, y = num(o, '2'), num(o, '3')
            sx = num(o, '128', num(o, '32', 1)); sy = num(o, '129', num(o, '32', 1))
            rot = num(o, '6') if '6' in o or not ROT_FROM_131 else num(o, '131')   # 키 131/132 = 비틀기(가로·세로 회전). 히트박스는 131로 봄 (추정)
            fx, fy = o.get('4') == '1', o.get('5') == '1'
            ob = Obj(); ob.uid = uid; rawuid[id(o)] = uid; uid += 1
            if STATE_TRIG and o.get('117') == '1': self.rev_uids.add(ob.uid)    # 방향 바꾸는 오브·패드
            ob.id = i; ob.x, ob.y = x, y; ob.rot = rot; ob.flip_x = fx
            ob.corners = None; ob.radius = 0.0; ob.orient = 0; ob.hazard_slope = False
            quarter = abs(rot / 90.0 - round(rot / 90.0)) < 1e-6
            turns = int(round(rot / 90.0)) % 4
            if i in ORBS:
                ob.kind, ob.arg = "orb", ORBS[i]; w, h = ORB_BOX * (1.0 if ORB_NOSCALE else (max(sx, 1.0) if ORB_MIN36 else sx)), ORB_BOX * (1.0 if ORB_NOSCALE else (max(sy, 1.0) if ORB_MIN36 else sy))   # 작게 줄인 오브도 판정은 36 그대로 (sacrilegium end 6 실제 매크로: 0.75배 오브를 늦게 눌러도 작동)
                if turns % 2: w, h = h, w
                ang = (rot + (180.0 if fy else 0.0)) % 360.0
                ob.upside = 90.0 < ang < 270.0          # 스파이더 오브: 화살표가 아래(180도 회전)면 뒤집힘
            elif i in PADS:
                ob.kind, ob.arg = "pad", PADS[i][0]; w, h = PADS[i][1] * sx, PADS[i][2] * sy
                if turns % 2: w, h = h, w
                ang = (rot + (180.0 if fy else 0.0)) % 360.0
                ob.upside = 90.0 < ang < 270.0
                # 패드는 블록 바닥에 붙어 있음: 히트박스는 오브젝트 칸의 아래쪽 (gd-sim 표 크기, 위치는 추정)
            elif i in PORTAL_BOX:
                # 포탈 히트박스도 크기 배율과 90도 회전을 따름 (90도면 가로·세로가 바뀜)
                ob.kind, ob.arg = "portal", i; w, h = PORTAL_BOX[i][0] * sx, PORTAL_BOX[i][1] * sy
                if turns % 2 and (quarter or not PORTAL_SWAP_FIX): w, h = h, w
            elif i in MODE_PORTALS:
                ob.kind, ob.arg = "mode", i; w, h = 34 * sx, 86 * sy
                if FREE_MODE and o.get('111') == '1' and i != 286: self.free_uids.add(ob.uid)   # 자유 모드 포탈 (키 111)
                if i == 286 and DUAL_PLAIN_PORTAL_Y and sx == 1 and sy == 1 and rot == 0 and o.get('111') != '1':
                    self.free_uids.add(ob.uid)   # 크기·회전 안 바꾼 듀얼 포탈 표시 (free_uids 칸을 같이 씀: 듀얼 포탈은 자유 모드 검사를 안 타므로 안 겹침)
                if turns % 2 and (quarter or not PORTAL_SWAP_FIX): w, h = h, w
            elif i == 747:
                # 텔레포트 포탈: 히트박스 25x90, 중심이 오른쪽으로 12 (OpenGD 표). 출구 y = 포탈 y + 키 54 (추정)
                ob.kind, ob.arg = "portal", 747; w, h = 25 * sx, 90 * sy
                if turns % 2 and (quarter or not PORTAL_SWAP_FIX): w, h = h, w
                ob.tp_y = y + num(o, '54')
                if TP_OBB:                                      # 돌리거나 뒤집은 텔레포트 포탈: 중심 치우침(12)도 같이 돌리고 뒤집음 (Deadlocked 30.17초 -46도, 37.0초 90도+좌우 뒤집기 실제 기록)
                    a = math.radians(rot); ox = -12.0 if fx else 12.0; x, y = x + ox * math.cos(a), y - ox * math.sin(a)
                else: x = x + 12
            elif i in table:
                kind, w0, h0, dx, dy = table[i]
                if kind.startswith("slope"):
                    if not quarter: continue
                    w, h = w0 * sx, h0 * sy
                    if turns % 2: w, h = h, w
                    ob.orient = (turns + (2 if (fx and fy) else 1 if fx else 3 if fy else 0)) % 4
                    ob.kind = "slope"; ob.hazard_slope = "hazard" in kind
                elif kind == "saw":
                    ob.kind = "saw"; ob.radius = w0 * max(sx, sy); w = h = 2 * ob.radius
                else:
                    ob.kind = kind
                    w, h = w0 * sx, h0 * sy
                    if kind == "hazard" and HAZ_SHRINK: w, h = max(0.5, w - 2 * HAZ_SHRINK), max(0.5, h - 2 * HAZ_SHRINK)   # 시험용
                    cx, cy = x, y
                    if dx or dy:
                        ox = dx * sx * (-1.0 if fx else 1.0); oy = dy * sy * (-1.0 if fy else 1.0)
                        a = math.radians(rot)
                        cx += ox * math.cos(a) + oy * math.sin(a); cy += oy * math.cos(a) - ox * math.sin(a)
                    if quarter:
                        if turns % 2 and o.get('_noswap') != '1': w, h = h, w
                    elif kind == "solid" and SOLID_NO_OBB:
                        pass                       # 블록: 돌려도 상자 그대로 (가로·세로 바꿈은 정확히 90·270도일 때만)
                    else:
                        a = math.radians(rot); c, s = math.cos(a), math.sin(a)
                        ob.corners = [(cx + px * c + py * s, cy - px * s + py * c)
                                      for px, py in ((-w/2, -h/2), (w/2, -h/2), (w/2, h/2), (-w/2, h/2))]
                    x, y = cx, cy
            else:
                continue
            ob.w, ob.h = w, h
            if (ob.kind in ("mode", "portal") or (ob.kind == "pad" and PAD_OBB)) and not quarter and (ob.id != 747 or TP_OBB) and PORTAL_OBB:
                # 비스듬히 돌린 포탈: 돌린 직사각형(OBB)으로 닿음 판정 (가시와 같은 회전 규칙). 쇼케이스 듀얼(27도)에서 확인
                a = math.radians(rot * PORTAL_OBB); c, s = math.cos(a), math.sin(a)
                ob.corners = [(x + px * c + py * s, y - px * s + py * c)
                              for px, py in ((-w/2, -h/2), (w/2, -h/2), (w/2, h/2), (-w/2, h/2))]
            if ob.corners:
                xs = [p[0] for p in ob.corners]; ys = [p[1] for p in ob.corners]
                ob.minx, ob.maxx, ob.miny, ob.maxy = min(xs), max(xs), min(ys), max(ys)
            else:
                ob.minx, ob.maxx, ob.miny, ob.maxy = x - w / 2, x + w / 2, y - h / 2, y + h / 2
            ob.x, ob.y = x, y
            if ogr:
                ob.groups = ogr; self.movers.append(ob); continue
            for c in range(int(ob.minx // 60) - 1, int(ob.maxx // 60) + 2):
                self.cells.setdefault(c, []).append(ob)
        # 토글 오브(1594, 키 56 없음 = 끄기): 같은 자리에 겹친 진짜 오브를 쓰면 대상 그룹(키 51)의 가시·톱니가 사라짐
        # (쇼케이스 70.1초: 로봇이 검은 오브를 쓰고 바로 뒤 톱니 x 31898을 지나감. 겹친 오브와 같이 켜진다는 것은 추정)
        self.off_by = {}
        if TOGGLE_ORBS and not STATE_TRIG:                     # 상태 트리거 모드에선 토글 오브를 진짜로 계산
            for t in objs:
                if t['1'] != '1594' or t.get('56') == '1' or t.get('121') == '1': continue
                tx, ty, tg = num(t, '2'), num(t, '3'), str(t.get('51', ''))
                orb = [rawuid[id(q)] for q in objs if q is not t and int(q['1']) in ORBS and id(q) in rawuid
                       and abs(num(q, '2') - tx) < 3 and abs(num(q, '3') - ty) < 3]
                if not orb: continue
                for q in objs:
                    if id(q) in rawuid and tg in str(q.get('57', '')).split('.'):
                        self.off_by[rawuid[id(q)]] = orb[0]

    def _strig_setup(self, objs):
        """상태 트리거용 정리: 트리거 목록(trigs), 움직이거나 켜고 꺼지는 그룹(dyn_groups), 그룹 기준 물체(anchors), 안 되는 트리거(todo)"""
        def gl(o): return [int(g) for g in str(o.get('57', '')).replace('.', ' ').split() if g.isdigit()]
        self.trigs = []; dyn = set(); self.todo = {}; self.guess = {}
        for o in objs:
            i = int(o['1'])
            if i in STRIG_TODO:
                self.todo[STRIG_TODO[i]] = self.todo.get(STRIG_TODO[i], 0) + 1
            if i in STRIG_GUESS and not (i in (2069, 3645) and o.get('121') == '1'):
                self.guess[STRIG_GUESS[i]] = self.guess.get(STRIG_GUESS[i], 0) + 1
            if i not in STRIG_IDS: continue
            if i == 901 and o.get('121') == '1': continue      # 예전 엔진과 같음 (키 121 이동 트리거는 무시)
            kv = {}; lists = {}
            for k, v in o.items():
                if k in ('1', '2', '3', '57') or not k.isdigit(): continue
                if int(k) in STRIG_LISTS:
                    try: lists[int(k)] = [float(t) for t in str(v).replace('.', ' ').split()]
                    except ValueError: pass
                    continue
                try: kv[int(k)] = float(v)
                except ValueError: pass
            how = 2 if kv.get(62) else (1 if (kv.get(11) and i not in (1594, 1816, 3640, 3643, 3027, 3032, 2069, 3645) and i not in STRIG_ITEMS) else 0)
            self.trigs.append((i, num(o, '2'), num(o, '3'), how, int(bool(kv.get(87))), gl(o), kv, lists))
            if i == 3604:
                bad = [int(e) for e in lists.get(430, []) if int(e) not in STRIG_EVENTS]
                if bad: self.todo["이벤트 종류 %s" % sorted(set(bad))] = self.todo.get("이벤트 종류 %s" % sorted(set(bad)), 0) + 1
            if i in STRIG_DYN and int(kv.get(51, 0)) > 0: dyn.add(int(kv[51]))
            if i == 901 and kv.get(142): self.todo["이동: 카메라 Y 따라가기"] = self.todo.get("이동: 카메라 Y 따라가기", 0) + 1
            if i in (901, 1346) and kv.get(397): self.todo["이동·회전: 계속 따라가기(dynamic)"] = self.todo.get("이동·회전: 계속 따라가기(dynamic)", 0) + 1
            if i == 1268 and kv.get(442) is not None: self.todo["생성: 그룹 바꾸기(remap)"] = self.todo.get("생성: 그룹 바꾸기(remap)", 0) + 1

        self.dyn_groups = dyn
        self.anchors = {}                                       # 그룹 → 그 그룹 첫 물체 (x, y, 그 물체의 움직이는 그룹들)
        for o in objs:
            gs = gl(o)
            for g in gs:
                if g not in self.anchors:
                    self.anchors[g] = (num(o, '2'), num(o, '3'), [h for h in gs if h in dyn])

    def group_off(self, groups, x):
        ox = oy = 0.0
        for g in groups:
            a, b = self.moves.offset(g, x); ox += a; oy += b
        return ox, oy

    def moved(self, x):
        """플레이어 x일 때 움직이는 물체들 (옮긴 복사본)"""
        out = []
        for o in self.movers:
            ox, oy = self.group_off(o.groups, x)
            out.append(shifted(o, ox, oy) if (ox or oy) else o)
        return out

    def moved_h(self, x, which):
        out = []
        for i, r, gs in self.hmovers:
            if i != which: continue
            ox, oy = self.group_off(gs, x)
            out.append((r[0] + ox, r[1] + ox, r[2] + oy, r[3] + oy))
        return out

    def query(self, minx, maxx, miny, maxy):
        seen = set(); out = []
        for c in range(int(minx // 60), int(maxx // 60) + 1):
            for o in self.cells.get(c, ()):
                if o.uid in seen: continue
                seen.add(o.uid)
                if rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy):
                    out.append(o)
        if self.movers:
            for o in (self.moved(self.cur_x) if self.cur_x is not None else self.movers):
                if rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy):
                    out.append(o)
        out.sort(key=lambda o: o.uid)
        return out


def sat(polyA, polyB):
    for poly in (polyA, polyB):
        n = len(poly)
        for k in range(n):
            x1, y1 = poly[k]; x2, y2 = poly[(k + 1) % n]
            ax, ay = y2 - y1, x1 - x2
            pa = [ax * x + ay * y for x, y in polyA]; pb = [ax * x + ay * y for x, y in polyB]
            if max(pa) < min(pb) or max(pb) < min(pa): return False
    return True

def rect_poly(a0, a1, a2, a3):
    return [(a0, a2), (a1, a2), (a1, a3), (a0, a3)]

def circle_hits_rect(cx, cy, r, minx, maxx, miny, maxy):
    dx = max(minx - cx, 0, cx - maxx); dy = max(miny - cy, 0, cy - maxy)
    return dx * dx + dy * dy < r * r


# ---------------------------------------------------------------- 플레이어
class Player:
    __slots__ = ("x", "y", "last_x", "last_y", "y_vel", "x_vel", "speed", "y_start", "gravity",
                 "mini", "upside_down", "holding_jump", "jump_unused", "can_jump", "on_ground",
                 "is_jumping", "holding_release_delayed", "can_ring_jump", "touched_ring",
                 "is_dashing", "dash_y", "velocity_uncapped", "used", "last_flip_tick", "tick",
                 "snap_obj", "snap_distance", "is_on_slope", "was_on_slope", "slope", "slope_velocity",
                 "slope_start_tick", "dead", "cause", "half", "mode_hit",
                 "mode", "robot_boost_charge", "robot_boost_invalidated", "floor", "ceiling", "dual", "spider_hold", "dual_y_ref", "tp_wait", "pad_tick")

    def __init__(self, x, y, speed=1, mini=False, upside=False, mode="cube"):
        self.x = self.last_x = f32(x); self.y = self.last_y = f32(y)
        self.y_vel = 0.0; self.mini = mini; self.upside_down = upside
        self.holding_jump = self.jump_unused = False
        self.can_jump = False; self.on_ground = False; self.is_jumping = False
        self.holding_release_delayed = self.can_ring_jump = self.touched_ring = False
        self.is_dashing = False; self.dash_y = 0.0; self.velocity_uncapped = False
        self.used = frozenset(); self.last_flip_tick = -10 ** 6; self.tick = 0
        self.snap_obj = None; self.snap_distance = 0.0
        self.is_on_slope = self.was_on_slope = False; self.slope = None; self.slope_velocity = 0.0
        self.slope_start_tick = 0; self.dead = False; self.cause = ""; self.mode_hit = None
        self.mode = mode; self.robot_boost_charge = 0.0; self.robot_boost_invalidated = False
        self.floor, self.ceiling = 0.0, None; self.dual = False
        self.dual_y_ref = None; self.tp_wait = 0; self.pad_tick = -1
        self.spider_hold = False      # 스파이더 순간이동 뒤 버튼을 계속 누르고 있는 중 (떼기 전엔 다시 순간이동 안 함)
        self.set_speed(speed); self.refresh_size()

    def copy(self):
        p = Player.__new__(Player)
        for k in Player.__slots__:
            setattr(p, k, getattr(self, k))
        if self.dual: p.dual = self.dual.copy()        # 듀얼: 두 번째 플레이어도 같이 복사
        return p

    def key(self):
        """같은 상태 판별용 (경로 찾기에서 합치기)"""
        return (self.x, self.y, self.y_vel, self.speed, self.mini, self.upside_down, self.holding_jump,
                self.jump_unused, self.can_jump, self.on_ground, self.is_jumping, self.is_dashing,
                self.velocity_uncapped, self.used, self.is_on_slope, self.slope.uid if self.slope else -1,
                self.mode, self.robot_boost_charge, self.robot_boost_invalidated, self.floor, self.ceiling,
                self.dual.key() if self.dual else None, self.spider_hold)

    def set_speed(self, s):
        self.speed = s
        sp, ys, g, mul = SPEED_TABLE[s]
        self.x_vel = f32(sp * mul); self.y_start = ys; self.gravity = g

    def refresh_size(self):
        base = WAVE_SIZE if self.mode == "wave" else (SPIDER_SIZE if (self.mode == "spider" and SPIDER27 and self.x >= SPIDER27_X) else PLAYER_SIZE)
        self.half = base * (MINI_SCALE if self.mini else 1.0) / 2.0

    def set_mode(self, mode):
        """모드 바꾸기: 쉽·UFO·웨이브·스윙으로 들어가거나 나올 때 세로 속도 절반"""
        old = self.mode
        if old == mode: return False
        for m in (old, mode):
            if m in FLYING:
                self.y_vel *= MODE_SWITCH_KEEP; self.on_ground = False; self.can_jump = False
        if mode in ("robot", "spider"): self.robot_boost_charge = ROBOT_BOOST_MAX
        if mode == "spider" and self.holding_jump and SPIDER_ENTRY_HOLD:
            self.spider_hold = True    # 누른 채로 스파이더가 되면, 새로 누르기 전엔 순간이동 안 함 (쇼케이스 30.45초: 누른 채로 블록 위에 착지, 추정)
        self.mode = mode; self.refresh_size()
        return True

    def bounds_around(self, y):
        """바닥·천장: 비행 모드·볼은 포탈 y 둘레로, 큐브·로봇은 땅(0)만"""
        h = BOUNDS_H.get(self.mode)
        if h is None:
            self.floor, self.ceiling = 0.0, None
        else:
            self.floor = max(0.0, 30.0 * math.floor((y - h / 2.0) / 30.0)); self.ceiling = self.floor + h

    def flip_mod(self):
        return -1.0 if self.upside_down else 1.0

    def is_falling(self, v):
        return -v < self.y_vel if self.upside_down else self.y_vel < v

    def is_falling_bugged(self):
        thr = self.gravity + self.gravity
        if self.mode == "swing" or SETTINGS["fix_gravity_bug"]:   # kA32 "Fix Gravity Bug" (스윙은 항상): 뒤집혔을 때 거울 판정
            return self.y_vel > -thr if self.upside_down else self.y_vel < thr
        # 옛 물리: 듀얼 중이면 기준 중력의 부호가 뒤집힘 (2.2081 디컴파일 PlayerObject::playerIsFallingBugged:
        #   gravity = m_isInDualMode ? -m_gravity : m_gravity → 듀얼 비행기·UFO가 듀얼 밖과 다르게 움직임)
        if DUAL_FALL_FLIP and _IN_DUAL[0]: thr = -thr
        return self.y_vel > thr if self.upside_down else self.y_vel < thr

    def die(self, cause):
        if not self.dead:
            self.dead = True; self.cause = cause

    def flip_gravity(self, flip):
        if self.upside_down == flip: return False
        self.upside_down = flip; self.last_flip_tick = self.tick
        self.y_vel *= GRAVITY_FLIP_KEEP; self.can_jump = False
        return True

    def hit_ground(self):
        self.y_vel = 0.0; self.can_jump = True; self.on_ground = True; self.is_jumping = False

    # ---- 입력
    def push(self):
        self.holding_jump = True; self.jump_unused = True
        self.update_jump(0.0)

    def release(self):
        self.holding_jump = False
        if self.is_dashing: self.is_dashing = False
        if self.mode == "robot": self.robot_boost_invalidated = True

    # ---- 한 틱
    def update(self):
        if self.y_vel > 1000.0: self.y_vel = 1000.0
        elif self.y_vel < -1000.0: self.y_vel = -1000.0
        self.last_x, self.last_y = self.x, self.y
        self.update_jump(DT_SCALED)
        if self.is_dashing: self.y_vel = 0.0
        dx = self.x_vel * DT
        dy = self.y_vel * DT_SCALED
        if self.mode == "wave" and not self.is_dashing:
            dy = abs(dx) * self.flip_mod() * (1.0 if self.holding_jump else -1.0)
            if self.mini: dy += dy
        elif self.is_dashing: dy = dx * self.dash_y
        self.x = f32(self.x + f32(dx)); self.y = f32(self.y + f32(dy))
        self.holding_release_delayed = self.holding_jump
        self.can_ring_jump = self.jump_unused
        if SHIP_ORB_CLICK and self.mode == "ship": self.jump_unused = False   # 비행기: 미리 누르고 있던 입력으로는 오브가 안 켜짐. 누른 그 틱에 닿아 있어야 함 (sacrilegium end 6 실제 매크로 13.8초)
        self.touched_ring = False

    def slope_vel_mod(self):
        on = (self.tick - self.slope_start_tick) / 240.0
        boost = max(0.4, on * 10.0) if on < 0.1 else 1.0
        return self.slope_velocity * boost

    def update_jump(self, dt):
        """OSGD updateJump (classic) — 모드별"""
        mode = self.mode; up = self.upside_down; flip = -1.0 if up else 1.0
        holding = self.holding_jump; mini = self.mini
        is_holding = holding
        if mode == "robot" and not self.jump_unused: is_holding = False
        flying = mode in FLYING
        g = GRAVITY_FLYING if (flying or mode in ("ball", "spider")) else self.gravity
        g0 = g                                # 비행기가 위로 미는 힘은 중력 배율을 안 받음 (디컴파일: shipMod >= 0일 때만 modifiedGravity)
        _L = _HB["L"]
        if _L is not None and _L.gmods is not None and _L.gmods.xs:
            g = g * _L.gmods.at(self.x)      # 중력 트리거(2066) 배율 (추정)
        if _IN_DUAL[0] and mode == "ship" and DUAL_SHIP_G != 1.0 and self.x >= DUAL_SHIP_G_X0:
            g = g * DUAL_SHIP_G               # 듀얼 중 비행기는 더 무거움 (사용자 의견, 시험 중)
        scale_mod = MINI_JUMP_MOD if mini else 1.0
        if flying:
            if mini: scale_mod = MINI_FLY_MOD
            max_y = FLY_MAX_UP / scale_mod; min_y = -FLY_MAX_DOWN / scale_mod
            v = self.y_vel
            recap = ((0.0 <= v < max_y) or (min_y < v <= 0.0)) if not up else ((-max_y < v <= 0.0) or (0.0 <= v < -min_y))
            if recap: self.velocity_uncapped = False
            if mode == "ship":
                if holding and not self.velocity_uncapped:
                    sm = SHIP_THRUST
                else:
                    if self.velocity_uncapped:
                        sm = (SHIP_THRUST if self.y_vel > 0.0 else SHIP_FALL) if up else (SHIP_THRUST if self.y_vel < 0.0 else SHIP_FALL)
                    else:
                        sm = SHIP_FALL
                    if not holding and not self.is_falling_bugged(): sm = SHIP_FALL_RISING
                bm = SHIP_BOOST_HOLD_FALLING if (holding and self.is_falling_bugged()) else SHIP_BOOST
                self.y_vel = round3(self.y_vel - ((g if sm >= 0 else g0) * dt * flip * sm * bm) / scale_mod)
                if holding: self.on_ground = False
            elif mode == "ufo":
                if self.jump_unused and holding:
                    self.jump_unused = False
                    jump = (UFO_JUMP_MINI if mini else UFO_JUMP) * flip * scale_mod
                    if (up and jump < self.y_vel) or (not up and jump > self.y_vel):
                        self.y_vel = round3(jump)
                        if (self.was_on_slope or self.is_on_slope) and self.slope_velocity > 0.0:
                            mx = self.y_vel * SLOPE_JUMP_CAP
                            self.y_vel = round3(self.y_vel + self.slope_velocity * 0.5); self.y_vel = round3(min(self.y_vel, mx))
                bmod = UFO_GRAV_FALL if self.is_falling_bugged() else UFO_GRAV_RISE
                self.y_vel = round3(self.y_vel - (g * dt * flip * bmod * 0.5) / scale_mod)
                if holding: self.on_ground = False
            elif mode == "wave":
                # 미니 웨이브는 2배 기울기로 움직이니 세로 속도도 2배로 저장 (모드가 바뀔 때 이어받는 속도. 쇼케이스 30.4초: 웨이브→스파이더가 블록 위까지 올라감)
                self.y_vel = round3(self.x_vel * flip * (1.0 if holding else -1.0) * (2.0 if (self.mini and MINI_WAVE_VEL2) else 1.0))
            else:   # swing
                if self.jump_unused and holding:
                    self.jump_unused = False
                    before = self.y_vel
                    self.flip_gravity(not up)
                    self.y_vel = round3(before * SWING_FLIP_KEEP)
                    up = self.upside_down; flip = -flip
                smod = SWING_GRAV_MINI if mini else SWING_GRAV
                self.y_vel = round3(self.y_vel - smod * (g * dt * flip))
                if holding: self.on_ground = False
                scale_mod = 1.0
            mode_scale = 1.0 if mode == "swing" else FLY_MAX_DOWN / FLY_MAX_UP
            if not self.velocity_uncapped and mode != "wave":
                if not up:
                    self.y_vel = round3(max(self.y_vel, mode_scale * -8.0 / scale_mod)); self.y_vel = round3(min(self.y_vel, 8.0 / scale_mod))
                else:
                    self.y_vel = round3(max(self.y_vel, -8.0 / scale_mod)); self.y_vel = round3(min(self.y_vel, mode_scale * 8.0 / scale_mod))
            if self.is_falling_bugged(): self.is_jumping = False
        else:
            gs = GRAV_SCALE_BALL if mode in ("ball", "spider") else GRAV_SCALE_ROBOT if mode == "robot" else 1.0
            if is_holding and self.can_jump:
                if mode == "spider" and not self.is_dashing:
                    self.spider_jump()
                elif not self.is_dashing:
                    self.is_jumping = True; self.on_ground = False; self.can_jump = False; self.jump_unused = False
                    self.robot_boost_invalidated = False; self.robot_boost_charge = 0.0
                    jump = self.y_start * (ROBOT_JUMP_MOD if mode == "robot" else 1.0)
                    self.y_vel = round3(jump * flip * scale_mod)
                    if (self.was_on_slope or self.is_on_slope) and self.slope_velocity * flip > 0.0:
                        thr = self.y_vel * SLOPE_JUMP_CAP
                        sv = self.slope_velocity if mode == "ball" else self.slope_vel_mod()
                        self.y_vel = round3(self.y_vel + sv * SLOPE_JUMP_SHARE)
                        self.y_vel = round3(min(self.y_vel, thr) if not up else max(self.y_vel, thr))
                    if mode == "ball":
                        self.flip_gravity(not up); self.y_vel *= BALL_SWITCH_KEEP; self.holding_jump = False
                        # 공을 뒤집은 그 누름은 다 쓴 것: 버튼을 떼기 전에는 다시 누른 걸로 안 침 (spider_hold = "누름 다 씀" 표시를 같이 씀).
                        # 근거: Clubstep 39.5초·Clutterfunk 46.5초 매크로 — 누른 채 반대편에 닿은 뒤 실제 게임은 그대로 붙어 감.
                        # 오브를 쓴 누름은 해당 없음 (그때는 누른 채 닿으면 뒤집힘 — 예전 그대로)
                        if BALL_ONE_FLIP: self.spider_hold = True
            elif self.is_jumping:
                if mode == "robot" and holding and not self.robot_boost_invalidated:
                    if self.robot_boost_charge < ROBOT_BOOST_MAX:
                        self.robot_boost_charge += dt * ROBOT_BOOST_RATE
                        self.y_vel = round3(self.y_vel + g * dt * flip * gs)
                self.y_vel = round3(self.y_vel - gs * g * dt * flip)
                if self.is_falling_bugged():
                    self.is_jumping = False; self.on_ground = False
            else:
                if self.is_falling_bugged(): self.can_jump = False
                self.y_vel = round3(self.y_vel - gs * g * dt * flip)
                self.y_vel = round3(max(self.y_vel, -CUBE_TERMINAL) if not up else min(self.y_vel, CUBE_TERMINAL))
                if self.is_falling_bugged():
                    if (self.y_vel > 4.0) if up else (self.y_vel < -4.0):
                        self.on_ground = False

    def spider_jump(self):
        """스파이더 점프: 위·아래 첫 번째 면으로 순간이동 + 중력 반전 (GD-Remake sim.py spider_teleport 설명을 보고 내가 새로 짠 것, 원본도 추정)"""
        spider_teleport(self); self.holding_jump = False; self.spider_hold = True

    # ---- 오브·패드
    def ring_jump(self, o):
        if self.dead or o.uid in self.used: return False
        if not self.can_ring_jump or self.is_dashing: return False
        if not self.holding_release_delayed: return False
        if self.touched_ring: return False
        was_gnd = self.on_ground
        self.used = self.used | {o.uid}
        self.touched_ring = True
        kind = o.arg; scale_mod = MINI_JUMP_MOD if self.mini else 1.0
        if kind == "spider":
            # 스파이더 오브: 오브 방향(위/아래)에 맞게 중력을 맞춘 뒤 그 방향 면으로 순간이동
            if o.upside != self.upside_down: self.flip_gravity(not self.upside_down)
            spider_teleport(self); self.holding_jump = False
            if SPORB_HOLD: self.spider_hold = True   # 게임 원문(spiderTestJumpInternal): 순간이동하면 "누르는 중"을 끔 → 떼기 전엔 어떤 모드에서도 다시 안 눌림
            return True
        if kind in ("dash", "gravity_dash"):
            if kind == "gravity_dash": self.flip_gravity(not self.upside_down); _grav_evt(self)
            self.start_dash(o)
        elif kind == "black":
            self.jump_unused = False
            if self.mode in FLYING:
                v = self.flip_mod() * -ORB_BLACK_FLY
                if self.mode == "ufo": v *= ORB_BLACK_UFO_MOD
            else:
                v = self.flip_mod() * -ORB_BLACK
                if self.mode == "spider": v *= ORB_BLACK_SPIDER_MOD
            self.y_vel = round3(v)
            self.velocity_uncapped = True
            if self.mode in ("ball", "swing"): self.holding_jump = False
            if self.mode == "ball" and BALL_ONE_FLIP and not was_gnd and "black" in BALL_ORB_EATS_PRESS: self.spider_hold = True
        else:
            self.jump_unused = False; self.is_jumping = True; self.on_ground = False; self.can_jump = False
            vel = self.y_start; m = self.mode
            if kind == "blue": vel *= ORB_BLUE
            elif kind == "green":
                if m == "ship": vel *= ORB_GREEN_SHIP
            elif kind == "pink": vel *= ORB_PINK_M.get(m, ORB_PINK)
            elif kind == "red": vel *= pick(ORB_RED_M, m, self.mini, ORB_RED)
            elif m == "robot": vel *= ORB_ROBOT
            if kind == "green": self.flip_gravity(not self.upside_down); _grav_evt(self)
            vel *= ORB_OVERRIDE.get((round(o.x, 1), round(o.y, 1)), 1.0)
            self.y_vel = round3(self.flip_mod() * vel * scale_mod)
            if m in ("ball", "spider"): self.y_vel *= ORB_BALL_SPIDER_KEEP; self.holding_jump = False
            if m == "ball" and BALL_ONE_FLIP and not was_gnd and kind in BALL_ORB_EATS_PRESS: self.spider_hold = True   # 공: 이 오브에 쓴 누름은 다 쓴 것 → 떼기 전에는 착지해도 안 뒤집힘 (Clubstep 40.07초 실제 기록)
            elif m == "swing": self.y_vel *= ORB_SWING_KEEP; self.holding_jump = False
            if kind == "blue": self.flip_gravity(not self.upside_down); _grav_evt(self)
            if kind == "red": self.velocity_uncapped = True
        return True

    def start_dash(self, o):
        if self.is_dashing: return
        self.is_dashing = True
        ring_rot = o.rot + (180.0 if o.flip_x else 0.0)
        angle = snap360(math.fmod(-ring_rot, 360.0))
        if abs(angle) > 90.0:
            angle = snap360((-180.0 if angle <= 0.0 else 180.0) - angle)
        angle = max(-DASH_MAX_ANGLE, min(DASH_MAX_ANGLE, angle))
        rad = math.radians(angle)
        self.dash_y = math.sin(rad) / abs(math.cos(rad))
        # 쇼케이스에서 직접 잰 기울기가 있으면 그 값 사용 (게임 내부 규칙 미확인 — 계산값과 다를 때만 등록)
        ov = DASH_OVERRIDE.get((round(o.x, 1), round(o.y, 1)))
        if ov is not None: self.dash_y = ov * (1.0 if self.dash_y >= 0 else -1.0)

    def propel(self, mod):
        self.is_jumping = True; self.on_ground = False; self.can_jump = False
        self.is_on_slope = self.was_on_slope = False
        scale_mod = MINI_JUMP_MOD if self.mini else 1.0
        self.y_vel = round3(mod * PAD_BASE * self.flip_mod() * scale_mod)
        if self.mode in ("ball", "spider", "swing"): self.y_vel *= PAD_BALL_KEEP

    def snap_jump(self, o):
        """계단 리듬 보정 (checkSnapJumpToObject)"""
        if SNAP_CUBE_ONLY and self.mode != "cube": return       # 큐브일 때만 (디컴파일 isInNormalMode + Deadlocked 로봇 실제 기록)
        prev = self.snap_obj
        if prev is not None and prev.uid != o.uid:
            up_dx, up_dx_mini, down_dx, double_dx, tol = SNAP_TABLE[self.speed]
            if self.mini: up_dx = up_dx_mini
            step = self.flip_mod() * 30.0
            ddx, ddy = o.x - prev.x, o.y - prev.y
            if ((abs(ddx - up_dx) <= tol and abs(ddy - step) <= tol)
                    or (abs(ddx - down_dx) <= tol and abs(ddy + step) <= tol)
                    or (abs(ddx - double_dx) <= tol and abs(ddy - 2.0 * step) <= tol)):
                fx = o.x + self.snap_distance
                if abs(fx - self.x) > tol:
                    fx = self.x - tol if fx <= self.x else self.x + tol
                self.x = f32(fx)
        self.snap_obj = o
        self.snap_distance = self.x - o.x


# ---------------------------------------------------------------- 충돌 (한 틱)
_HB = {"L": None}
def L_HBLOCK(p):
    L = _HB["L"]
    if L is None or not (L.hblocks or L.hmovers): return False
    hh = p.half
    for r in L.hblocks.get(int(p.x // 60), ()):
        if rects_touch(p.x - hh, p.x + hh, p.y - hh, p.y + hh, *r): return True
    for r in (L.moved_h(p.x, 1859) if L.hmovers else ()):
        if rects_touch(p.x - hh, p.x + hh, p.y - hh, p.y + hh, *r): return True
    return False

_TPED = [False]
SLOPE_TOP_SKIP = _os.environ.get('SLOPE_TOP', '1') != '0'   # 경사 꼭대기 끝 규칙
SNAP_CUBE_ONLY = _os.environ.get('SNAP_CUBE', '1') != '0'   # 계단 스냅은 큐브 전용
TP_KEEP_BOUNDS = _os.environ.get('TP_KEEP', '1') != '0'   # 텔레포트 때 모드 바닥·천장을 다시 잡지 않음 (끄려면 TP_KEEP=0)
TP_RECOLLIDE = _os.environ.get('TP_RECOLLIDE', '1') != '0'   # 텔레포트한 틱에 도착 자리 물체를 다시 검사 (끄려면 TP_RECOLLIDE=0)
def collide(p, L):
    _HB["L"] = L; L.cur_x = p.x
    p.was_on_slope = p.is_on_slope; p.is_on_slope = False
    hh = p.half
    bounds(p)
    if p.dead: return
    launch = [None]
    for _pass in (0, 1):                             # 텔레포트한 틱에는 도착한 자리의 패드·포탈도 같은 틱에 받음 (Deadlocked 30.17초 실제 기록)
        _TPED[0] = False
        solids, hazards = [], []
        qs = L.query(p.x - hh - 30, p.x + hh + 30, p.y - hh - 30, p.y + hh + 30)
        if PORTAL_REVERSE: qs = qs[::-1]
        for o in qs:
            k = o.kind
            if k == "solid":
                solids.append(o)
            elif k in ("hazard", "saw"):
                hazards.append(o)
            elif o.uid not in p.used and not (p.tp_wait > 0 and k in ("mode", "portal")):
                minx, maxx, miny, maxy = p.x - hh, p.x + hh, p.y - hh, p.y + hh
                if k == "slope":
                    if rects_touch(minx, maxx, miny, maxy, o.x - o.w, o.x + o.w, o.y - o.h, o.y + o.h):
                        slope_hit(p, o, launch)
                elif rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy) and (
                        o.corners is None or sat(o.corners, rect_poly(minx, maxx, miny, maxy))):
                    touch(p, o)
                if p.dead: return
        if not (TP_RECOLLIDE and _TPED[0]): break
    if launch[0] is not None and not p.is_on_slope:
        p.y = f32(launch[0]); p.y_vel = round3(p.slope_vel_mod())
        p.is_jumping = True; p.can_jump = False; p.on_ground = False; p.slope = None
    solids.sort(key=lambda o: (-(o.x // L.sec), -o.uid))
    for o in solids:
        solid_hit(p, o)
        if p.dead: return
    minx, maxx, miny, maxy = p.x - hh, p.x + hh, p.y - hh, p.y + hh
    for o in hazards:
        if o.uid in L.off_by and L.off_by[o.uid] in p.used: continue
        if o.kind == "saw":
            if SETTINGS["fix_radius"]:              # kA39: 플레이어도 원으로 봄
                hit = math.hypot(p.x - o.x, p.y - o.y) <= o.radius + hh
            else:                                    # 옛 방식: 중심이 박스 안이거나 박스 꼭짓점이 원 안
                hit = (minx <= o.x <= maxx and miny <= o.y <= maxy) or any(
                    (px - o.x) ** 2 + (py - o.y) ** 2 < o.radius ** 2
                    for px, py in ((maxx, maxy), (maxx, miny), (minx, miny), (minx, maxy)))
        else:
            hit = rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy)
            if hit and o.corners is not None:
                hit = sat(o.corners, rect_poly(minx, maxx, miny, maxy))
        if hit:
            p.die("가시·톱니 (ID %d, x %.0f)" % (o.id, o.x)); return
    if p.was_on_slope and not p.is_on_slope:
        left_slope(p)

def bounds(p):
    """바닥·천장 (gd-sim Player::postCollision). 웨이브는 중심이 히트박스 한 개만큼 떨어져 미끄러짐"""
    up = p.upside_down; hh = p.half
    ext = hh * 2.0 if p.mode == "wave" else hh
    fl, ce = p.floor, p.ceiling
    if p.y - ext <= fl:
        if p.mode == "wave":
            p.y = f32(fl + ext)
        elif not up:
            if p.y_vel <= 0.0:
                p.y = f32(fl + ext); p.hit_ground()
        elif p.y - ext < fl:
            if ce is None:
                p.die("뒤집힌 채 땅에 닿음"); return
            p.y = f32(fl + ext)
            if p.y_vel < 0.0: p.y_vel = 0.0
    if ce is not None and p.y + ext >= ce:
        if p.mode == "wave":
            p.y = f32(ce - ext)
        elif up:
            if p.y_vel >= 0.0:
                p.y = f32(ce - ext); p.hit_ground()
        elif p.y + ext > ce:
            p.y = f32(ce - ext)
            if p.y_vel > 0.0: p.y_vel = 0.0

def spider_teleport(p):
    """스파이더 순간이동 (추정 — 게임 코드 미공개, GD-Remake 설명을 보고 새로 짬):
    지금 중력 기준 '위'(뒤집혔으면 아래) 방향으로, 같은 세로줄에서 처음 만나는 블럭 면 또는 화면 경계까지 이동하고 중력을 뒤집음.
    가시가 먼저 있으면 그 위에 놓이고 죽음."""
    L = _HB["L"]
    up = p.upside_down
    d = -1.0 if up else 1.0
    hh = p.half
    edge = p.y + d * hh
    best, best_o = None, None
    if L is not None:
        lo, hi = (edge, edge + 3000.0) if d > 0 else (edge - 3000.0, edge)
        L.cur_x = p.x
        for o in L.query(p.x - hh + 1.0, p.x + hh - 1.0, lo, hi):
            if o.kind not in ("solid", "hazard", "saw", "pad"): continue
            if SPIDER_RAY_SOLID and o.kind in ("hazard", "saw"): continue
            if o.maxx < p.x - hh + 1.0 or o.minx > p.x + hh - 1.0: continue
            # 패드: 쇼케이스 27.5초처럼 순간이동 길에 있는 패드에서 멈추고 그 패드를 밟음 (추정)
            face = (o.maxy if d > 0 else o.miny) if o.kind == "pad" else (o.miny if d > 0 else o.maxy)
            dist = (face - edge) * d
            if dist >= -1e-3 and (best is None or dist < best): best, best_o = dist, o
    bound = p.ceiling if d > 0 else p.floor
    bdist = None if bound is None else (bound - edge) * d
    p.flip_gravity(not up)
    p.is_jumping = False
    if best is not None and (bdist is None or best <= bdist):
        p.y = f32(edge + d * best - d * hh); p.last_y = p.y
        if best_o.kind not in ("solid", "pad"):
            p.die("스파이더 순간이동으로 가시에 닿음"); return
        p.y_vel = 0.0
    elif bdist is not None:
        p.y = f32(bound - d * hh); p.last_y = p.y; p.y_vel = 0.0

def L_DBLOCK(p):
    L = _HB["L"]
    if L is None or not (L.dblocks or L.hmovers): return False
    hh = p.half
    for r in L.dblocks.get(int(p.x // 60), ()):
        if rects_touch(p.x - hh, p.x + hh, p.y - hh, p.y + hh, *r): return True
    for r in (L.moved_h(p.x, 1755) if L.hmovers else ()):
        if rects_touch(p.x - hh, p.x + hh, p.y - hh, p.y + hh, *r): return True
    return False

def touch(p, o):
    if o.kind == "orb":
        p.ring_jump(o)
    elif o.kind == "pad":
        if PAD_ONE_PER_TICK:                         # 패드는 한 틱에 하나만 (겹친 패드는 다음 틱에: Deadlocked 30.2초 실제 기록, 3틱 연속 16)
            if p.pad_tick == p.tick: return
            p.pad_tick = p.tick
        if o.arg == "spider":
            p.used = p.used | {o.uid}; p.robot_boost_invalidated = True
            if o.upside != p.upside_down: p.flip_gravity(not p.upside_down)
            spider_teleport(p); p.holding_jump = False
            if SPORB_HOLD: p.spider_hold = True
            return
        if o.arg == "blue":
            if o.upside != p.upside_down: return
            p.used = p.used | {o.uid}; p.robot_boost_invalidated = True
            p.propel(PAD_BLUE); p.flip_gravity(not p.upside_down); p.velocity_uncapped = False; _grav_evt(p)
        else:
            p.used = p.used | {o.uid}
            p.robot_boost_invalidated = True
            mod = pick(PAD_MOD_M[o.arg], p.mode, p.mini, PAD_MOD[o.arg]) if o.arg in PAD_MOD_M else PAD_MOD[o.arg]
            p.propel(mod); p.velocity_uncapped = (o.arg == "red")
    elif o.kind == "mode":
        p.used = p.used | {o.uid}
        if p.mode_hit != 287: p.mode_hit = o.arg     # 같은 틱에 듀얼 끄기(287)와 다른 포탈을 같이 닿아도 듀얼 끄기는 남김
        if o.arg == 286 and _DUAL_SNAP[0] is None:
            _DUAL_SNAP[0] = (p.copy(), o.y if (not DUAL_BOUNDS_PLAYER or (_HB["L"] is not None and o.uid in _HB["L"].free_uids)) else p.y)        # 듀얼에 닿은 그 순간의 모습 (같은 틱에 뒤에 닿는 포탈은 안 받음)
        if o.arg in MODE_OF:
            p.set_mode(MODE_OF[o.arg]); p.bounds_around(o.y)
            _L = _HB["L"]
            if _L is not None and o.uid in _L.free_uids:
                p.floor, p.ceiling = 0.0, None     # 자유 모드: 비행 모드여도 위·아래 경계 없음 (땅만)
    elif o.kind == "portal":
        i = o.arg
        p.used = p.used | {o.uid}
        if i in (10, 11): p.flip_gravity(i == 11); _grav_evt(p)
        elif i in (99, 101):
            if p.mini != (i == 101): p.mini = (i == 101); p.refresh_size()
        elif i in SPEED_ID: p.set_speed(SPEED_ID[i])
        elif i == 747:
            p.y = f32(o.tp_y); p.last_y = p.y; _TPED[0] = True
            if p.ceiling is not None and not TP_KEEP_BOUNDS: p.bounds_around(p.y)   # 게임은 텔레포트해도 바닥·천장을 그대로 둠 (Deadlocked 37.4초 실제 기록: 볼이 원래 바닥 180에 얹힘)

def solid_hit(p, o):
    if o.id in BREAKABLE and o.uid in p.used: return      # 이미 부서진 블록
    hh = p.half
    pminx, pmaxx, pminy, pmaxy = p.x - hh, p.x + hh, p.y - hh, p.y + hh
    if not rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.miny, o.maxy): return
    if o.corners is not None:
        if sat(o.corners, rect_poly(p.x - hh * INNER, p.x + hh * INNER, p.y - hh * INNER, p.y + hh * INNER)):
            p.die("기운 블록 (ID %d, x %.0f)" % (o.id, o.x))
        return
    s = p.slope
    if s is not None and (p.is_on_slope or p.was_on_slope):
        if rects_touch(o.minx, o.maxx, o.miny, o.maxy, s.x - s.w * 0.6, s.x + s.w * 0.6, s.y - s.h * 0.55, s.y + s.h * 0.55):
            ex = min(max(o.x, s.minx), s.maxx); r = s.h / s.w
            rises = s.orient in (0, 2)
            surf = s.miny + (ex - s.minx) * r if rises else s.maxy - (ex - s.minx) * r
            if (o.miny >= surf - 2.0) if s.orient >= 2 else (o.maxy <= surf + 2.0):
                return
    up = p.upside_down
    wave_d = p.mode == "wave" and L_DBLOCK(p)       # D 블록 안의 웨이브는 비행 모드처럼 (추정)
    fly = p.mode in FLYING
    thr = SNAP_THRESHOLD_FLY if fly else SNAP_THRESHOLD
    dy = p.y - p.last_y; yv = p.y_vel; g = -1.0 if up else 1.0
    lethal = dy * g > 0.0
    if p.mode == "wave" and not wave_d:
        lethal = True
        k = INNER
        if rects_touch(p.x - hh * k, p.x + hh * k, p.y - hh * k, p.y + hh * k, o.minx, o.maxx, o.miny, o.maxy):
            p.die("웨이브가 블록에 닿음 (ID %d, x %.0f)" % (o.id, o.x))
        return
    # 비행 모드·볼은 머리 부딪힘 가능, 큐브·로봇은 H 블록(1859) 안에서만
    bumps_head = fly or p.mode == "ball" or L_HBLOCK(p)
    if up and not bumps_head:
        lethal = True
    else:
        low = pminy + thr
        if low < o.maxy and low - dy < o.maxy:
            lethal = True
        elif yv > 0.0:
            lethal = False
        else:
            p.y = f32(o.maxy + hh)
            if not up:
                p.snap_jump(o); p.hit_ground()
            elif p.y_vel < 0.0:
                p.y_vel = 0.0
            return
    if lethal and (up or bumps_head):
        high = pmaxy - thr
        if not (o.miny < high and o.miny < high - dy):
            if yv >= 0.0:
                p.y = f32(o.miny - hh)
                if up:
                    p.snap_jump(o); p.hit_ground()
                elif p.y_vel > 0.0:
                    p.y_vel = 0.0
                return
    if up:
        head = (pmaxy - thr >= o.maxy) or (pmaxy - thr - dy >= o.maxy)
    else:
        head = (pminy + thr <= o.miny) or (pminy + thr - dy <= o.miny)
    if not lethal: return
    k = INNER
    if not rects_touch(p.x - hh * k, p.x + hh * k, p.y - hh * k, p.y + hh * k, o.minx, o.maxx, o.miny, o.maxy):
        return
    if head and p.tick - p.last_flip_tick < FLIP_GRACE_TICKS:
        p.y = f32(o.maxy + hh) if up else f32(o.miny - hh); p.hit_ground(); p.on_ground = False; return
    if o.id in BREAKABLE:
        p.used = p.used | {o.uid}; return                 # 죽지 않고 블록이 부서짐
    p.die("블록에 박음 (ID %d, x %.0f)" % (o.id, o.x))

def slope_hit(p, o, launch):
    hh = p.half
    pminx, pmaxx, pminy, pmaxy = p.x - hh, p.x + hh, p.y - hh, p.y + hh
    attached = p.was_on_slope and p.slope is o
    ceiling = o.orient >= 2
    if not attached:
        side = False
        if o.orient in (1, 2) and p.x < o.minx:
            if rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.minx + 1.0, o.miny, o.maxy):
                face_hit(p, o.minx, o.minx + 1.0, o.miny, o.maxy, o); side = True
        if ceiling:
            if p.y >= o.maxy and rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.maxy - 1.0, o.maxy):
                face_hit(p, o.minx, o.maxx, o.maxy - 1.0, o.maxy, o); side = True
        elif p.y <= o.miny + 1.0 and rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.miny, o.miny + 1.0):
            face_hit(p, o.minx, o.maxx, o.miny, o.miny + 1.0, o); side = True
        if side or p.dead: return
        pminy, pmaxy = p.y - hh, p.y + hh
    if not rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.miny, o.maxy): return
    up = p.upside_down; g = -1.0 if up else 1.0
    ratio = o.h / o.w; angle = math.atan(ratio)
    rises = o.orient in (0, 2)
    surface = o.miny + (p.x - o.minx) * ratio if rises else o.maxy - (p.x - o.minx) * ratio
    radius = hh / math.cos(angle)
    if ceiling:
        sd = -1.0
        if o.hazard_slope: surface -= SLOPE_HAZARD_OFFSET
        limit = o.miny - hh
        target = min(max(surface - radius, limit), o.maxy)
    else:
        sd = 1.0
        if o.hazard_slope: surface += SLOPE_HAZARD_OFFSET
        limit = o.maxy + hh
        target = max(min(surface + radius, limit), o.miny)
    inside = sd * p.y < sd * target
    walkable = sd == g
    uphill = (o.orient == 0) if not up else (o.orient == 3)
    if not walkable:
        if not inside: return
        if o.hazard_slope or (p.mode == "wave" and not WAVE_SLOPE_SLIDE):
            p.die("경사 (x %.0f)" % o.x)
        elif p.mode in FLYING or p.mode == "ball" or p.is_dashing:
            # 대시 중에 머리 위 경사에 닿으면 경사를 따라 미끄러짐 (쇼케이스 31.55~31.85초: 대시가 경사 밑면을 따라감, 추정)
            p.y = f32(target)
            if sd * p.y_vel < 0.0: p.y_vel = 0.0
            if SLOPE_UNDERSIDE and p.x >= SU_X0 and (p.mode in FLYING or p.mode == "ball"):
                # 반대쪽 면에 닿아도 게임은 '경사 위에 있음'으로 침 (2.2081 디컴파일 collidedWithSlopeInternal: m_isOnSlope = true,
                # 경사 속도 계산 → 경사 끝을 떠날 때 그 속도를 받음). 쇼케이스 70.85~71.1초: 듀얼 둘째 비행기가 다이아몬드 경사 윗면을
                # 타고 올라간 뒤 속도를 유지한 채 천장까지 곧게 올라감 (사용자: "바닥에 부딪히고 올라감")
                ang = math.atan(o.h / o.w)
                lv = min(SLOPE_VEL_FACTOR / ang, SLOPE_VEL_CAP) * (o.h * p.x_vel / o.w) * SLOPE_FLY_BALL_MOD
                p.slope_velocity = lv * g * (1.0 if uphill else -1.0)
                p.can_jump = False; p.on_ground = False
                if not p.was_on_slope: p.slope_start_tick = p.tick
                p.slope = o; p.is_on_slope = True
        elif sd * (target - p.y) <= 2.0:
            p.y = f32(target); p.y_vel = round3(sd * max(sd * p.y_vel, 2.0)); p.can_jump = False; p.on_ground = False
        else:
            # 죽음은 안쪽 작은 히트박스(0.3배)가 경사 안에 들어갔을 때만 (블럭과 같은 규칙으로 맞춤, 추정)
            # 예: 파트 2 x 7226 — 웨이브가 V자 통로 끝에서 큐브로 바뀌는 순간 큐브 윗부분이 경사에 살짝 걸림
            ih = hh * INNER
            edge = p.y + ih if sd < 0 else p.y - ih
            if (edge > surface) if sd < 0 else (edge < surface):
                p.die("경사에 머리 (x %.0f)" % o.x)
        return
    collided = inside
    if SLOPE_TOP_SKIP and collided and not attached and target == limit and sd * (target - p.y) < 0.5: collided = False   # 경사 꼭대기 끝에 살짝 걸친 것은 경사에 붙지 않음 (Deadlocked 59.97초 실제 기록)
    if not collided and attached and not uphill and g * p.y_vel <= 0.0 and p.x <= o.maxx:
        collided = True
    if not collided: return
    if o.hazard_slope or (p.mode == "wave" and not WAVE_SLOPE_SLIDE):
        p.die("경사 (x %.0f)" % o.x); return
    slope_y_vel = o.h * p.x_vel / o.w
    lv = min(SLOPE_VEL_FACTOR / angle, SLOPE_VEL_CAP) * slope_y_vel
    if p.mode in FLYING or p.mode == "ball": lv *= SLOPE_FLY_BALL_MOD
    p.slope_velocity = lv * g * (1.0 if uphill else -1.0)
    if attached and uphill and target == limit:
        launch[0] = target; return
    before = p.y_vel
    p.y = f32(target); p.hit_ground()
    if g * before > 5.0:
        p.y_vel = round3(before); p.can_jump = False
    if not p.was_on_slope: p.slope_start_tick = p.tick
    p.slope = o; p.is_on_slope = True

def face_hit(p, minx, maxx, miny, maxy, o):
    """경사의 세로면·밑면 = 두께 1 블록"""
    fake = Obj(); fake.uid = o.uid; fake.id = o.id; fake.x, fake.y = (minx + maxx) / 2, (miny + maxy) / 2
    fake.minx, fake.maxx, fake.miny, fake.maxy = minx, maxx, miny, maxy; fake.corners = None
    solid_hit(p, fake)

def left_slope(p):
    o = p.slope
    if o is None: return
    up = p.upside_down
    if SLOPE_UNDERSIDE and p.x >= SU_X0 and (p.mode in FLYING or p.mode == "ball") and (-1.0 if o.orient >= 2 else 1.0) != (-1.0 if up else 1.0) \
            and p.x + p.half >= o.maxx:
        # 반대쪽 면으로 탄 경사의 끝에서 떠남: 경사 속도로 튀어나감 (위쪽 면 끝의 발사와 같은 처리, 추정.
        # 쇼케이스 70.94~71.1초: 둘째 비행기가 다이아몬드 경사를 떠난 뒤 최고 속도로 곧게 올라감)
        p.y_vel = round3(p.slope_vel_mod()); p.is_jumping = True; p.can_jump = False; p.on_ground = False
        p.slope = None; return
    uphill = (o.orient == 0) if not up else (o.orient == 3)
    if not uphill and not p.is_jumping and p.x > o.maxx:
        g = -1.0 if up else 1.0
        p.y_vel = round3(-g * p.x_vel * (o.h / o.w)); p.can_jump = False
    p.slope = None

def orb_on_press(p, L):
    """UFO·스윙: 오브에 닿은 채 새로 누르면 날갯짓·뒤집기 대신 오브가 먼저 작동 (실제 GD 동작, 규칙 구현은 추정).
    참고 코드 순서대로면 누르는 순간 UFO 점프가 입력을 써 버려서 UFO가 오브를 못 씀 → 그 경우만 따로 처리."""
    hh = p.half
    L.cur_x = p.x
    for o in L.query(p.x - hh, p.x + hh, p.y - hh, p.y + hh):
        if o.kind == "orb" and o.uid not in p.used and not p.is_dashing:
            p.holding_jump = True
            p.can_ring_jump = True; p.holding_release_delayed = True; p.touched_ring = False
            if p.ring_jump(o):
                p.jump_unused = False
                return True
    return False

def _step_one(p, L, pressed, main=True):
    if p.dead: return
    # 스파이더: 순간이동은 '새로 누를 때'만. 누른 채로 있으면 매 틱 다시 순간이동하던 것 막음
    #   (GD-Remake도 순간이동 뒤 holding을 꺼 두는데, 우리 입력 방식에선 다음 틱에 다시 누름으로 보여서 생긴 문제)
    if not pressed: p.spider_hold = False
    elif p.spider_hold and (SPORB_HOLD or p.mode == "spider" or (p.mode == "ball" and BALL_ONE_FLIP)) and not p.holding_jump:
        pressed = False
    if pressed != p.holding_jump:
        if pressed and (ORB_FIRST_ALL or p.mode in ("ufo", "swing") or (p.mode == "ball" and BALL_ORB_FIRST) or (p.mode == "spider" and SPIDER_ORB_FIRST)) and orb_on_press(p, L):
            pass
        else:
            p.push() if pressed else p.release()
    if main and L.teleports and TP_BEFORE_UPDATE:
        teleport_trigger(p, L, before=True)
    p.update()
    if main and L.teleports and not TP_BEFORE_UPDATE:
        teleport_trigger(p, L)
    collide(p, L)
    if p.tp_wait > 0: p.tp_wait -= 1
    p.tick += 1

def teleport_trigger(p, L, before=False):
    """텔레포트 트리거(3022): x를 지나가는 순간 대상 그룹 첫 오브젝트 위치로 이동. 속도는 그대로 둠 (추정).
    첫 번째 플레이어에만 적용 (듀얼 두 번째는 추정상 제외)."""
    for tx, g, k in L.teleports:
        if ((p.x - 30.0 < tx <= p.x) if before else p.last_x < tx <= p.x) and g in L.groups:
            key = -1000000 - k
            if key in p.used: continue
            p.used = p.used | {key}
            gx, gy = L.groups[g]
            p.x = f32(gx); p.y = f32(gy); p.last_x = p.x; p.last_y = p.y
            p.on_ground = False
            p.tp_wait = TP_PORTAL_DELAY

_DUAL_SNAP = [None]
_IN_DUAL = [False]
DUAL_SHIP_G = float(_os2.environ.get("DSG", "1.0"))   # 듀얼 중 비행기 중력 배율 (시험용. 진짜 원인은 DUAL_FALL_FLIP이라 1.0)
DUAL_FALL_FLIP = _os2.environ.get("DFF", "1") != "0"
SLOPE_UNDERSIDE = _os2.environ.get("SU", "1") != "0"
SU_X0 = float(_os2.environ.get("SU_X0", "-1e18"))
FREE_MODE = _os2.environ.get("FREE", "1") != "0"   # 모드 포탈 '자유 모드'(키 111): 위·아래 경계 없애기 (사용자 지적)   # 시험용: 이 x부터만   # 비행 모드·볼이 경사 반대쪽 면에 닿아도 경사 위로 침 (2.2081 디컴파일)   # 듀얼 중 '떨어지는 중' 판정 기준 부호 뒤집기 (2.2081 디컴파일)
DUAL_SHIP_G_X0 = float(_os2.environ.get("DSG_X0", "-1e18"))   # 이 x부터만 (시험용)
_GRAV = [None]
SPIDER_ORB_FIRST = True   # 스파이더도 오브에 닿은 채 누르면 순간이동 대신 오브 (쇼케이스 82.2~82.6초: 분홍·노랑 오브로 뜀, 추정)
DUAL_SPAWN_SRC = _os.environ.get("DSS", "1") != "0"   # 듀얼 둘째 생김: 2.2081 spawnFromPlayer 그대로 (속도 = -첫째 속도, 반으로 안 줄임). 누를 때 둘째도 누름(TWIN_SPAWN_PUSH)은 toggleDualMode가 디컴파일에 없어 쇼케이스 기준 유지 (추정)
ORB_FIRST_ALL = _os.environ.get("OFA", "1") != "0"   # 모든 모드: 오브에 닿은 채 누르면 땅 점프보다 오브 먼저 (쇼케이스 103.5초: 경사 위 작은 큐브가 빨간 오브 속도로 튐)
BALL_ORB_FIRST = True   # 볼: 오브에 닿은 채 누르면 볼 자체 뒤집기 대신 오브가 먼저 (볼 스윙콥터, 추정)
DUAL_GRAV_LINK = True   # 듀얼: 오브·패드·포탈로 한 명의 중력이 바뀌면 다른 한 명은 그 반대가 됨 (2.2 이전 '볼 스윙콥터' 원리, 사용자 확인)

def _grav_evt(p):
    _GRAV[0] = p

def _linked(L, x):
    st = 0
    for tx, v in getattr(L, "grav_unlink", ()):
        if tx <= x: st = v
        else: break
    return DUAL_GRAV_LINK and st != 1

DUAL_BOUNDS_UNSNAP = _os.environ.get("DBU", "0") == "1"   # 실험: 듀얼 바닥·천장을 30칸 격자에 안 맞추고 포탈 y 중심 그대로
DUAL_SHARED_SPEED = _os.environ.get("DSP", "1") != "0"
def _dual_bounds(p, y):
    """듀얼 중 바닥·천장: 듀얼 포탈 y 둘레, 높이 270 (둘 중 하나가 비행기·UFO·웨이브면 300). GD-Remake/gdsolver 측정값"""
    tw = p.dual
    H = 300.0 if (p.mode in ("ship", "ufo", "wave") or tw.mode in ("ship", "ufo", "wave")) else 270.0
    fl = max(0.0, 30.0 * math.floor((y - H / 2.0) / 30.0)) if not DUAL_BOUNDS_UNSNAP else max(0.0, y - H / 2.0); ce = fl + H
    for x0, x1, f2, c2 in DUAL_BAND_OVERRIDE:
        if x0 <= p.x <= x1: fl, ce = f2, c2
    for q in (p, tw):
        q.floor, q.ceiling = fl, ce

def step(p, L, pressed):
    """한 틱: 입력 → 이동 → 충돌. p를 직접 바꿈.
    듀얼(포탈 286 ~ 287): 두 번째 플레이어(p.dual)가 같은 입력으로 같이 움직이고, 둘 중 하나라도 죽으면 죽음.
    두 번째 플레이어는 듀얼 포탈에 닿은 순간 첫 번째와 같은 자리·같은 모드·반대 중력으로 생김 (GD 2.2 방식, 세부는 추정)."""
    if p.dead: return
    tw = p.dual
    _IN_DUAL[0] = bool(tw)
    _sp0 = p.speed; _tsp0 = tw.speed if tw else None
    p.mode_hit = None
    _DUAL_SNAP[0] = None; _GRAV[0] = None
    _step_one(p, L, pressed)
    snap = _DUAL_SNAP[0]; _DUAL_SNAP[0] = None
    if tw and _GRAV[0] is p and not tw.dead and _linked(L, p.x):
        tw.flip_gravity(not p.upside_down)
    if tw and DUAL_OFF_FIRST and p.mode_hit == 287:
        # 첫째가 듀얼 끄기 포탈(287)에 닿으면 그 틱에 둘째는 바로 사라짐 → 둘째는 이번 틱에 움직이지도 부딪히지도 않음
        # (쇼케이스 46.15~46.25초: 둘째 비행기가 x 18331 가시 바로 앞에서 사라지고 첫째만 남음, 추정)
        p.dual = False; p.dual_y_ref = None; tw = None
    if tw:
        tw.mode_hit = None
        _GRAV[0] = None
        _step_one(tw, L, pressed, main=False)
        if _GRAV[0] is tw and not p.dead and _linked(L, p.x):
            p.flip_gravity(not tw.upside_down)
        if tw.dead:
            p.die("듀얼 두 번째 플레이어: " + tw.cause); return
        if DUAL_SHARED_SPEED:
            # 듀얼: 속도는 둘이 같이 씀 (쇼케이스 146~147초: 두 로봇이 늘 같은 x. 따로 쓰면 0.07초 늦어짐)
            if tw.speed != _tsp0: p.set_speed(tw.speed)
            elif p.speed != _sp0: tw.set_speed(p.speed)
        if tw.mode_hit == 287:
            if DUAL_EXIT_SWAP:
                for k in Player.__slots__: setattr(p, k, getattr(tw, k))
            p.dual = False
    if snap is not None and not p.dual and not tw:
        # 두 번째 플레이어: 듀얼 포탈에 닿은 순간의 첫 번째 플레이어 복사본, 중력 반대 (GD-Remake 추정과 같음)
        # 쇼케이스 30.6~30.9초: 둘째는 웨이브로 남고 첫째만 스파이더 → 같은 틱에 뒤에 닿은 포탈은 둘째에게 안 감
        t2 = snap[0]; t2.dual = False
        t2.upside_down = not p.upside_down
        t2.can_jump = t2.on_ground = t2.is_jumping = False
        t2.used = p.used
        if DUAL_SPAWN_SRC:
            # copyAttributes: 누름 상태(holding, jump_unused)는 복사된 그대로. spawnFromPlayer(flip): 속도 = -첫째 속도 (flipGravity 반감은 덮어씀)
            t2.y_vel = -snap[0].y_vel
        p.dual = t2
        _dual_bounds(p, snap[1])
        p.dual_y_ref = snap[1]
        if TWIN_SPAWN_PUSH and pressed:
            # 버튼을 누른 채로 둘째가 생기면 둘째에겐 '새로 누름': 스파이더면 바로 순간이동 (쇼케이스 30.4~30.7초:
            # 둘째가 바로 바닥으로 내려가 볼 포탈을 탐, 추정)
            t2.holding_jump = False; t2.spider_hold = False
            if t2.mode == "spider": t2.spider_jump()
            else: t2.push()
    elif p.mode_hit == 287:
        p.dual = False; p.dual_y_ref = None
    if not p.dual: p.dual_y_ref = None
    if p.dual and p.dual_y_ref is not None:
        _dual_bounds(p, p.dual_y_ref)
    if SHARED_USED and p.dual:
        u = p.used | p.dual.used; p.used = u; p.dual.used = u
