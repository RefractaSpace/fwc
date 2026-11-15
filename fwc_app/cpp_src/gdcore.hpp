#include <memory>
#include <array>
#include <atomic>
// gdcore.hpp — gdplay.py(파이썬 엔진)를 C++로 그대로 옮긴 것. 계산 순서·반올림(f32, round3)까지 똑같이 맞춤.
// 레벨은 파이썬에서 내보낸 텍스트(export_level.py → json2txt.py)를 읽음 (레벨 파싱은 옮기지 않음).
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <limits>

namespace gd {

static inline double f32(double v) { return (double)(float)v; }

static inline double round3(double v) {
    double whole = (double)(long long)v;   // 파이썬 int(v): 0 쪽으로 자름
    if (whole != v) {
        double frac = (v - whole) * 1000.0;
        frac = frac >= 0 ? std::floor(frac + 0.5) : -std::floor(-frac + 0.5);
        v = whole + frac / 1000.0;
    }
    return v;
}

// 파이썬 float // (floor division) 그대로 (CPython float_floor_div)
static inline double pyfloordiv(double vx, double wx) {
    double mod = std::fmod(vx, wx);
    double div = (vx - mod) / wx;
    if (mod != 0.0) {
        if ((wx < 0) != (mod < 0)) { mod += wx; div -= 1.0; }
    }
    double floordiv;
    if (div != 0.0) {
        floordiv = std::floor(div);
        if (div - floordiv > 0.5) floordiv += 1.0;
    } else {
        floordiv = std::copysign(0.0, vx / wx);
    }
    return floordiv;
}
static inline long long pyidiv(double a, double b) { return (long long)pyfloordiv(a, b); }

// ---------------------------------------------------------------- 상수
const double DT = 0.25;
static const double DT_SCALED = f32(DT * 0.9);
struct SpeedRow { double sp, ys, g, mul; };
static const SpeedRow SPEED_TABLE[5] = {
    {f32(0.7), f32(10.620032), f32(0.940199), f32(5.980002)},
    {f32(0.9), f32(11.180032), f32(0.958199), f32(5.770002)},
    {f32(1.1), f32(11.420032), f32(0.957199), f32(5.870002)},
    {f32(1.3), f32(11.230032), f32(0.961199), f32(6.000002)},
    {f32(1.6), f32(11.230032), f32(0.961199), f32(6.000002)},
};
const double MINI_SCALE = 0.6, MINI_JUMP_MOD = 0.8, CUBE_TERMINAL = 15.0, GRAVITY_FLIP_KEEP = 0.5;
const double PLAYER_SIZE = 30.0, INNER = 0.3, SNAP_THRESHOLD = 10.0;
const int FLIP_GRACE_TICKS = 24;   // int(round(0.1*240))
struct SnapRow { double up_dx, up_dx_mini, down_dx, double_dx, tol; };
static const SnapRow SNAP_TABLE[5] = {{90, 90, 120, 60, 1}, {120, 90, 150, 90, 1}, {150, 90, 195, 120, 2}, {180, 90, 225, 135, 2}, {120, 120, 150, 90, 1}};
const double ORB_PINK = 0.72, ORB_RED = 1.38, ORB_BLUE = 0.8, ORB_BLACK = 15.0;
const double PAD_BASE = 16.0, PAD_BLUE = 0.8, DASH_MAX_ANGLE = 70.0;
const double SLOPE_VEL_FACTOR = 1.12, SLOPE_VEL_CAP = 1.54, SLOPE_FLY_BALL_MOD = 0.75;
const double GRAVITY_FLYING = 0.958199, MINI_FLY_MOD = 0.85;
const double GRAV_SCALE_BALL = 0.6, GRAV_SCALE_ROBOT = 0.9;
static const double BALL_SWITCH_KEEP = f32(0.6);
const double ROBOT_JUMP_MOD = 0.5, ROBOT_BOOST_MAX = 1.5, ROBOT_BOOST_RATE = 0.1;
const double FLY_MAX_UP = 8.0, FLY_MAX_DOWN = 6.4;
const double SHIP_THRUST = -1.0, SHIP_FALL = 0.8, SHIP_FALL_RISING = 1.2, SHIP_BOOST_HOLD_FALLING = 0.5, SHIP_BOOST = 0.4;
const double UFO_JUMP = 7.0, UFO_JUMP_MINI = 8.0, UFO_GRAV_FALL = 0.8, UFO_GRAV_RISE = 1.2;
const double SWING_GRAV = 0.4, SWING_GRAV_MINI = 0.6, SWING_FLIP_KEEP = 0.8;
static const bool G_PAD_ONE = !(std::getenv("PAD_ONE") && std::string(std::getenv("PAD_ONE")) == "0");   // 패드는 한 틱에 하나만 작동
static const bool G_MOVE_LAG = !(std::getenv("MOVE_LAG") && std::string(std::getenv("MOVE_LAG")) == "0");   // 가로 위치 무브 시작 시점을 게임처럼 (끄려면 MOVE_LAG=0)
static const bool G_TP_KEEP = !(std::getenv("TP_KEEP") && std::string(std::getenv("TP_KEEP")) == "0");   // 텔레포트 때 모드 바닥·천장 유지
static const bool G_SNAP_CUBE_ONLY = !(std::getenv("SNAP_CUBE") && std::string(std::getenv("SNAP_CUBE")) == "0");   // 계단 스냅 큐브 전용 (끄려면 SNAP_CUBE=0)
static const bool G_SLOPE_TOP_SKIP = !(std::getenv("SLOPE_TOP") && std::string(std::getenv("SLOPE_TOP")) == "0");   // 경사 꼭대기 끝 규칙 (끄려면 SLOPE_TOP=0)
static const bool G_SPORB_HOLD = !(std::getenv("SPORB_HOLD") && std::string(std::getenv("SPORB_HOLD")) == "0");   // 거미 오브·패드로 순간이동하면 그 누름은 끝 (모든 모드, 떼기 전엔 다시 안 눌림. 게임 원문 확인. 끄려면 SPORB_HOLD=0)
static const bool G_SHIP_ORB = !(std::getenv("SHIP_ORB") && std::string(std::getenv("SHIP_ORB")) == "0");   // 비행기 오브는 새로 눌러야 작동 (끄려면 SHIP_ORB=0)
static const bool G_WAVE_SLOPE = !(std::getenv("WAVE_SLOPE") && std::string(std::getenv("WAVE_SLOPE")) == "0");   // 웨이브는 경사에서 미끄러짐 (끄려면 WAVE_SLOPE=0)
static thread_local bool G_TPED = false;                             // 이번 충돌 검사 중 텔레포트했나
static const bool G_TP_RECOLLIDE = !(std::getenv("TP_RECOLLIDE") && std::string(std::getenv("TP_RECOLLIDE")) == "0");   // 텔레포트한 틱에 도착 자리 물체를 다시 검사
static const bool G_PUSH_HEAD = !(std::getenv("PUSH_HEAD") && std::string(std::getenv("PUSH_HEAD")) == "0");   // 머리 쪽으로 다가오는 움직이는 블록은 플레이어를 밀어냄 (Deadlocked 26.6초 실제 기록: 작은 UFO가 내려오는 기둥에 딱 붙어 같이 내려감). 시험용으로 PUSH_HEAD=0 이면 끔
const double MODE_SWITCH_KEEP = 0.5, WAVE_SIZE = 10.0, SPIDER_SIZE_C = 27.0, SNAP_THRESHOLD_FLY = 6.0;
const double ORB_GREEN_SHIP = 0.7, ORB_ROBOT = 0.9, ORB_BALL_SPIDER_KEEP = 0.7, ORB_SWING_KEEP = 0.6;
const double ORB_BLACK_FLY = 14.0, ORB_BLACK_UFO_MOD = 0.8, ORB_BLACK_SPIDER_MOD = 1.1;
const double PAD_BALL_KEEP = 0.6;
const double SLOPE_JUMP_SHARE = 0.25, SLOPE_JUMP_CAP = 1.4, SLOPE_HAZARD_OFFSET = 4.0;

enum Mode { CUBE = 0, SHIP, BALL, UFO, WAVE, ROBOT, SPIDER, SWING };
static const char* MODE_NAME[8] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};
static inline bool flying(int m) { return m == SHIP || m == UFO || m == WAVE || m == SWING; }
enum Kind { K_SOLID = 0, K_HAZARD, K_SAW, K_SLOPE, K_ORB, K_PAD, K_PORTAL, K_MODE };
enum OrbArg { O_YELLOW = 0, O_PINK, O_RED, O_BLUE, O_GREEN, O_BLACK, O_DASH, O_GDASH, O_SPIDER };
enum PadArg { P_YELLOW = 0, P_PINK, P_RED, P_BLUE, P_SPIDER };

static inline int mode_of(int id) {
    switch (id) { case 12: return CUBE; case 13: return SHIP; case 47: return BALL; case 111: return UFO;
        case 660: return WAVE; case 745: return ROBOT; case 1331: return SPIDER; case 1933: return SWING; }
    return -1;
}
static inline double bounds_h(int m) {   // 없으면 -1
    switch (m) { case SHIP: case UFO: case WAVE: case SWING: return 300.0; case BALL: case SPIDER: return 240.0; }
    return -1.0;
}
static inline int speed_id(int id) {
    switch (id) { case 200: return 0; case 201: return 1; case 202: return 2; case 203: return 3; case 1334: return 4; }
    return -1;
}
static inline double orb_pink_mod(int m) { if (m == SHIP) return 0.37; if (m == UFO) return 0.42; if (m == BALL) return 0.77; return ORB_PINK; }
static inline double orb_red_mod(int m, bool mini) {
    if (mini) { if (m == SHIP) return 1.4; if (m == UFO) return 1.36; }
    switch (m) { case SHIP: return 1.0; case UFO: return 1.02; case BALL: return 1.34; case ROBOT: return 1.28; case SPIDER: return 1.34; }
    return ORB_RED;
}
static inline double pad_mod(int arg, int m, bool mini) {
    if (arg == P_PINK) { if (m == SHIP) return 0.35; if (m == UFO) return 0.4; if (m == BALL) return 0.7; return 0.65; }
    if (arg == P_RED) { if (mini) { if (m == SHIP) return 0.95; if (m == UFO) return 0.98; } if (m == SHIP) return 0.63; if (m == UFO) return 0.6; return 1.25; }
    return 1.0;
}

struct Flags {
    bool rot131 = true, spider_entry_hold = false, twin_spawn_push = false, dual_off_first = true, dual_bounds_player = false,
         mini_wave_vel2 = false, tp_before_update = false; int tp_portal_delay = 0;
    bool portal_reverse = false, shared_used = false, ball_orb_first = true, dual_grav_link = true;
    bool fix_gravity_bug = false, fix_radius = false, dual_exit_swap = false, spider_ray_solid = false, spider27 = false, spider_orb_first = false; double spider27_x = 1e18;
};
static Flags F;
static double G_DSG = 1.0, G_DSG_X0 = -1e18;      // 듀얼 중 비행기 중력 배율 (파이썬 DUAL_SHIP_G)
static thread_local bool G_INDUAL = false;
static bool G_DFF = true, G_SU = true, G_OFA = true, G_DSS = true, G_DBU = false, G_DSP = true;
static thread_local int G_SP0 = 0;
static std::vector<std::array<double, 4>> G_BANDS;   // 듀얼 바닥·천장 덮어쓰기 (x0, x1, 바닥, 천장)

struct Pt { double x, y; };
struct Obj {
    int uid = -1, id = 0, kind = 0, arg = 0; double x = 0, y = 0, w = 0, h = 0, minx = 0, maxx = 0, miny = 0, maxy = 0, rot = 0;
    bool flip_x = false, upside = false; int orient = 0; bool hazard_slope = false;
    double radius = 0, tp_y = 0; std::vector<Pt> corners;
    double vy = 0;                                                    // 이번 틱에 무브로 세로로 움직인 만큼 (움직이는 물체만)
};
struct Rect { double a0, a1, a2, a3; };

static inline bool rects_touch(double a0, double a1, double a2, double a3, double b0, double b1, double b2, double b3) {
    return !(a1 < b0 || b1 < a0 || a3 < b2 || b3 < a2);
}

static bool sat(const std::vector<Pt>& A, const std::vector<Pt>& B) {
    const std::vector<Pt>* polys[2] = {&A, &B};
    for (int q = 0; q < 2; ++q) {
        const auto& poly = *polys[q]; size_t n = poly.size();
        for (size_t k = 0; k < n; ++k) {
            double x1 = poly[k].x, y1 = poly[k].y, x2 = poly[(k + 1) % n].x, y2 = poly[(k + 1) % n].y;
            double ax = y2 - y1, ay = x1 - x2;
            double amin = 1e300, amax = -1e300, bmin = 1e300, bmax = -1e300;
            for (auto& p : A) { double v = ax * p.x + ay * p.y; if (v < amin) amin = v; if (v > amax) amax = v; }
            for (auto& p : B) { double v = ax * p.x + ay * p.y; if (v < bmin) bmin = v; if (v > bmax) bmax = v; }
            if (amax < bmin || bmax < amin) return false;
        }
    }
    return true;
}
static inline std::vector<Pt> rect_poly(double a0, double a1, double a2, double a3) { return {{a0, a2}, {a1, a2}, {a1, a3}, {a0, a3}}; }
// 플레이어 기준 좌표 (방향 반대·게임 회전): 세계 = M·로컬 + T (M은 90도 단위 회전·뒤집기). 플레이어 물리는 로컬에서 그대로 계산
struct Frame {
    int a = 1, b = 0, c = 0, d = 1; double tx = 0, ty = 0; uint64_t h = 0;
    bool id() const { return a == 1 && b == 0 && c == 0 && d == 1 && tx == 0 && ty == 0; }
    void to_world(double lx, double ly, double& wx, double& wy) const { wx = a * lx + b * ly + tx; wy = c * lx + d * ly + ty; }
    void to_local(double wx, double wy, double& lx, double& ly) const { double x = wx - tx, y = wy - ty; lx = a * x + c * y; ly = b * x + d * y; }
};
static thread_local const Frame* G_FRM = nullptr;    // 지금 상태의 좌표 (기본이면 nullptr = 예전과 똑같이)

struct Level {
    std::vector<Obj> objs;                                   // objs[uid] (없는 uid는 uid=-1)
    std::unordered_map<long long, std::vector<int>> cells;   // 칸 → uid 목록 (파이썬 순서 그대로)
    std::unordered_map<long long, std::vector<Rect>> hblocks, dblocks;
    struct Tp { double tx; int g, k; };
    std::vector<Tp> teleports;
    std::unordered_map<int, Pt> groups;
    std::vector<std::pair<double, int>> grav_unlink;
    std::unordered_map<int, int> off_by;
    std::unordered_set<int> free_uids;                         // 자유 모드 모드 포탈                     // 토글 오브: 가시 uid → 이 오브 uid를 쓰면 사라짐
    double sec = 100.0;
    // 이동 트리거(901) — trigmove.py와 같은 계산 (플레이어 x만으로)
    struct Move { double tx, dx, dy, ticks, rate, mod_x, x_end; int easing; bool lock_x; };
    std::unordered_map<int, std::vector<Move>> moves;        // 그룹 → 이동 (tx 순)
    std::vector<double> spd_x, spd_v;                         // 속도 지도
    std::vector<double> gm_x, gm_v;                           // 중력 트리거 배율
    std::vector<int> mover_uid; std::vector<std::vector<int>> mover_groups; std::vector<int> mover_idx;
    struct Env { double minx, maxx, miny, maxy; }; std::vector<Env> mover_env;   // 움직이는 물체가 갈 수 있는 범위 전체 (넉넉하게)   // uid → 순번 (-1 = 안 움직임)
    struct HM { int which; Rect r; std::vector<int> groups; std::vector<int> dg; };
    std::vector<HM> hmovers;
    // ---- 상태 트리거 (STRIG) 레벨 쪽: 안 바뀌는 것
    bool strig = false;
    struct Trg {
        int idx = 0, id = 0, how = 0; bool multi = false; double x = 0, y = 0;
        std::vector<int> groups, dg;                         // 이 트리거가 속한 그룹 (dg = 움직이는 그룹 번호)
        std::vector<std::pair<int, double>> kv;              // 키 → 값 (키 순)
        std::vector<std::pair<int, std::vector<double>>> lists;   // 목록 값 키 (이벤트 430, 고급 랜덤 152)
        const std::vector<double>* list(int key) const { for (auto& l : lists) if (l.first == key) return &l.second; return nullptr; }
        double k(int key, double d = 0.0) const { auto it = std::lower_bound(kv.begin(), kv.end(), std::make_pair(key, -1e300)); return (it != kv.end() && it->first == key) ? it->second : d; }
        bool has(int key) const { auto it = std::lower_bound(kv.begin(), kv.end(), std::make_pair(key, -1e300)); return it != kv.end() && it->first == key; }
    };
    std::vector<Trg> trg;
    std::vector<int> trg_x, trg_touch, trg_rings, trg_items, trg_colb;   // x로 켜짐(x 순) / 닿으면 켜짐 / 토글 오브 / 아이템 / 충돌 블록
    std::vector<int> trg_tblock, trg_torb, trg_sblock;                    // 터치 토글 블록 / 텔레포트 오브 / 충돌 상태 블록
    bool col_players = false;                                             // 충돌 트리거가 플레이어를 씀
    std::unordered_map<int, std::vector<int>> kf_by_group;                // 키프레임 점 (그룹별, 순서대로)
    std::vector<int> trg_force;                                           // 힘 블록·힘 원
    std::unordered_set<int> rev_uids;                                     // 방향 바꾸는 오브·패드 (키 117)
    std::vector<double> trg_xs;
    std::unordered_map<int, std::vector<int>> trg_by_group;  // 그룹 → 그 그룹 트리거 (생성용)
    std::unordered_map<int, int> dyn_idx; std::vector<int> dyn_gid;   // 움직이는 그룹 ↔ 번호
    struct Clu { std::vector<int> dg; std::vector<int> movers; double minx = 1e300, maxx = -1e300, miny = 1e300, maxy = -1e300, pad = 0; };
    std::vector<Clu> clu; std::vector<int> mover_clu;
    struct Anc { double x = 0, y = 0; std::vector<int> dg; };
    std::unordered_map<int, Anc> anc;
    int fpy_hist = 0;                                         // 플레이어 Y 따라가기 지연 최대 (틱)
    int dg_of(int g) const { auto it = dyn_idx.find(g); return it == dyn_idx.end() ? -1 : it->second; }

    // 파이썬 query: 칸을 돌며 처음 보는 것만, 상자가 닿으면 넣고 uid 순 정렬
    // 빠른 찾기: 가로·세로 60칸 격자 (파이썬 query와 결과 같음 = 상자에 닿는 칸 안 물체 전부, uid 순)
    double gx0 = 0, gy0 = 0; int gw = 0, gh = 0; std::vector<std::vector<int>> grid;
    struct BB { double minx, maxx, miny, maxy; }; std::vector<BB> bb;
    void build_grid() {
        std::vector<int> us; for (auto& kv : cells) for (int u : kv.second) us.push_back(u);
        std::sort(us.begin(), us.end()); us.erase(std::unique(us.begin(), us.end()), us.end());
        bb.assign(objs.size(), {0, 0, 0, 0});
        if (us.empty()) return;
        double ax = 1e300, bx = -1e300, ay = 1e300, by = -1e300;
        for (int u : us) { const Obj& o = objs[u]; ax = std::min(ax, o.minx); bx = std::max(bx, o.maxx); ay = std::min(ay, o.miny); by = std::max(by, o.maxy); bb[u] = {o.minx, o.maxx, o.miny, o.maxy}; }
        gx0 = std::floor(ax / 60.0) * 60.0; gy0 = std::floor(ay / 60.0) * 60.0;
        gw = (int)((bx - gx0) / 60.0) + 1; gh = (int)((by - gy0) / 60.0) + 1;
        grid.assign((size_t)gw * gh, {});
        for (int u : us) {
            const Obj& o = objs[u];
            int c0 = (int)std::floor((o.minx - gx0) / 60.0), c1 = (int)std::floor((o.maxx - gx0) / 60.0);
            int d0 = (int)std::floor((o.miny - gy0) / 60.0), d1 = (int)std::floor((o.maxy - gy0) / 60.0);
            for (int cx = c0; cx <= c1; ++cx) for (int cy = d0; cy <= d1; ++cy) grid[(size_t)cy * gw + cx].push_back(u);
        }
    }
    void query(double minx, double maxx, double miny, double maxy, std::vector<int>& out) const {
        if (G_FRM) {                                                   // 로컬 상자 → 세계 상자
            double xs[4], ys[4]; G_FRM->to_world(minx, miny, xs[0], ys[0]); G_FRM->to_world(maxx, miny, xs[1], ys[1]); G_FRM->to_world(maxx, maxy, xs[2], ys[2]); G_FRM->to_world(minx, maxy, xs[3], ys[3]);
            minx = *std::min_element(xs, xs + 4); maxx = *std::max_element(xs, xs + 4); miny = *std::min_element(ys, ys + 4); maxy = *std::max_element(ys, ys + 4);
        }
        out.clear();
        if (gw == 0) { add_movers(minx, maxx, miny, maxy, out); std::sort(out.begin(), out.end()); return; }   // 안 움직이는 물체가 하나도 없는 레벨
        int c0 = std::max(0, (int)std::floor((minx - gx0) / 60.0)), c1 = std::min(gw - 1, (int)std::floor((maxx - gx0) / 60.0));
        int d0 = std::max(0, (int)std::floor((miny - gy0) / 60.0)), d1 = std::min(gh - 1, (int)std::floor((maxy - gy0) / 60.0));
        bool multi = (c1 > c0) || (d1 > d0);
        for (int cy = d0; cy <= d1; ++cy) for (int cx = c0; cx <= c1; ++cx)
            for (int u : grid[(size_t)cy * gw + cx]) {
                const BB& o = bb[u];
                if (rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy)) out.push_back(u);
            }
        add_movers(minx, maxx, miny, maxy, out);
        std::sort(out.begin(), out.end());
        if (multi) out.erase(std::unique(out.begin(), out.end()), out.end());
    }
    void add_movers(double minx, double maxx, double miny, double maxy, std::vector<int>& out) const;
};

// ---- 이동 트리거 계산 (trigmove.py 그대로)
static double ease_bounce(double t) {
    if (t < 1 / 2.75) return 7.5625 * t * t;
    if (t < 2 / 2.75) { t -= 1.5 / 2.75; return 7.5625 * t * t + 0.75; }
    if (t < 2.5 / 2.75) { t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375; }
    t -= 2.625 / 2.75; return 7.5625 * t * t + 0.984375;
}
static double ease(int kind, double rate, double t) {
    const double PI = 3.141592653589793;
    switch (kind) {
    case 0: return t;
    case 1: t *= 2.0; return t < 1.0 ? 0.5 * std::pow(t, rate) : 1.0 - 0.5 * std::pow(2.0 - t, rate);
    case 2: return std::pow(t, rate);
    case 3: return rate != 0 ? std::pow(t, 1.0 / rate) : t;
    case 4: case 5: case 6: {
        if (t == 0.0 || t == 1.0) return t;
        double per = rate != 0 ? rate : 0.3;
        if (kind == 4) {
            per = rate != 0 ? rate : 0.45; double s = per / 4.0, u = t * 2.0 - 1.0;
            if (u < 0) return -0.5 * std::pow(2.0, 10.0 * u) * std::sin((u - s) * 2 * PI / per);
            return std::pow(2.0, -10.0 * u) * std::sin((u - s) * 2 * PI / per) * 0.5 + 1.0;
        }
        double s = per / 4.0;
        if (kind == 5) { double u = t - 1.0; return -(std::pow(2.0, 10.0 * u)) * std::sin((u - s) * 2 * PI / per); }
        return std::pow(2.0, -10.0 * t) * std::sin((t - s) * 2 * PI / per) + 1.0;
    }
    case 7: return t < 0.5 ? (1.0 - ease_bounce(1.0 - t * 2.0)) * 0.5 : ease_bounce(t * 2.0 - 1.0) * 0.5 + 0.5;
    case 8: return 1.0 - ease_bounce(1.0 - t);
    case 9: return ease_bounce(t);
    case 10: if (t == 0.0 || t == 1.0) return t; t *= 2.0;
        return t < 1.0 ? 0.5 * std::pow(2.0, 10.0 * (t - 1.0)) : 0.5 * (2.0 - std::pow(2.0, -10.0 * (t - 1.0)));
    case 11: return t == 0.0 ? 0.0 : std::pow(2.0, 10.0 * (t - 1.0)) - 0.001;
    case 12: return t == 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t);
    case 13: return -0.5 * (std::cos(PI * t) - 1.0);
    case 14: return 1.0 - std::cos(t * PI / 2.0);
    case 15: return std::sin(t * PI / 2.0);
    case 16: { double o2 = 1.70158 * 1.525; t *= 2.0;
        if (t < 1.0) return t * t * ((o2 + 1.0) * t - o2) / 2.0;
        t -= 2.0; return t * t * ((o2 + 1.0) * t + o2) / 2.0 + 1.0; }
    case 17: { double o = 1.70158; return t * t * ((o + 1.0) * t - o); }
    case 18: { double o = 1.70158; t -= 1.0; return t * t * ((o + 1.0) * t + o) + 1.0; }
    }
    return t;
}
static double spd_ticks(const Level& L, double x0, double x1) {
    if (x1 <= x0) return 0.0;
    int i = (int)(std::upper_bound(L.spd_x.begin(), L.spd_x.end(), x0) - L.spd_x.begin()) - 1;
    double t = 0.0, x = x0;
    while (true) {
        double nx = (size_t)(i + 1) < L.spd_x.size() ? L.spd_x[i + 1] : 1e18;
        double v = L.spd_v[std::max(i, 0)];
        if (x1 <= nx) return t + (x1 - x) / v;
        t += (nx - x) / v; x = nx; ++i;
    }
}
static void group_offset(const Level& L, int g, double x, double& ox, double& oy) {
    auto it = L.moves.find(g); if (it == L.moves.end()) return;
    for (auto& m : it->second) {
        if (x < m.tx) break;
        double e;
        if (m.ticks <= 0) e = 1.0;
        else {
            double tl = x < m.x_end ? spd_ticks(L, m.tx, x) : m.ticks;
            e = ease(m.easing, m.rate, std::min(1.0, tl / m.ticks));
        }
        if (m.lock_x) ox += (std::min(x, m.x_end) - m.tx) * m.mod_x;
        else ox += m.dx * e;
        oy += m.dy * e;
    }
}
// group_offset 과 같은 계산인데, 트리거마다 (x몫, y몫)을 따로 돌려줌 (ensure_moved 캐시용)
static void group_parts(const Level& L, int g, double x, std::vector<std::pair<double, double>>& parts) {
    auto it = L.moves.find(g); if (it == L.moves.end()) return;
    for (auto& m : it->second) {
        if (x < m.tx) break;
        double e;
        if (m.ticks <= 0) e = 1.0;
        else {
            double tl = x < m.x_end ? spd_ticks(L, m.tx, x) : m.ticks;
            e = ease(m.easing, m.rate, std::min(1.0, tl / m.ticks));
        }
        double px = m.lock_x ? (std::min(x, m.x_end) - m.tx) * m.mod_x : m.dx * e;
        parts.push_back({px, m.dy * e});
    }
}
static double grav_mod_at(const Level& L, double x) {
    int i = (int)(std::upper_bound(L.gm_x.begin(), L.gm_x.end(), x) - L.gm_x.begin()) - 1;
    return i >= 0 ? L.gm_v[i] : 1.0;
}
// ---- 상태 트리거 (STRIG): 그룹 상태. 틱마다 바뀌고 상태(State)에 들어감
// 그룹 변환 = 선형(a b / c d, 회전) + 회전 이동(rt) + 이동 트리거 이동(mt).  점 p → L·p + rt + mt
struct GS { double a = 1, b = 0, c = 0, d = 1, rtx = 0, rty = 0, mtx = 0, mty = 0, rot = 0, sx = 1, sy = 1; int order = 0; bool on = true; };
static thread_local bool G_GSDIRTY = false;   // 그룹 상태가 바뀜 → 물체 위치 다시 계산 필요
static thread_local std::vector<int> G_MUTS;   // 이번 틱에 바뀐 그룹 번호
// 나눠 쓰는 배열: 16개씩 묶어서, 바뀐 묶음만 복사 (상태를 틱마다 저장해도 메모리 적게)
template <class T, int N = 16> struct Cow {
    using Chunk = std::array<T, N>;
    std::shared_ptr<std::vector<std::shared_ptr<Chunk>>> ch;            // 묶음 목록도 나눠 씀: 상태 복사 = 포인터 하나
    void init(size_t n) { ch = std::make_shared<std::vector<std::shared_ptr<Chunk>>>(); for (size_t i = 0; i < (n + N - 1) / N; ++i) ch->push_back(std::make_shared<Chunk>()); }
    const T& get(size_t i) const { return (*(*ch)[i / N])[i % N]; }
    T& mut(size_t i) {
        if (ch.use_count() > 1) ch = std::make_shared<std::vector<std::shared_ptr<Chunk>>>(*ch);
        auto& p = (*ch)[i / N]; if (p.use_count() > 1) p = std::make_shared<Chunk>(*p);
        G_GSDIRTY = true; G_MUTS.push_back((int)i); return (*p)[i % N];
    }
};
struct YNode { double y; std::shared_ptr<const YNode> prev; double x = 0; };       // 플레이어 y 기록 (지연 따라가기용, 나눠 쓰는 목록)
struct Act {                                                          // 진행 중인 이동·회전·따라가기
    int t = 0, kind = 0, gi = -1; double done = 0; bool paused = false;
    double px = 0, py = 0, ang = 0, x0 = 0, y0 = 0, dx = 0, dy = 0, T = 0, mx = 1, my = 1; bool lx = false, ly = false, forever = false; int cg = 0, easing = 0; double rate = 2.0; int et = -1, src = 0; double vx = 0, vy = 0, steer = 0, sx = 1, sy = 1; bool started = false;
};
struct Spn { double due; int g; int t; };                             // 기다리는 생성 (due = 틱)
struct TrigRun {
    Cow<GS> gs; uint64_t ver = 0; size_t xcur = 0;
    std::shared_ptr<const std::vector<int>> fired;                    // 다시 못 켜지는 것 (닿기·생성 트리거, 쓴 토글 오브, 주운 아이템)
    std::vector<Act> acts; std::vector<Spn> spawns;
    std::vector<std::pair<int, double>> gdy;                           // 이번 틱 그룹별 세로 이동량 (충돌 판정용, 틱마다 새로 채움)
    std::vector<std::pair<int, int>> items;                           // 아이템 번호 → 개수
    std::vector<int> armed;                                           // 켜진 뒤 기다리는 개수·충돌·터치 트리거
    std::vector<std::pair<int, int>> cols;                            // 지금 닿아 있는 충돌 블록 번호 쌍
    int order = 0; double grav = 1.0; uint64_t sig = 0, gsh = 0x51ed27; bool lastin = false;   // gsh = 그룹 상태 내용 해시 (같은 내용이면 같은 번호 → 갈래끼리 위치 캐시 같이 씀)
    std::shared_ptr<const YNode> yh;
    int points = 0;                                                   // 점수 아이템
    struct Tm { int id; double t, mod; bool run; };
    std::vector<Tm> timers;                                           // 타이머 (2.2)
    struct TL { int ti; int id; double target; int g; bool multi, pause, done; };
    std::vector<TL> tlis;                                             // 타이머가 목표 시간에 닿으면 생성
    std::vector<int> inside;                                          // 지금 플레이어가 안에 있는 충돌 상태 블록
    int pmode = -1; bool pmini = false, pup = false, pdual = false, pdash = false, pground = true; // 지난 틱 플레이어 (이벤트용)
    std::vector<int> pend;                                            // 지난 틱에 생긴 게임 이벤트 (충돌 중 생김 → 다음 틱에 처리)
    bool blk1 = false, blk2 = false;                                  // 플레이어 조작 '점프 멈춤': 손을 뗄 때까지 누름 무시
    double tw = 1.0, gextra = 0.0;                                    // 시간 왜곡, 쌓인 추가 시간(틱)
    Frame fr; int freq = 0, fa = 1, fb = 0, fc = 0, fd = 1;           // 플레이어 좌표 + 바꾸기 요청 (1 방향 반대, 2 게임 회전)
    struct Area { int ti, et; double done, cx, cy, str; bool has_c; };
    std::vector<Area> areas;                                          // 켜져 있는 영역 효과 (영역 이동·회전·크기)
    struct Seq { int ti, idx; double last; };
    std::vector<Seq> seqs;                                            // 순서 트리거 진행
    bool was_fired(int t) const { return fired && std::binary_search(fired->begin(), fired->end(), t); }
    void add_fired(int t) {
        std::vector<int> v = fired ? *fired : std::vector<int>(); auto it = std::lower_bound(v.begin(), v.end(), t);
        if (it != v.end() && *it == t) return; v.insert(it, t); fired = std::make_shared<const std::vector<int>>(std::move(v));
    }
};
static std::atomic<uint64_t> G_VERT{1};
static inline uint64_t new_ver() {                                     // 스레드마다 따로 세서 (공유 숫자 다툼 없음) 겹치지 않는 번호
    static thread_local uint64_t base = G_VERT.fetch_add(1) << 40, n = 0;
    return base | ++n;
}
static thread_local const TrigRun* G_TR = nullptr;                    // 지금 계산하는 상태의 트리거 (물체 위치는 이걸로)
static thread_local TrigRun* G_TRW = nullptr;      // 지금 진행하는 상태의 트리거 (쓰기용)
struct Xf { double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0, rot = 0, sx = 1, sy = 1; bool on = true, lin = false; };
// 진행 중인 이동이 지금까지 옮긴 만큼 (끝나면 GS.mt로 넘어감 → 진행 중엔 그룹 상태를 틱마다 안 고쳐도 됨)
static thread_local const double* G_RUNX = nullptr; static thread_local const double* G_RUNY = nullptr;   // 미리 더해 둔 진행 중 이동 (물체 위치 계산 때만)
static inline void run_part(const TrigRun& R, int g, double& x, double& y) {
    if (G_RUNX) { x += G_RUNX[g]; y += G_RUNY[g]; return; }
    for (auto& a : R.acts) if ((a.kind == 1 || a.kind == 7) && a.gi == g) { x += a.px; y += a.py; }
}
static Xf group_xf(const TrigRun& R, const std::vector<int>& dg) {
    Xf X; int best = 0; bool pure = true;
    for (int g : dg) { const GS& s = R.gs.get(g); if (s.a != 1 || s.b != 0 || s.c != 0 || s.d != 1) { pure = false; break; } }
    if (pure) {                                                       // 이동만: 예전처럼 더하기만
        for (int g : dg) { const GS& s = R.gs.get(g); X.tx += s.rtx + s.mtx; X.ty += s.rty + s.mty; run_part(R, g, X.tx, X.ty); X.rot += s.rot; X.sx *= s.sx; X.sy *= s.sy; if (s.order > best) { best = s.order; X.on = s.on; } }
        return X;
    }
    X.lin = true;
    for (int g : dg) {                                                // 여러 그룹이면 차례로 (추정)
        const GS& s = R.gs.get(g);
        double a = s.a * X.a + s.b * X.c, b = s.a * X.b + s.b * X.d, c = s.c * X.a + s.d * X.c, d = s.c * X.b + s.d * X.d;
        double rx = 0, ry = 0; run_part(R, g, rx, ry);
        double tx = s.a * X.tx + s.b * X.ty + s.rtx + s.mtx + rx, ty = s.c * X.tx + s.d * X.ty + s.rty + s.mty + ry;
        X.a = a; X.b = b; X.c = c; X.d = d; X.tx = tx; X.ty = ty; X.rot += s.rot; X.sx *= s.sx; X.sy *= s.sy;
        if (s.order > best) { best = s.order; X.on = s.on; }
    }
    return X;
}
static inline void xf_pt(const Xf& X, double x, double y, double& ox, double& oy) {
    if (!X.lin) { ox = x + X.tx; oy = y + X.ty; return; }
    ox = X.a * x + X.b * y + X.tx; oy = X.c * x + X.d * y + X.ty;
}
// 지금 질문하는 플레이어 x (파이썬 L.cur_x). NaN = 원래 자리
static thread_local double G_CURX = std::numeric_limits<double>::quiet_NaN();
static thread_local std::vector<Obj> G_MV; static thread_local double G_MVX = std::numeric_limits<double>::quiet_NaN();
static thread_local const Level* G_MVL = nullptr;
static thread_local std::vector<unsigned> G_MVST; static thread_local unsigned G_EPOCH = 1;   // 물체마다 "이 x에서 계산했나" 표시
static thread_local std::unordered_map<int, std::vector<std::pair<double, double>>> G_GCACHE;
static thread_local uint64_t G_MVVER = 0;
struct CluC { Xf X; double bx0, bx1, by0, by1; };
static thread_local std::vector<unsigned> G_CLST; static thread_local std::vector<CluC> G_CLX;   // 묶음(같은 그룹들) 변환·상자 캐시
// 지금 x가 바뀌었으면 새 판 (물체는 필요할 때만 계산 = 결과는 예전처럼 전부 계산한 것과 같음)
static void ensure_moved(const Level& L) {
    if (L.strig) {
        // 최근 위치 판 4개를 기억: 탐색이 갈래 사이를 오가도 다시 계산 안 함
        struct MvSlot { uint64_t ver = 0; unsigned epoch = 0; unsigned long long used = 0; bool valid = false;
                        std::vector<Obj> mv; std::vector<unsigned> mvst, clst; std::vector<CluC> clx; };
        static thread_local MvSlot SL[4]; static thread_local int cur = -1; static thread_local unsigned long long use = 0; static thread_local unsigned epc = 1;
        uint64_t v = G_TR ? G_TR->ver : 0;
        if (G_MVL == &L && G_MVVER == v && G_MV.size() == L.mover_uid.size()) return;
        size_t n = L.mover_uid.size();
        if (G_MVL != &L || G_MV.size() != n) { for (auto& sl : SL) sl.valid = false; cur = -1; G_MV.clear(); }
        auto swap_in = [&](MvSlot& sl) { std::swap(sl.mv, G_MV); std::swap(sl.mvst, G_MVST); std::swap(sl.clst, G_CLST); std::swap(sl.clx, G_CLX); };
        if (cur >= 0) { swap_in(SL[cur]); SL[cur].epoch = G_EPOCH; }    // 지금 판을 제자리로
        int k = -1;
        for (int i = 0; i < 4; ++i) if (SL[i].valid && SL[i].ver == v) k = i;
        if (k < 0) {
            k = 0; for (int i = 1; i < 4; ++i) if (!SL[i].valid || (SL[k].valid && SL[i].used < SL[k].used)) k = i;
            MvSlot& sl = SL[k]; sl.valid = true; sl.ver = v; sl.epoch = ++epc + 0x40000000u;
            if (sl.mv.size() != n) { sl.mv.resize(n); for (size_t i = 0; i < n; ++i) sl.mv[i] = L.objs[L.mover_uid[i]]; sl.mvst.assign(n, 0); sl.clst.assign(L.clu.size(), 0); sl.clx.assign(L.clu.size(), CluC()); }
        }
        swap_in(SL[k]); G_EPOCH = SL[k].epoch; SL[k].used = ++use; cur = k;
        G_MVL = &L; G_MVVER = v;
        return;
    }
    if (G_MVL == &L && (G_MVX == G_CURX || (std::isnan(G_MVX) && std::isnan(G_CURX))) && G_MV.size() == L.mover_uid.size()) return;
    if (G_MVL != &L || G_MV.size() != L.mover_uid.size()) {
        G_MV.resize(L.mover_uid.size()); G_MVST.assign(L.mover_uid.size(), 0);
        for (size_t i = 0; i < L.mover_uid.size(); ++i) G_MV[i] = L.objs[L.mover_uid[i]];
    }
    G_MVL = &L; G_MVX = G_CURX; ++G_EPOCH; G_GCACHE.clear();
}
static const CluC& clu_c(const Level& L, int ci) {
    if (G_CLST[ci] != G_EPOCH) {
        G_CLST[ci] = G_EPOCH; CluC& c = G_CLX[ci]; const Level::Clu& C = L.clu[ci];
        c.X = G_TR ? group_xf(*G_TR, C.dg) : Xf(); const Xf& X = c.X;
        if (!X.lin) { c.bx0 = C.minx + X.tx; c.bx1 = C.maxx + X.tx; c.by0 = C.miny + X.ty; c.by1 = C.maxy + X.ty; }
        else {
            double xs[4], ys[4]; xf_pt(X, C.minx, C.miny, xs[0], ys[0]); xf_pt(X, C.maxx, C.miny, xs[1], ys[1]); xf_pt(X, C.maxx, C.maxy, xs[2], ys[2]); xf_pt(X, C.minx, C.maxy, xs[3], ys[3]);
            c.bx0 = *std::min_element(xs, xs + 4); c.bx1 = *std::max_element(xs, xs + 4); c.by0 = *std::min_element(ys, ys + 4); c.by1 = *std::max_element(ys, ys + 4);
        }
        double pd = (X.lin || X.rot != 0 || X.sx != 1 || X.sy != 1) ? C.pad * std::max({1.0, std::fabs(X.sx), std::fabs(X.sy)}) : 1.0;
        c.bx0 -= pd; c.bx1 += pd; c.by0 -= pd; c.by1 += pd;
    }
    return G_CLX[ci];
}
static inline const Xf& clu_xf(const Level& L, int ci) { return clu_c(L, ci).X; }
// 물체 하나를 변환 X로 옮기고 돌림 (상태 트리거)
static void place_obj(const Obj& o0, const Xf& X, Obj& c) {
    Obj scaled; const Obj* op = &o0;
    if (X.sx != 1.0 || X.sy != 1.0) {                                   // 크기 트리거: 물체 자기 중심 기준으로 늘리고 줄임 (추정)
        scaled = o0; Obj& q = scaled;
        q.w = o0.w * std::fabs(X.sx); q.h = o0.h * std::fabs(X.sy);
        q.minx = o0.x + (o0.minx - o0.x) * std::fabs(X.sx); q.maxx = o0.x + (o0.maxx - o0.x) * std::fabs(X.sx);
        q.miny = o0.y + (o0.miny - o0.y) * std::fabs(X.sy); q.maxy = o0.y + (o0.maxy - o0.y) * std::fabs(X.sy);
        for (auto& p : q.corners) { p.x = o0.x + (p.x - o0.x) * X.sx; p.y = o0.y + (p.y - o0.y) * X.sy; }
        q.radius = o0.radius * std::max(std::fabs(X.sx), std::fabs(X.sy));
        op = &scaled;
    }
    const Obj& o = *op;
    c.radius = o.radius;
    double rot = std::fmod(X.rot, 360.0); if (rot < 0) rot += 360.0;
    if (std::fabs(rot - 360.0) < 1e-9) rot = 0.0;
    if (!X.lin && rot == 0.0) {
        double ox = X.tx, oy = X.ty;
        c.x = o.x + ox; c.y = o.y + oy; c.minx = o.minx + ox; c.maxx = o.maxx + ox; c.miny = o.miny + oy; c.maxy = o.maxy + oy;
        c.w = o.w; c.h = o.h; c.orient = o.orient; c.upside = o.upside;
        if (c.corners.size() != o.corners.size()) c.corners = o.corners;
        for (size_t k = 0; k < o.corners.size(); ++k) { c.corners[k].x = o.corners[k].x + ox; c.corners[k].y = o.corners[k].y + oy; }
        c.tp_y = o.id == 747 ? o.tp_y + oy : o.tp_y;
        return;
    }
    double nx, ny; xf_pt(X, o.x, o.y, nx, ny);
    double ddx = nx - o.x, ddy = ny - o.y;
    c.x = nx; c.y = ny; c.w = o.w; c.h = o.h; c.orient = o.orient; c.upside = o.upside;
    c.tp_y = o.id == 747 ? o.tp_y + ddy : o.tp_y;
    double q = rot / 90.0; bool quarter = std::fabs(q - std::round(q)) < 1e-9; int turns = ((int)std::lround(q)) % 4;
    if (rot == 0.0 || o.kind == K_SAW || (o.kind == K_SLOPE && !quarter)) {          // 안 돌림 (톱니는 동그라미, 비스듬한 경사는 못 함)
        c.minx = o.minx + ddx; c.maxx = o.maxx + ddx; c.miny = o.miny + ddy; c.maxy = o.maxy + ddy;
        c.corners = o.corners; for (auto& p : c.corners) { p.x += ddx; p.y += ddy; }
        return;
    }
    double a = rot * 3.141592653589793 / 180.0, co = std::cos(a), si = std::sin(a);
    if (quarter) { co = std::round(co); si = std::round(si); }
    if (o.kind == K_SOLID && !quarter && o.corners.empty()) {                         // 블록: 비스듬히 돌려도 판정 상자는 안 돌아감 (2.206 디컴파일 canRotateFree 기준). 자리만 옮김
        double hw = (o.maxx - o.minx) / 2.0, hh = (o.maxy - o.miny) / 2.0, cx0 = (o.maxx + o.minx) / 2.0 - o.x, cy0 = (o.maxy + o.miny) / 2.0 - o.y;
        double rcx = cx0 * co + cy0 * si, rcy = -cx0 * si + cy0 * co;
        c.minx = nx + rcx - hw; c.maxx = nx + rcx + hw; c.miny = ny + rcy - hh; c.maxy = ny + rcy + hh; c.corners.clear();
        return;
    }
    std::vector<Pt> cs;
    if (!o.corners.empty()) { for (auto& p : o.corners) { double px = p.x - o.x, py = p.y - o.y; cs.push_back({nx + px * co + py * si, ny - px * si + py * co}); } }
    else if (quarter) {                                                               // 90도 단위: 상자 그대로 (가로·세로 바꿈)
        if (turns % 2) { c.w = o.h; c.h = o.w; }
        double hw = (o.maxx - o.minx) / 2.0, hh = (o.maxy - o.miny) / 2.0, cx0 = (o.maxx + o.minx) / 2.0 - o.x, cy0 = (o.maxy + o.miny) / 2.0 - o.y;
        double rcx = cx0 * co + cy0 * si, rcy = -cx0 * si + cy0 * co;
        if (turns % 2) std::swap(hw, hh);
        c.minx = nx + rcx - hw; c.maxx = nx + rcx + hw; c.miny = ny + rcy - hh; c.maxy = ny + rcy + hh; c.corners.clear();
        if (o.kind == K_SLOPE) c.orient = (o.orient + turns) % 4;
        if ((o.kind == K_ORB || o.kind == K_PAD) && turns == 2) c.upside = !o.upside;
        return;
    } else {
        double hw = o.w / 2.0, hh = o.h / 2.0;
        for (auto& p : std::vector<Pt>{{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}}) cs.push_back({nx + p.x * co + p.y * si, ny - p.x * si + p.y * co});
    }
    c.corners = cs;
    c.minx = c.maxx = cs[0].x; c.miny = c.maxy = cs[0].y;
    for (auto& p : cs) { c.minx = std::min(c.minx, p.x); c.maxx = std::max(c.maxx, p.x); c.miny = std::min(c.miny, p.y); c.maxy = std::max(c.maxy, p.y); }
    if ((o.kind == K_ORB || o.kind == K_PAD) && quarter && turns == 2) c.upside = !o.upside;
}
// 영역 효과 (디컴파일 processAreaMoveGroupAction): 물체가 중심에서 얼마나 먼가로 옮기기·돌리기·크기 (난수 흔들림 0, 들어오는/나가는 이징 구분 없음 = 추정)
static void apply_areas(const Level& L, const TrigRun& R, const std::vector<int>& groups, Obj& c) {
    double mx = 0, my = 0, rot = 0, scx = 1, scy = 1; bool any = false;
    for (auto& A : R.areas) {
        if (!A.has_c || A.str == 0) continue;
        const Level::Trg& t = L.trg[A.ti];
        auto P = [&](int key, double d = 0.0) { return (A.et >= 0 && L.trg[A.et].has(key)) ? L.trg[A.et].k(key) : t.k(key, d); };
        int tg = (int)t.k(51); if (std::find(groups.begin(), groups.end(), tg) == groups.end()) continue;
        double ox = c.x, oy = c.y, dist, sgn;
        int kid = t.id >= 3017 ? t.id - 11 : t.id;                  // 들어올 때 효과(3017~3019) = 영역 효과와 같은 식, 중심 = 화면 오른쪽 끝(추정)
        int dir = t.id >= 3017 ? 1 : (int)P(262);
        double mb = P(264, 1.0), mf = P(263, 1.0);
        if (t.id >= 3017) { double d = A.cx - ox; dist = d; sgn = d; }   // 들어올 때: 화면 안쪽으로 들어온 거리 (멀리 들어올수록 원래 자리)
        else if (dir == 1) { double d = ox - A.cx; dist = d >= 0 ? d * mb : d * mf; sgn = d; }
        else if (dir == 2) { double d = oy - A.cy; dist = d >= 0 ? d * mb : d * mf; sgn = d; }
        else { dist = std::hypot(ox - A.cx, oy - A.cy); sgn = oy - A.cy; }
        (void)sgn;
        double len = (double)(int)P(222); if (len <= 0) continue;
        double dz = P(282), r = dz == 0 ? dist / len : (dist / len - dz) / (1.0 - dz);
        r = std::max(0.0, std::min(1.0, r));
        if (P(276) != 0) r = 1.0 - r;
        if (r >= 1.0) continue;
        double e = ease((int)P(242), P(243, 2.0), r), k = (1.0 - e) * A.str;
        if (kid == 3006) {
            if (P(241) != 0) { mx += k * P(237); my += k * P(239); }
            else {
                double md = P(218); if (md == 0) continue;
                double dx, dy;
                if (P(287) != 0) { double vx = ox - A.cx, vy = oy - A.cy, mag = std::hypot(vx, vy); double rf = std::max(0.0, P(288)); if (rf > mag) md *= mag / rf; dx = mag > 0 ? vx / mag : 0; dy = mag > 0 ? vy / mag : 0; }
                else { double an = (P(231) - 90.0) * 0.017453; dx = std::cos(an); dy = std::sin(an); }
                mx += k * md * dx; my += k * md * dy;
            }
        } else if (kid == 3007) rot += k * P(270);
        else if (kid == 3008) { scx *= 1.0 + k * (P(233, 1.0) - 1.0); scy *= 1.0 + k * (P(235, 1.0) - 1.0); }
        any = true;
    }
    if (!any) return;
    if (rot != 0 || scx != 1 || scy != 1) { Obj b = c; Xf Y; Y.rot = rot; Y.sx = scx; Y.sy = scy; place_obj(b, Y, c); }
    if (mx != 0 || my != 0) {
        c.x += mx; c.y += my; c.minx += mx; c.maxx += mx; c.miny += my; c.maxy += my;
        for (auto& q : c.corners) { q.x += mx; q.y += my; }
        if (c.id == 747) c.tp_y += my;
    }
}
static void compute_mover(const Level& L, size_t i) {
    if (G_MVST[i] == G_EPOCH) return;
    G_MVST[i] = G_EPOCH;
    const Obj& o = L.objs[L.mover_uid[i]]; Obj& c = G_MV[i];
    if (L.strig) {
        place_obj(o, clu_xf(L, L.mover_clu[i]), c);
        c.vy = 0;
        if (G_TR && !G_TR->gdy.empty()) { const auto& dg = L.clu[L.mover_clu[i]].dg; for (auto& q : G_TR->gdy) if (std::find(dg.begin(), dg.end(), q.first) != dg.end()) c.vy += q.second; }
        if (G_TR && !G_TR->areas.empty()) apply_areas(L, *G_TR, L.mover_groups[i], c);
        return;
    }
    double ox = 0, oy = 0;
    if (!std::isnan(G_CURX)) {
        for (int g : L.mover_groups[i]) {
            auto it = G_GCACHE.find(g);
            if (it == G_GCACHE.end()) { std::vector<std::pair<double, double>> parts; group_parts(L, g, G_CURX, parts); it = G_GCACHE.emplace(g, std::move(parts)).first; }
            for (auto& pq : it->second) { ox += pq.first; oy += pq.second; }     // 더하는 순서 예전과 같음
        }
    }
    if (ox != 0 || oy != 0) {
        c.x = o.x + ox; c.y = o.y + oy; c.minx = o.minx + ox; c.maxx = o.maxx + ox; c.miny = o.miny + oy; c.maxy = o.maxy + oy;
        for (size_t k = 0; k < o.corners.size(); ++k) { c.corners[k].x = o.corners[k].x + ox; c.corners[k].y = o.corners[k].y + oy; }
        c.tp_y = o.id == 747 ? o.tp_y + oy : o.tp_y;
    } else {
        c.x = o.x; c.y = o.y; c.minx = o.minx; c.maxx = o.maxx; c.miny = o.miny; c.maxy = o.maxy;
        for (size_t k = 0; k < o.corners.size(); ++k) c.corners[k] = o.corners[k];
        c.tp_y = o.tp_y;
    }
}
// uid로 물체 찾기: 움직이는 물체면 지금 x로 옮긴 복사본
// 세계 물체 → 로컬 물체 (좌표 바뀐 동안만, 캐시)
static void frame_obj(const Frame& F, const Obj& w, Obj& o) {
    o = w;
    double xs[4], ys[4];
    F.to_local(w.minx, w.miny, xs[0], ys[0]); F.to_local(w.maxx, w.miny, xs[1], ys[1]); F.to_local(w.maxx, w.maxy, xs[2], ys[2]); F.to_local(w.minx, w.maxy, xs[3], ys[3]);
    o.minx = *std::min_element(xs, xs + 4); o.maxx = *std::max_element(xs, xs + 4); o.miny = *std::min_element(ys, ys + 4); o.maxy = *std::max_element(ys, ys + 4);
    F.to_local(w.x, w.y, o.x, o.y);
    for (auto& p : o.corners) F.to_local(p.x, p.y, p.x, p.y);
    if (F.b != 0) { o.w = w.h; o.h = w.w; }
    if (w.kind == K_SLOPE) {                                              // 직각 모서리 방향으로 경사 방향 다시 정함
        static const int CX[4] = {1, -1, -1, 1}, CY[4] = {-1, -1, 1, 1};
        int cx = CX[w.orient & 3], cy = CY[w.orient & 3];
        int lx = F.a * cx + F.c * cy, ly = F.b * cx + F.d * cy;
        o.orient = lx > 0 ? (ly < 0 ? 0 : 3) : (ly < 0 ? 1 : 2);
    }
    if (w.kind == K_ORB || w.kind == K_PAD) { int uy = w.upside ? -1 : 1; int ly = F.d * uy; if (ly != 0) o.upside = ly < 0; }
    if (w.id == 747) { double lx2, ly2; F.to_local(w.x, w.tp_y, lx2, ly2); o.tp_y = ly2; }
}
static inline const Obj& OB(const Level& L, int u) {
    const Obj* w;
    if (!L.mover_uid.empty() && u < (int)L.mover_idx.size() && L.mover_idx[u] >= 0) {
        ensure_moved(L); size_t i = (size_t)L.mover_idx[u]; compute_mover(L, i); w = &G_MV[i];
    } else w = &L.objs[u];
    if (!G_FRM) return *w;
    static thread_local std::vector<Obj> fo; static thread_local std::vector<uint64_t> fst; static thread_local uint64_t fkey = 0, fep = 0;
    uint64_t key = G_FRM->h * 1000003ULL ^ (G_TR ? G_TR->ver : 0);
    if (key != fkey || fo.size() != L.objs.size()) { fkey = key; ++fep; if (fo.size() != L.objs.size()) { fo.assign(L.objs.size(), Obj()); fst.assign(L.objs.size(), 0); } }
    if (fst[u] != fep) { fst[u] = fep; frame_obj(*G_FRM, *w, fo[u]); }
    return fo[u];
}
void Level::add_movers(double minx, double maxx, double miny, double maxy, std::vector<int>& out) const {
    if (mover_uid.empty()) return;
    ensure_moved(*this);
    if (strig) {                                                      // 묶음마다: 꺼졌거나 상자가 안 닿으면 건너뜀
        for (size_t ci = 0; ci < clu.size(); ++ci) {
            const Clu& C = clu[ci];
            const CluC& cc = clu_c(*this, (int)ci); if (!cc.X.on) continue;
            if (!(G_TR && !G_TR->areas.empty()) && !rects_touch(minx, maxx, miny, maxy, cc.bx0, cc.bx1, cc.by0, cc.by1)) continue;   // 영역 효과 중엔 물체마다 확인
            for (int i : C.movers) {
                compute_mover(*this, (size_t)i);
                const Obj& o = G_MV[i];
                if (rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy)) out.push_back(mover_uid[i]);
            }
        }
        return;
    }
    for (size_t i = 0; i < mover_uid.size(); ++i) {
        const Env& e = mover_env[i];
        if (!rects_touch(minx, maxx, miny, maxy, e.minx, e.maxx, e.miny, e.maxy)) continue;   // 갈 수 있는 범위에도 안 닿으면 계산 안 함
        compute_mover(*this, i);
        const Obj& o = G_MV[i];
        if (rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy)) out.push_back(mover_uid[i]);
    }
}
static bool hmover_touch(const Level& L, int which, double a0, double a1, double a2, double a3, double x) {
    for (auto& h : L.hmovers) {
        if (h.which != which) continue;
        double ox = 0, oy = 0;
        if (L.strig) {
            if (!G_TR) continue;
            Xf X = group_xf(*G_TR, h.dg); if (!X.on) continue;
            double cx = (h.r.a0 + h.r.a1) / 2.0, cy = (h.r.a2 + h.r.a3) / 2.0, nx, ny; xf_pt(X, cx, cy, nx, ny); ox = nx - cx; oy = ny - cy;
        } else for (int g : h.groups) group_offset(L, g, x, ox, oy);
        if (rects_touch(a0, a1, a2, a3, h.r.a0 + ox, h.r.a1 + ox, h.r.a2 + oy, h.r.a3 + oy)) return true;
    }
    return false;
}

// ---------------------------------------------------------------- 플레이어
// 쓴 물체 목록: 여러 상태가 같은 목록을 나눠 씀 (바뀔 때만 새로 만듦) → 상태 복사가 빨라짐
struct UsedSet {
    std::shared_ptr<const std::vector<int>> v;
    static const std::vector<int>& none() { static const std::vector<int> e; return e; }
    const std::vector<int>& vec() const { return v ? *v : none(); }
    operator const std::vector<int>&() const { return vec(); }
    std::vector<int>::const_iterator begin() const { return vec().begin(); }
    std::vector<int>::const_iterator end() const { return vec().end(); }
    size_t size() const { return v ? v->size() : 0; }
    int operator[](size_t i) const { return (*v)[i]; }
    bool operator==(const UsedSet& o) const { return v == o.v || vec() == o.vec(); }
    bool operator!=(const UsedSet& o) const { return !(*this == o); }
    void clear() { v.reset(); }
    void set(std::vector<int> x) { v = std::make_shared<const std::vector<int>>(std::move(x)); }
};

struct Player {
    double x = 0, y = 0, last_x = 0, last_y = 0, y_vel = 0, x_vel = 0; int speed = 1; double y_start = 0, gravity = 0;
    bool mini = false, upside_down = false, holding_jump = false, jump_unused = false, can_jump = false, on_ground = false,
         is_jumping = false, holding_release_delayed = false, can_ring_jump = false, touched_ring = false, is_dashing = false;
    double dash_y = 0; bool velocity_uncapped = false;
    UsedSet used;                    // 정렬된 uid (frozenset 대신)
    long long last_flip_tick = -1000000, tick = 0; long long pad_tick = -1;
    int snap_obj = -1; double snap_distance = 0, snap_ox = 0, snap_oy = 0;
    bool is_on_slope = false, was_on_slope = false; int slope = -1; double slope_velocity = 0; long long slope_start_tick = 0;
    bool dead = false; std::string cause; double half = 15.0; int mode_hit = 0;   // 0 = 없음
    int mode = CUBE; double robot_boost_charge = 0; bool robot_boost_invalidated = false;
    double floor = 0.0; bool has_ceiling = false; double ceiling = 0.0;
    bool spider_hold = false; bool has_dual_y_ref = false; double dual_y_ref = 0; int tp_wait = 0;

    bool has_used(int u) const { return std::binary_search(used.begin(), used.end(), u); }
    void add_used(int u) {
        const std::vector<int>& cur = used.vec(); auto it = std::lower_bound(cur.begin(), cur.end(), u);
        if (it != cur.end() && *it == u) return;
        std::vector<int> n; n.reserve(cur.size() + 1); n.insert(n.end(), cur.begin(), it); n.push_back(u); n.insert(n.end(), it, cur.end()); used.set(std::move(n));
    }

    void set_speed(int s) { speed = s; const SpeedRow& r = SPEED_TABLE[s]; x_vel = f32(r.sp * r.mul); y_start = r.ys; gravity = r.g; }
    void refresh_size() { double base = mode == WAVE ? WAVE_SIZE : (mode == SPIDER && F.spider27 && x >= F.spider27_x ? SPIDER_SIZE_C : PLAYER_SIZE); half = base * (mini ? MINI_SCALE : 1.0) / 2.0; }
    double flip_mod() const { return upside_down ? -1.0 : 1.0; }
    bool is_falling_bugged() const {
        double thr = gravity + gravity;
        if (mode == SWING || F.fix_gravity_bug) return upside_down ? y_vel > -thr : y_vel < thr;
        if (G_DFF && G_INDUAL) thr = -thr;   // 옛 물리: 듀얼 중이면 기준 부호 뒤집힘 (2.2081 디컴파일 playerIsFallingBugged)
        return upside_down ? y_vel > thr : y_vel < thr;
    }
    void die(const std::string& c) { if (!dead) { dead = true; cause = c; } }
    bool flip_gravity(bool flip) {
        if (upside_down == flip) return false;
        upside_down = flip; last_flip_tick = tick; y_vel *= GRAVITY_FLIP_KEEP; can_jump = false; return true;
    }
    void hit_ground() { y_vel = 0.0; can_jump = true; on_ground = true; is_jumping = false; }
    bool set_mode(int m) {
        int old = mode;
        if (old == m) return false;
        int ms[2] = {old, m};
        for (int q = 0; q < 2; ++q) if (flying(ms[q])) { y_vel *= MODE_SWITCH_KEEP; on_ground = false; can_jump = false; }
        if (m == ROBOT || m == SPIDER) robot_boost_charge = ROBOT_BOOST_MAX;
        if (m == SPIDER && holding_jump && F.spider_entry_hold) spider_hold = true;
        mode = m; refresh_size(); return true;
    }
    void bounds_around(double yy) {
        double h = bounds_h(mode);
        if (h < 0) { floor = 0.0; has_ceiling = false; ceiling = 0.0; }
        else { floor = std::max(0.0, 30.0 * std::floor((yy - h / 2.0) / 30.0)); has_ceiling = true; ceiling = floor + h; }
    }
    double slope_vel_mod() const {
        double on = (double)(tick - slope_start_tick) / 240.0;
        double boost = on < 0.1 ? std::max(0.4, on * 10.0) : 1.0;
        return slope_velocity * boost;
    }
};

// 전역 (파이썬 _HB, _DUAL_SNAP, _GRAV 와 같음)
static thread_local const Level* G_L = nullptr;
static thread_local std::vector<int> G_EVQ;          // 이번 틱 게임 이벤트 (이벤트 트리거용, 디컴파일 GJGameEvent 번호)
static thread_local double G_TWF = 1.0;              // 시간 왜곡 배율 (1보다 클 때만 한 틱 물리가 빨라짐, 추정)
static inline void evt(int e) { if (G_L && G_L->strig) G_EVQ.push_back(e); }
struct DualSnap { bool set = false; Player p; double y = 0; };
static thread_local DualSnap G_SNAP;
static thread_local Player* G_GRAV = nullptr;
static thread_local std::vector<int> QBUF;

static inline void grav_evt(Player& p) { G_GRAV = &p; }
void spider_teleport(Player& p);
void update_jump(Player& p, double dt);

static void push(Player& p) { p.holding_jump = true; p.jump_unused = true; update_jump(p, 0.0); }
static void release(Player& p) { p.holding_jump = false; if (p.is_dashing) p.is_dashing = false; if (p.mode == ROBOT) p.robot_boost_invalidated = true; }
static void spider_jump(Player& p) { spider_teleport(p); p.holding_jump = false; p.spider_hold = true; }

void update_jump(Player& p, double dt) {
    int mode = p.mode; bool up = p.upside_down; double flip = up ? -1.0 : 1.0;
    bool holding = p.holding_jump, mini = p.mini, is_holding = holding;
    if (mode == ROBOT && !p.jump_unused) is_holding = false;
    bool fly = flying(mode);
    double g = (fly || mode == BALL || mode == SPIDER) ? GRAVITY_FLYING : p.gravity;
    double g0 = g;
    if (G_L && !G_L->gm_x.empty()) g = g * grav_mod_at(*G_L, p.x);
    if (G_L && G_L->strig && G_TR) g = g * G_TR->grav;   // 상태 트리거: 중력 트리거 배율 (추정)
    if (G_INDUAL && mode == SHIP && G_DSG != 1.0 && p.x >= G_DSG_X0) g = g * G_DSG;
    double scale_mod = mini ? MINI_JUMP_MOD : 1.0;
    if (fly) {
        if (mini) scale_mod = MINI_FLY_MOD;
        double max_y = FLY_MAX_UP / scale_mod, min_y = -FLY_MAX_DOWN / scale_mod;
        double v = p.y_vel;
        bool recap = !up ? ((0.0 <= v && v < max_y) || (min_y < v && v <= 0.0)) : ((-max_y < v && v <= 0.0) || (0.0 <= v && v < -min_y));
        if (recap) p.velocity_uncapped = false;
        if (mode == SHIP) {
            double sm;
            if (holding && !p.velocity_uncapped) sm = SHIP_THRUST;
            else {
                if (p.velocity_uncapped) sm = up ? (p.y_vel > 0.0 ? SHIP_THRUST : SHIP_FALL) : (p.y_vel < 0.0 ? SHIP_THRUST : SHIP_FALL);
                else sm = SHIP_FALL;
                if (!holding && !p.is_falling_bugged()) sm = SHIP_FALL_RISING;
            }
            double bm = (holding && p.is_falling_bugged()) ? SHIP_BOOST_HOLD_FALLING : SHIP_BOOST;
            p.y_vel = round3(p.y_vel - ((sm >= 0 ? g : g0) * dt * flip * sm * bm) / scale_mod);
            if (holding) p.on_ground = false;
        } else if (mode == UFO) {
            if (p.jump_unused && holding) {
                p.jump_unused = false;
                double jump = (mini ? UFO_JUMP_MINI : UFO_JUMP) * flip * scale_mod;
                if ((up && jump < p.y_vel) || (!up && jump > p.y_vel)) {
                    p.y_vel = round3(jump);
                    if ((p.was_on_slope || p.is_on_slope) && p.slope_velocity > 0.0) {
                        double mx = p.y_vel * SLOPE_JUMP_CAP;
                        p.y_vel = round3(p.y_vel + p.slope_velocity * 0.5); p.y_vel = round3(std::min(p.y_vel, mx));
                    }
                }
            }
            double bmod = p.is_falling_bugged() ? UFO_GRAV_FALL : UFO_GRAV_RISE;
            p.y_vel = round3(p.y_vel - (g * dt * flip * bmod * 0.5) / scale_mod);
            if (holding) p.on_ground = false;
        } else if (mode == WAVE) {
            p.y_vel = round3(p.x_vel * flip * (holding ? 1.0 : -1.0) * ((p.mini && F.mini_wave_vel2) ? 2.0 : 1.0));
        } else {   // swing
            if (p.jump_unused && holding) {
                p.jump_unused = false;
                double before = p.y_vel;
                p.flip_gravity(!up);
                p.y_vel = round3(before * SWING_FLIP_KEEP);
                up = p.upside_down; flip = -flip;
            }
            double smod = mini ? SWING_GRAV_MINI : SWING_GRAV;
            p.y_vel = round3(p.y_vel - smod * (g * dt * flip));
            if (holding) p.on_ground = false;
            scale_mod = 1.0;
        }
        double mode_scale = mode == SWING ? 1.0 : FLY_MAX_DOWN / FLY_MAX_UP;
        if (!p.velocity_uncapped && mode != WAVE) {
            if (!up) { p.y_vel = round3(std::max(p.y_vel, mode_scale * -8.0 / scale_mod)); p.y_vel = round3(std::min(p.y_vel, 8.0 / scale_mod)); }
            else { p.y_vel = round3(std::max(p.y_vel, -8.0 / scale_mod)); p.y_vel = round3(std::min(p.y_vel, mode_scale * 8.0 / scale_mod)); }
        }
        if (p.is_falling_bugged()) p.is_jumping = false;
    } else {
        double gs = (mode == BALL || mode == SPIDER) ? GRAV_SCALE_BALL : mode == ROBOT ? GRAV_SCALE_ROBOT : 1.0;
        if (is_holding && p.can_jump) {
            if (mode == SPIDER && !p.is_dashing) spider_jump(p);
            else if (!p.is_dashing) {
                p.is_jumping = true; p.on_ground = false; p.can_jump = false; p.jump_unused = false;
                p.robot_boost_invalidated = false; p.robot_boost_charge = 0.0;
                double jump = p.y_start * (mode == ROBOT ? ROBOT_JUMP_MOD : 1.0);
                p.y_vel = round3(jump * flip * scale_mod);
                if ((p.was_on_slope || p.is_on_slope) && p.slope_velocity * flip > 0.0) {
                    double thr = p.y_vel * SLOPE_JUMP_CAP;
                    double sv = mode == BALL ? p.slope_velocity : p.slope_vel_mod();
                    p.y_vel = round3(p.y_vel + sv * SLOPE_JUMP_SHARE);
                    p.y_vel = round3(!up ? std::min(p.y_vel, thr) : std::max(p.y_vel, thr));
                }
                if (mode == BALL) { p.flip_gravity(!up); p.y_vel *= BALL_SWITCH_KEEP; p.holding_jump = false; p.spider_hold = true; }   // 뒤집은 누름은 다 씀: 떼기 전엔 다시 안 누른 걸로 (gdplay.py BALL_ONE_FLIP)
            }
        } else if (p.is_jumping) {
            if (mode == ROBOT && holding && !p.robot_boost_invalidated) {
                if (p.robot_boost_charge < ROBOT_BOOST_MAX) {
                    p.robot_boost_charge += dt * ROBOT_BOOST_RATE;
                    p.y_vel = round3(p.y_vel + g * dt * flip * gs);
                }
            }
            p.y_vel = round3(p.y_vel - gs * g * dt * flip);
            if (p.is_falling_bugged()) { p.is_jumping = false; p.on_ground = false; }
        } else {
            if (p.is_falling_bugged()) p.can_jump = false;
            p.y_vel = round3(p.y_vel - gs * g * dt * flip);
            p.y_vel = round3(!up ? std::max(p.y_vel, -CUBE_TERMINAL) : std::min(p.y_vel, CUBE_TERMINAL));
            if (p.is_falling_bugged()) {
                if (up ? (p.y_vel > 4.0) : (p.y_vel < -4.0)) p.on_ground = false;
            }
        }
    }
}

static void update(Player& p) {
    if (p.y_vel > 1000.0) p.y_vel = 1000.0; else if (p.y_vel < -1000.0) p.y_vel = -1000.0;
    p.last_x = p.x; p.last_y = p.y;
    double dts = G_TWF == 1.0 ? DT_SCALED : f32(DT * G_TWF * 0.9), dtx = G_TWF == 1.0 ? DT : DT * G_TWF;
    update_jump(p, dts);
    if (p.is_dashing) p.y_vel = 0.0;
    double dx = p.x_vel * dtx;
    double dy = p.y_vel * dts;
    if (p.mode == WAVE && !p.is_dashing) {
        dy = std::fabs(dx) * p.flip_mod() * (p.holding_jump ? 1.0 : -1.0);
        if (p.mini) dy += dy;
    } else if (p.is_dashing) dy = dx * p.dash_y;
    p.x = f32(p.x + f32(dx)); p.y = f32(p.y + f32(dy));
    p.holding_release_delayed = p.holding_jump;
    p.can_ring_jump = p.jump_unused;
    if (G_SHIP_ORB && p.mode == SHIP) p.jump_unused = false;            // 비행기: 미리 누르고 있던 입력으로는 오브가 안 켜짐 (sacrilegium end 6 실제 매크로 13.8초)
    p.touched_ring = false;
}

static double snap360(double a) {
    a = std::fmod(a, 360.0);
    if (a > 180.0) a -= 360.0; else if (a <= -180.0) a += 360.0;
    return a;
}
// 파이썬 math.fmod 는 C fmod 와 같음
static void start_dash(Player& p, const Obj& o) {
    if (p.is_dashing) return;
    p.is_dashing = true;
    double ring_rot = o.rot + (o.flip_x ? 180.0 : 0.0);
    double angle = snap360(std::fmod(-ring_rot, 360.0));
    if (std::fabs(angle) > 90.0) angle = snap360((angle <= 0.0 ? -180.0 : 180.0) - angle);
    angle = std::max(-DASH_MAX_ANGLE, std::min(DASH_MAX_ANGLE, angle));
    double rad = angle * (M_PI / 180.0);   // math.radians: x * (pi/180)
    p.dash_y = std::sin(rad) / std::fabs(std::cos(rad));
}

static void propel(Player& p, double mod) {
    p.is_jumping = true; p.on_ground = false; p.can_jump = false;
    p.is_on_slope = p.was_on_slope = false;
    double scale_mod = p.mini ? MINI_JUMP_MOD : 1.0;
    p.y_vel = round3(mod * PAD_BASE * p.flip_mod() * scale_mod);
    if (p.mode == BALL || p.mode == SPIDER || p.mode == SWING) p.y_vel *= PAD_BALL_KEEP;
}

// 공 모드에서 누름을 다 써 버리는 오브 종류 (기본: 전부. 단 공중에서 쓴 오브만 — 땅에서 누르며 쓴 오브는 공 뒤집기도 같이 일어남: sac 63.48초. 시험용 환경변수 BALL_ORB_EATS="blue,green,yellow,pink,red,black" 로 바꿀 수 있음)
static unsigned boe_mask() {
    const char* e = std::getenv("BALL_ORB_EATS"); std::string v = e ? e : "yellow,pink,red,blue,green,black"; unsigned m = 0;
    static const char* N[6] = {"yellow", "pink", "red", "blue", "green", "black"};
    for (int i = 0; i < 6; ++i) if (v.find(N[i]) != std::string::npos) m |= 1u << i;
    return m;
}
static const unsigned G_BOE = boe_mask();
static bool ring_jump(Player& p, const Obj& o) {
    if (p.dead || p.has_used(o.uid)) return false;
    if (!p.can_ring_jump || p.is_dashing) return false;
    if (!p.holding_release_delayed) return false;
    if (p.touched_ring) return false;
    bool was_gnd = p.on_ground;                                       // 오브를 쓴 순간 땅에 있었나 (공: 공중에서 쓴 오브만 누름을 다 씀)
    p.add_used(o.uid);
    p.touched_ring = true;
    int kind = o.arg; double scale_mod = p.mini ? MINI_JUMP_MOD : 1.0;
    if (kind == O_SPIDER) {
        if (o.upside != p.upside_down) p.flip_gravity(!p.upside_down);
        spider_teleport(p); p.holding_jump = false;
        if (G_SPORB_HOLD) p.spider_hold = true;      // 게임 원문(spiderTestJumpInternal): 순간이동하면 "누르는 중"을 끔 → 떼고 다시 누르기 전엔 아무 모드에서도 다시 안 눌림
        return true;
    }
    if (kind == O_DASH || kind == O_GDASH) {
        if (kind == O_GDASH) { p.flip_gravity(!p.upside_down); grav_evt(p); }
        start_dash(p, o);
    } else if (kind == O_BLACK) {
        p.jump_unused = false;
        double v;
        if (flying(p.mode)) { v = p.flip_mod() * -ORB_BLACK_FLY; if (p.mode == UFO) v *= ORB_BLACK_UFO_MOD; }
        else { v = p.flip_mod() * -ORB_BLACK; if (p.mode == SPIDER) v *= ORB_BLACK_SPIDER_MOD; }
        p.y_vel = round3(v);
        p.velocity_uncapped = true;
        if (p.mode == BALL || p.mode == SWING) p.holding_jump = false;
        if (p.mode == BALL && !was_gnd && (G_BOE >> 5 & 1)) p.spider_hold = true;
    } else {
        p.jump_unused = false; p.is_jumping = true; p.on_ground = false; p.can_jump = false;
        double vel = p.y_start; int m = p.mode;
        if (kind == O_BLUE) vel *= ORB_BLUE;
        else if (kind == O_GREEN) { if (m == SHIP) vel *= ORB_GREEN_SHIP; }
        else if (kind == O_PINK) vel *= orb_pink_mod(m);
        else if (kind == O_RED) vel *= orb_red_mod(m, p.mini);
        else if (m == ROBOT) vel *= ORB_ROBOT;
        if (kind == O_GREEN) { p.flip_gravity(!p.upside_down); grav_evt(p); }
        vel *= 1.0;   // ORB_OVERRIDE 없음
        p.y_vel = round3(p.flip_mod() * vel * scale_mod);
        if (m == BALL || m == SPIDER) { p.y_vel *= ORB_BALL_SPIDER_KEEP; p.holding_jump = false; }
        if (m == BALL && !was_gnd && kind < 6 && (G_BOE >> kind & 1)) p.spider_hold = true;        // 공: 이 오브에 쓴 누름은 다 씀 → 떼기 전에는 착지해도 안 뒤집힘 (Clubstep 40.07초 실제 기록)
        else if (m == SWING) { p.y_vel *= ORB_SWING_KEEP; p.holding_jump = false; }
        if (kind == O_BLUE) { p.flip_gravity(!p.upside_down); grav_evt(p); }
        if (kind == O_RED) p.velocity_uncapped = true;
    }
    return true;
}

static void snap_jump(Player& p, const Obj& o) {
    if (G_SNAP_CUBE_ONLY && p.mode != CUBE) return;                    // 계단 스냅은 큐브일 때만 (2.206 디컴파일: isInNormalMode일 때만 checkSnapJumpToObject + Deadlocked 56.15초 로봇 실제 기록)
    if (p.snap_obj >= 0 && p.snap_obj != o.uid) {
        struct { double x, y; } prev = {p.snap_ox, p.snap_oy};
        SnapRow r = SNAP_TABLE[p.speed];
        double up_dx = r.up_dx; if (p.mini) up_dx = r.up_dx_mini;
        double tol = r.tol, step = p.flip_mod() * 30.0;
        double ddx = o.x - prev.x, ddy = o.y - prev.y;
        if ((std::fabs(ddx - up_dx) <= tol && std::fabs(ddy - step) <= tol)
            || (std::fabs(ddx - r.down_dx) <= tol && std::fabs(ddy + step) <= tol)
            || (std::fabs(ddx - r.double_dx) <= tol && std::fabs(ddy - 2.0 * step) <= tol)) {
            double fx = o.x + p.snap_distance;
            if (std::fabs(fx - p.x) > tol) fx = fx <= p.x ? p.x - tol : p.x + tol;
            p.x = f32(fx);
        }
    }
    p.snap_obj = o.uid; p.snap_ox = o.x; p.snap_oy = o.y;
    p.snap_distance = p.x - o.x;
}

static bool hd_world(const Level* L, const Player& p, int which) {   // 좌표 바뀐 동안: 플레이어 상자를 세계로 바꿔서 확인
    double hh = p.half, xs[4], ys[4];
    G_FRM->to_world(p.x - hh, p.y - hh, xs[0], ys[0]); G_FRM->to_world(p.x + hh, p.y + hh, xs[1], ys[1]);
    double a0 = std::min(xs[0], xs[1]), a1 = std::max(xs[0], xs[1]), a2 = std::min(ys[0], ys[1]), a3 = std::max(ys[0], ys[1]);
    if (!L->hmovers.empty() && hmover_touch(*L, which, a0, a1, a2, a3, a0)) return true;
    auto& tab = which == 1859 ? L->hblocks : L->dblocks;
    for (long long c = pyidiv(a0, 60.0) - 1; c <= pyidiv(a1, 60.0) + 1; ++c) {
        auto it = tab.find(c); if (it == tab.end()) continue;
        for (auto& r : it->second) if (rects_touch(a0, a1, a2, a3, r.a0, r.a1, r.a2, r.a3)) return true;
    }
    return false;
}
static bool L_HBLOCK(const Player& p) {
    const Level* L = G_L;
    if (!L || (L->hblocks.empty() && L->hmovers.empty())) return false;
    if (G_FRM) return hd_world(L, p, 1859);
    double hh = p.half;
    if (!L->hmovers.empty() && hmover_touch(*L, 1859, p.x - hh, p.x + hh, p.y - hh, p.y + hh, p.x)) return true;
    auto it = L->hblocks.find(pyidiv(p.x, 60.0));
    if (it == L->hblocks.end()) return false;
    for (auto& r : it->second) if (rects_touch(p.x - hh, p.x + hh, p.y - hh, p.y + hh, r.a0, r.a1, r.a2, r.a3)) return true;
    return false;
}
static bool L_DBLOCK(const Player& p) {
    const Level* L = G_L;
    if (!L || (L->dblocks.empty() && L->hmovers.empty())) return false;
    if (G_FRM) return hd_world(L, p, 1755);
    double hh = p.half;
    if (!L->hmovers.empty() && hmover_touch(*L, 1755, p.x - hh, p.x + hh, p.y - hh, p.y + hh, p.x)) return true;
    auto it = L->dblocks.find(pyidiv(p.x, 60.0));
    if (it == L->dblocks.end()) return false;
    for (auto& r : it->second) if (rects_touch(p.x - hh, p.x + hh, p.y - hh, p.y + hh, r.a0, r.a1, r.a2, r.a3)) return true;
    return false;
}

void spider_teleport(Player& p) {
    const Level* L = G_L;
    bool up = p.upside_down;
    double d = up ? -1.0 : 1.0;
    double hh = p.half;
    double edge = p.y + d * hh;
    bool has_best = false; double best = 0; int best_o = -1;
    if (L) {
        double lo, hi;
        if (d > 0) { lo = edge; hi = edge + 3000.0; } else { lo = edge - 3000.0; hi = edge; }
        G_CURX = p.x;
        std::vector<int> q; L->query(p.x - hh + 1.0, p.x + hh - 1.0, lo, hi, q);
        for (int u : q) {
            const Obj& o = OB(*L, u);
            if (!(o.kind == K_SOLID || o.kind == K_HAZARD || o.kind == K_SAW || o.kind == K_PAD)) continue;
            if (F.spider_ray_solid && (o.kind == K_HAZARD || o.kind == K_SAW)) continue;
            if (o.maxx < p.x - hh + 1.0 || o.minx > p.x + hh - 1.0) continue;
            double face = o.kind == K_PAD ? (d > 0 ? o.maxy : o.miny) : (d > 0 ? o.miny : o.maxy);
            double dist = (face - edge) * d;
            if (dist >= -1e-3 && (!has_best || dist < best)) { has_best = true; best = dist; best_o = u; }
        }
    }
    bool has_bound = d > 0 ? p.has_ceiling : true;
    double bound = d > 0 ? p.ceiling : p.floor;
    double bdist = has_bound ? (bound - edge) * d : 0;
    p.flip_gravity(!up);
    p.is_jumping = false;
    if (has_best && (!has_bound || best <= bdist)) {
        p.y = f32(edge + d * best - d * hh); p.last_y = p.y;
        int k = OB(*L, best_o).kind;
        if (!(k == K_SOLID || k == K_PAD)) { p.die("스파이더 순간이동으로 가시에 닿음"); return; }
        p.y_vel = 0.0;
    } else if (has_bound) {
        p.y = f32(bound - d * hh); p.last_y = p.y; p.y_vel = 0.0;
    }
}

static void touch(Player& p, const Obj& o) {
    if (o.kind == K_ORB) {
        if (ring_jump(p, o)) { static const int OE[9] = {34, 35, 36, 37, 38, 39, 41, 42, 43}; evt(8); evt(OE[o.arg]); if (G_L && G_L->rev_uids.count(o.uid) && G_TRW) G_TRW->freq = 1; }
    } else if (o.kind == K_PAD) {
        if (G_PAD_ONE) { if (p.pad_tick == p.tick) return; p.pad_tick = p.tick; }   // 패드는 한 틱에 하나만 (겹친 패드는 다음 틱에: Deadlocked 30.2초 실제 기록)
        { static const int PE[5] = {45, 46, 47, 48, 49}; if (!(o.arg == P_BLUE && o.upside != p.upside_down)) { evt(9); evt(PE[o.arg]); if (G_L && G_L->rev_uids.count(o.uid) && G_TRW) G_TRW->freq = 1; } }
        if (o.arg == P_SPIDER) {
            p.add_used(o.uid); p.robot_boost_invalidated = true;
            if (o.upside != p.upside_down) p.flip_gravity(!p.upside_down);
            spider_teleport(p); p.holding_jump = false; if (G_SPORB_HOLD) p.spider_hold = true;
            return;
        }
        if (o.arg == P_BLUE) {
            if (o.upside != p.upside_down) return;
            p.add_used(o.uid); p.robot_boost_invalidated = true;
            propel(p, PAD_BLUE); p.flip_gravity(!p.upside_down); p.velocity_uncapped = false; grav_evt(p);
        } else {
            p.add_used(o.uid);
            p.robot_boost_invalidated = true;
            double mod = pad_mod(o.arg, p.mode, p.mini);
            propel(p, mod); p.velocity_uncapped = (o.arg == P_RED);
        }
    } else if (o.kind == K_MODE) {
        p.add_used(o.uid);
        if (p.mode_hit != 287) p.mode_hit = o.arg;   // 듀얼 끄기는 같은 틱 다른 포탈에 안 덮임
        if (o.arg == 286 && !G_SNAP.set) {
            G_SNAP.set = true; G_SNAP.p = p; G_SNAP.y = (!F.dual_bounds_player || (G_L && G_L->free_uids.count(o.uid))) ? o.y : p.y;   // 크기·회전 안 바꾼 듀얼 포탈(표시됨)은 포탈 높이 기준
        }
        int m = mode_of(o.arg);
        if (m >= 0) {
            p.set_mode(m); p.bounds_around(o.y);
            if (G_L && G_L->free_uids.count(o.uid)) { p.floor = 0.0; p.has_ceiling = false; p.ceiling = 0.0; }   // 자유 모드
        }
    } else if (o.kind == K_PORTAL) {
        int i = o.arg;
        p.add_used(o.uid);
        if (i == 10 || i == 11) { evt(i == 11 ? 52 : 51); p.flip_gravity(i == 11); grav_evt(p); }
        else if (i == 99 || i == 101) { if (p.mini != (i == 101)) { p.mini = (i == 101); p.refresh_size(); } }
        else if (speed_id(i) >= 0) p.set_speed(speed_id(i));
        else if (i == 747) {
            evt(59); evt(25);
            p.y = f32(o.tp_y); p.last_y = p.y; G_TPED = true;
            if (p.has_ceiling && !G_TP_KEEP) p.bounds_around(p.y);   // 게임은 텔레포트해도 바닥·천장을 그대로 둠 (Deadlocked 37.4초 실제 기록)
        }
    }
}

static void solid_hit(Player& p, const Obj& o);

static void face_hit(Player& p, double minx, double maxx, double miny, double maxy, const Obj& o) {
    Obj fake; fake.uid = o.uid; fake.id = o.id; fake.x = (minx + maxx) / 2; fake.y = (miny + maxy) / 2;
    fake.minx = minx; fake.maxx = maxx; fake.miny = miny; fake.maxy = maxy;
    solid_hit(p, fake);
}

static void solid_hit(Player& p, const Obj& o) {
    if (o.id == 143 && p.has_used(o.uid)) return;                       // 이미 부서진 블록 (gdplay.py BREAKABLE)
    double hh = p.half;
    double pminx = p.x - hh, pmaxx = p.x + hh, pminy = p.y - hh, pmaxy = p.y + hh;
    if (!rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.miny, o.maxy)) return;
    if (!o.corners.empty()) {
        if (sat(o.corners, rect_poly(p.x - hh * INNER, p.x + hh * INNER, p.y - hh * INNER, p.y + hh * INNER))) {
            char b[96]; snprintf(b, sizeof b, "기운 블록 (ID %d, x %.0f)", o.id, o.x); p.die(b);
        }
        return;
    }
    if (p.slope >= 0 && (p.is_on_slope || p.was_on_slope)) {
        const Obj& s = OB(*G_L, p.slope);
        if (rects_touch(o.minx, o.maxx, o.miny, o.maxy, s.x - s.w * 0.6, s.x + s.w * 0.6, s.y - s.h * 0.55, s.y + s.h * 0.55)) {
            double ex = std::min(std::max(o.x, s.minx), s.maxx), r = s.h / s.w;
            bool rises = s.orient == 0 || s.orient == 2;
            double surf = rises ? s.miny + (ex - s.minx) * r : s.maxy - (ex - s.minx) * r;
            if (s.orient >= 2 ? (o.miny >= surf - 2.0) : (o.maxy <= surf + 2.0)) return;
        }
    }
    bool up = p.upside_down;
    bool wave_d = p.mode == WAVE && L_DBLOCK(p);
    bool fly = flying(p.mode);
    double thr = fly ? SNAP_THRESHOLD_FLY : SNAP_THRESHOLD;
    double dy = p.y - p.last_y, yv = p.y_vel, g = up ? -1.0 : 1.0;
    bool lethal = dy * g > 0.0;
    if (p.mode == WAVE && !wave_d) {
        double k = INNER;
        if (rects_touch(p.x - hh * k, p.x + hh * k, p.y - hh * k, p.y + hh * k, o.minx, o.maxx, o.miny, o.maxy)) {
            char b[96]; snprintf(b, sizeof b, "웨이브가 블록에 닿음 (ID %d, x %.0f)", o.id, o.x); p.die(b);
        }
        return;
    }
    bool bumps_head = fly || p.mode == BALL || L_HBLOCK(p);
    if (up && !bumps_head) lethal = true;
    else {
        double low = pminy + thr;
        if (low < o.maxy && low - dy < o.maxy) lethal = true;
        else if (yv > 0.0) lethal = false;
        else {
            p.y = f32(o.maxy + hh);
            if (!up) { snap_jump(p, o); p.hit_ground(); }
            else if (p.y_vel < 0.0) p.y_vel = 0.0;
            return;
        }
    }
    if (lethal && (up || bumps_head)) {
        double high = pmaxy - thr;
        if (!(o.miny < high && o.miny < high - dy)) {
            if (yv >= 0.0) {
                double ny = f32(o.miny - hh);
                if (G_PUSH_HEAD && !up && o.vy < 0.0 && p.y_vel > 0.0) {      // 내려오는 블록에 올라가다 막힘: 속도 = 이번 틱에 실제로 움직인 만큼 (0보다 크면 0)
                    double v = (ny - p.last_y) / (G_TWF == 1.0 ? DT_SCALED : f32(DT * G_TWF * 0.9));
                    p.y = ny; p.y_vel = v < 0.0 ? v : 0.0; return;
                }
                p.y = ny;
                if (up) { snap_jump(p, o); p.hit_ground(); }
                else if (p.y_vel > 0.0) p.y_vel = 0.0;
                return;
            }
            if (G_PUSH_HEAD && !up && o.vy < 0.0) { p.y = f32(o.miny - hh); return; }   // 내려오는 블록이 머리를 누름: 떨어지는 중이어도 딱 붙여 밀어냄 (속도는 그대로)
        }
    }
    bool head;
    if (up) head = (pmaxy - thr >= o.maxy) || (pmaxy - thr - dy >= o.maxy);
    else head = (pminy + thr <= o.miny) || (pminy + thr - dy <= o.miny);
    if (!lethal) return;
    double k = INNER;
    if (!rects_touch(p.x - hh * k, p.x + hh * k, p.y - hh * k, p.y + hh * k, o.minx, o.maxx, o.miny, o.maxy)) return;
    if (head && p.tick - p.last_flip_tick < FLIP_GRACE_TICKS) {
        p.y = up ? f32(o.maxy + hh) : f32(o.miny - hh); p.hit_ground(); p.on_ground = false; return;
    }
    if (o.id == 143) { p.add_used(o.uid); return; }                     // 부서지는 블록: 죽지 않고 부서짐
    char b[96]; snprintf(b, sizeof b, "블록에 박음 (ID %d, x %.0f)", o.id, o.x); p.die(b);
}

static void slope_hit(Player& p, const Obj& o, bool& has_launch, double& launch) {
    double hh = p.half;
    double pminx = p.x - hh, pmaxx = p.x + hh, pminy = p.y - hh, pmaxy = p.y + hh;
    bool attached = p.was_on_slope && p.slope == o.uid;
    bool ceiling = o.orient >= 2;
    if (!attached) {
        bool side = false;
        if ((o.orient == 1 || o.orient == 2) && p.x < o.minx) {
            if (rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.minx + 1.0, o.miny, o.maxy)) {
                face_hit(p, o.minx, o.minx + 1.0, o.miny, o.maxy, o); side = true;
            }
        }
        if (ceiling) {
            if (p.y >= o.maxy && rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.maxy - 1.0, o.maxy)) {
                face_hit(p, o.minx, o.maxx, o.maxy - 1.0, o.maxy, o); side = true;
            }
        } else if (p.y <= o.miny + 1.0 && rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.miny, o.miny + 1.0)) {
            face_hit(p, o.minx, o.maxx, o.miny, o.miny + 1.0, o); side = true;
        }
        if (side || p.dead) return;
        pminy = p.y - hh; pmaxy = p.y + hh;
    }
    if (!rects_touch(pminx, pmaxx, pminy, pmaxy, o.minx, o.maxx, o.miny, o.maxy)) return;
    bool up = p.upside_down; double g = up ? -1.0 : 1.0;
    double ratio = o.h / o.w, angle = std::atan(ratio);
    bool rises = o.orient == 0 || o.orient == 2;
    double surface = rises ? o.miny + (p.x - o.minx) * ratio : o.maxy - (p.x - o.minx) * ratio;
    double radius = hh / std::cos(angle);
    double sd, limit, target;
    if (ceiling) {
        sd = -1.0;
        if (o.hazard_slope) surface -= SLOPE_HAZARD_OFFSET;
        limit = o.miny - hh;
        target = std::min(std::max(surface - radius, limit), o.maxy);
    } else {
        sd = 1.0;
        if (o.hazard_slope) surface += SLOPE_HAZARD_OFFSET;
        limit = o.maxy + hh;
        target = std::max(std::min(surface + radius, limit), o.miny);
    }
    bool inside = sd * p.y < sd * target;
    bool walkable = sd == g;
    bool uphill = !up ? (o.orient == 0) : (o.orient == 3);
    char b[96];
    if (!walkable) {
        if (!inside) return;
        if (o.hazard_slope || (p.mode == WAVE && !G_WAVE_SLOPE)) { snprintf(b, sizeof b, "경사 (x %.0f)", o.x); p.die(b); }
        else if (flying(p.mode) || p.mode == BALL || p.is_dashing) {
            p.y = f32(target);
            if (sd * p.y_vel < 0.0) p.y_vel = 0.0;
            if (G_SU && (flying(p.mode) || p.mode == BALL)) {   // 반대쪽 면도 '경사 위' (2.2081 디컴파일 collidedWithSlopeInternal)
                double ang = std::atan(o.h / o.w);
                double lv = std::min(SLOPE_VEL_FACTOR / ang, SLOPE_VEL_CAP) * (o.h * p.x_vel / o.w) * SLOPE_FLY_BALL_MOD;
                p.slope_velocity = lv * g * (uphill ? 1.0 : -1.0);
                p.can_jump = false; p.on_ground = false;
                if (!p.was_on_slope) p.slope_start_tick = p.tick;
                p.slope = o.uid; p.is_on_slope = true;
            }
        } else if (sd * (target - p.y) <= 2.0) {
            p.y = f32(target); p.y_vel = round3(sd * std::max(sd * p.y_vel, 2.0)); p.can_jump = false; p.on_ground = false;
        } else {
            double ih = hh * INNER;
            double edge = sd < 0 ? p.y + ih : p.y - ih;
            if (sd < 0 ? (edge > surface) : (edge < surface)) { snprintf(b, sizeof b, "경사에 머리 (x %.0f)", o.x); p.die(b); }
        }
        return;
    }
    bool collided = inside;
    if (G_SLOPE_TOP_SKIP && collided && !attached && target == limit && sd * (target - p.y) < 0.5) collided = false;   // 경사 꼭대기 끝(옆 블록 윗면과 같은 높이)에 살짝 걸친 것은 경사에 붙지 않음 → 끝에서 튕기지 않음 (Deadlocked 59.97초 실제 기록)
    if (!collided && attached && !uphill && g * p.y_vel <= 0.0 && p.x <= o.maxx) collided = true;
    if (!collided) return;
    if (o.hazard_slope || (p.mode == WAVE && !G_WAVE_SLOPE)) { snprintf(b, sizeof b, "경사 (x %.0f)", o.x); p.die(b); return; }
    double slope_y_vel = o.h * p.x_vel / o.w;
    double lv = std::min(SLOPE_VEL_FACTOR / angle, SLOPE_VEL_CAP) * slope_y_vel;
    if (flying(p.mode) || p.mode == BALL) lv *= SLOPE_FLY_BALL_MOD;
    p.slope_velocity = lv * g * (uphill ? 1.0 : -1.0);
    if (attached && uphill && target == limit) { has_launch = true; launch = target; return; }
    double before = p.y_vel;
    p.y = f32(target); p.hit_ground();
    if (g * before > 5.0) { p.y_vel = round3(before); p.can_jump = false; }
    if (!p.was_on_slope) p.slope_start_tick = p.tick;
    p.slope = o.uid; p.is_on_slope = true;
}

static void left_slope(Player& p) {
    if (p.slope < 0) return;
    const Obj& o = OB(*G_L, p.slope);
    bool up = p.upside_down;
    if (G_SU && (flying(p.mode) || p.mode == BALL) && (o.orient >= 2 ? -1.0 : 1.0) != (up ? -1.0 : 1.0) && p.x + p.half >= o.maxx) {
        p.y_vel = round3(p.slope_vel_mod()); p.is_jumping = true; p.can_jump = false; p.on_ground = false;   // 반대쪽 면 경사 끝 발사 (추정)
        p.slope = -1; return;
    }
    bool uphill = !up ? (o.orient == 0) : (o.orient == 3);
    if (!uphill && !p.is_jumping && p.x > o.maxx) {
        double g = up ? -1.0 : 1.0;
        p.y_vel = round3(-g * p.x_vel * (o.h / o.w)); p.can_jump = false;
    }
    p.slope = -1;
}

static void bounds(Player& p) {
    bool up = p.upside_down; double hh = p.half;
    double ext = p.mode == WAVE ? hh * 2.0 : hh;
    double fl = p.floor;
    if (G_FRM && !(G_FRM->b == 0 && G_FRM->c == 0 && G_FRM->d == 1 && G_FRM->ty == 0) && !p.has_ceiling) fl = -1e18;   // 게임 회전 중엔 땅 없음 (추정)
    if (p.y - ext <= fl) {
        if (p.mode == WAVE) p.y = f32(fl + ext);
        else if (!up) { if (p.y_vel <= 0.0) { p.y = f32(fl + ext); p.hit_ground(); } }
        else if (p.y - ext < fl) {
            if (!p.has_ceiling) { p.die("뒤집힌 채 땅에 닿음"); return; }
            p.y = f32(fl + ext);
            if (p.y_vel < 0.0) p.y_vel = 0.0;
        }
    }
    if (p.has_ceiling && p.y + ext >= p.ceiling) {
        double ce = p.ceiling;
        if (p.mode == WAVE) p.y = f32(ce - ext);
        else if (up) { if (p.y_vel >= 0.0) { p.y = f32(ce - ext); p.hit_ground(); } }
        else if (p.y + ext > ce) { p.y = f32(ce - ext); if (p.y_vel > 0.0) p.y_vel = 0.0; }
    }
}

static void collide(Player& p, const Level& L) {
    G_L = &L; G_CURX = p.x;
    p.was_on_slope = p.is_on_slope; p.is_on_slope = false;
    double hh = p.half;
    bounds(p);
    if (p.dead) return;
    static thread_local std::vector<int> solids, hazards, qs;          // 틱마다 새로 안 만들고 다시 씀 (빠르게)
    bool has_launch = false; double launch = 0;
    for (int pass = 0; pass < 2; ++pass) {                              // 텔레포트한 틱에는 도착한 자리의 패드·포탈도 같은 틱에 받음 (Deadlocked 30.17초 실제 기록)
    G_TPED = false;
    solids.clear(); hazards.clear();
    L.query(p.x - hh - 30, p.x + hh + 30, p.y - hh - 30, p.y + hh + 30, qs);
    if (F.portal_reverse) std::reverse(qs.begin(), qs.end());
    for (int u : qs) {
        const Obj& o = OB(L, u); int k = o.kind;
        if (k == K_SOLID) solids.push_back(u);
        else if (k == K_HAZARD || k == K_SAW) hazards.push_back(u);
        else if (!p.has_used(u) && !(p.tp_wait > 0 && (k == K_MODE || k == K_PORTAL))) {
            double minx = p.x - hh, maxx = p.x + hh, miny = p.y - hh, maxy = p.y + hh;
            if (k == K_SLOPE) {
                if (rects_touch(minx, maxx, miny, maxy, o.x - o.w, o.x + o.w, o.y - o.h, o.y + o.h)) slope_hit(p, o, has_launch, launch);
            } else if (rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy) &&
                       (o.corners.empty() || sat(o.corners, rect_poly(minx, maxx, miny, maxy)))) {
                touch(p, o);
            }
            if (p.dead) return;
        }
    }
    if (!(G_TP_RECOLLIDE && G_TPED)) break;
    }
    if (has_launch && !p.is_on_slope) {
        p.y = f32(launch); p.y_vel = round3(p.slope_vel_mod());
        p.is_jumping = true; p.can_jump = false; p.on_ground = false; p.slope = -1;
    }
    // solids.sort(key=lambda o: (-(o.x // sec), -o.uid))
    std::stable_sort(solids.begin(), solids.end(), [&](int a, int b) {
        double ka = -pyfloordiv(OB(L, a).x, L.sec), kb = -pyfloordiv(OB(L, b).x, L.sec);
        if (ka != kb) return ka < kb;
        return -a < -b;
    });
    for (int u : solids) { Obj oc = OB(L, u); solid_hit(p, oc); if (p.dead) return; }
    double minx = p.x - hh, maxx = p.x + hh, miny = p.y - hh, maxy = p.y + hh;
    for (int u : hazards) {
        { auto ob = L.off_by.find(u); if (ob != L.off_by.end() && p.has_used(ob->second)) continue; }
        const Obj& o = OB(L, u); bool hit;
        if (o.kind == K_SAW) {
            if (F.fix_radius) hit = std::hypot(p.x - o.x, p.y - o.y) <= o.radius + hh;
            else {
                hit = (minx <= o.x && o.x <= maxx && miny <= o.y && o.y <= maxy);
                if (!hit) {
                    double cx[4] = {maxx, maxx, minx, minx}, cy[4] = {maxy, miny, miny, maxy};
                    for (int i = 0; i < 4 && !hit; ++i) hit = (cx[i] - o.x) * (cx[i] - o.x) + (cy[i] - o.y) * (cy[i] - o.y) < o.radius * o.radius;
                }
            }
        } else {
            hit = rects_touch(minx, maxx, miny, maxy, o.minx, o.maxx, o.miny, o.maxy);
            if (hit && !o.corners.empty()) hit = sat(o.corners, rect_poly(minx, maxx, miny, maxy));
        }
        if (hit) { char b[96]; snprintf(b, sizeof b, "가시·톱니 (ID %d, x %.0f)", o.id, o.x); p.die(b); return; }
    }
    if (p.was_on_slope && !p.is_on_slope) left_slope(p);
}

static bool orb_on_press(Player& p, const Level& L) {
    double hh = p.half;
    G_CURX = p.x;
    std::vector<int> q; L.query(p.x - hh, p.x + hh, p.y - hh, p.y + hh, q);
    for (int u : q) {
        Obj o = OB(L, u);
        if (o.kind == K_ORB && !p.has_used(u) && !p.is_dashing) {
            p.holding_jump = true;
            p.can_ring_jump = true; p.holding_release_delayed = true; p.touched_ring = false;
            if (ring_jump(p, o)) { static const int OE[9] = {34, 35, 36, 37, 38, 39, 41, 42, 43}; evt(8); evt(OE[o.arg]); if (L.rev_uids.count(o.uid) && G_TRW) G_TRW->freq = 1; p.jump_unused = false; return true; }
        }
    }
    return false;
}

static void teleport_trigger(Player& p, const Level& L, bool before) {
    for (auto& t : L.teleports) {
        bool cross = before ? (p.x - 30.0 < t.tx && t.tx <= p.x) : (p.last_x < t.tx && t.tx <= p.x);
        auto gi = L.groups.find(t.g);
        if (cross && gi != L.groups.end()) {
            int key = -1000000 - t.k;
            if (p.has_used(key)) continue;
            p.add_used(key);
            p.x = f32(gi->second.x); p.y = f32(gi->second.y); p.last_x = p.x; p.last_y = p.y;
            p.on_ground = false;
            p.tp_wait = F.tp_portal_delay;
        }
    }
}

// ================================================================ 상태 트리거 진행 (STRIG)
// 한 틱 순서 (예전 x 계산과 같은 때): 플레이어 움직임 → [여기] → 충돌
//   ① 진행 중인 것 1틱 진행 ② x를 지난 트리거 켜기 ③ 닿은 트리거·아이템·토글 오브·터치 버튼 ④ 기다리던 생성
//   ⑤ 회전 → 이동 → 플레이어 Y 따라가기 → 따라가기 순으로 그룹 옮기기 (디컴파일 processMoveActionsStep 순서) ⑥ 충돌 블록
// "켜진 시각"은 소수 틱까지: x로 켜지면 (x - 트리거 x) / 1틱 이동 만큼 이미 지난 것으로 봄 (= 예전 x 계산과 같은 결과)
struct TCtx { TrigRun* R = nullptr; const Level* L = nullptr; Player* p = nullptr; Player* p2 = nullptr; double now = 0, vx = 0, vy = 0; bool changed = false; };
static thread_local TCtx TC;
static thread_local std::vector<int> G_SPAWNED;   // 이 틱에 생성한 그룹 (같은 틱 같은 그룹은 한 번 — 디컴파일 spawnGroup)
static thread_local int G_DEPTH = 0;
static inline uint64_t mix64(uint64_t h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); return h; }

static bool anc_pos(const Level& L, const TrigRun& R, int g, double& x, double& y) {
    auto it = L.anc.find(g); if (it == L.anc.end()) return false;
    Xf X = group_xf(R, it->second.dg); xf_pt(X, it->second.x, it->second.y, x, y); return true;
}
static bool dg_on(const TrigRun& R, const std::vector<int>& dg) {
    int best = 0; bool on = true;
    for (int g : dg) { const GS& s = R.gs.get(g); if (s.order > best) { best = s.order; on = s.on; } }
    return on;
}
static bool group_on(const Level& L, const TrigRun& R, int g) { int i = L.dg_of(g); return i < 0 || R.gs.get(i).on; }
static void trg_pos(const Level& L, const TrigRun& R, const Level::Trg& t, double& x, double& y) {
    if (t.dg.empty()) { x = t.x; y = t.y; return; }
    Xf X = group_xf(R, t.dg); xf_pt(X, t.x, t.y, x, y);
}
static int item_get(const TrigRun& R, int id) {
    auto it = std::lower_bound(R.items.begin(), R.items.end(), std::make_pair(id, INT32_MIN));
    return (it != R.items.end() && it->first == id) ? it->second : 0;
}
static void fire(int ti, double done0);
static void spawn_group(int g, double done0) {
    const Level& L = *TC.L; TrigRun& R = *TC.R;
    if (g <= 0 || !group_on(L, R, g)) return;
    if (std::find(G_SPAWNED.begin(), G_SPAWNED.end(), g) != G_SPAWNED.end()) return;
    G_SPAWNED.push_back(g);
    auto it = L.trg_by_group.find(g); if (it == L.trg_by_group.end()) return;
    for (int ti : it->second) {
        const Level::Trg& t = L.trg[ti];
        if (t.how == 2 && dg_on(R, t.dg)) fire(ti, done0);    // 생성으로 켜지는 트리거만 (디컴파일 spawnObject)
    }
}
static void toggle(int g, bool on) {
    int i = TC.L->dg_of(g); if (i < 0) return;
    if (TC.R->gs.get(i).on == on) return;                       // 이미 그 상태면 아무것도 안 함 (디컴파일 toggleGroup)
    GS& s = TC.R->gs.mut(i); s.order = ++TC.R->order; s.on = on; TC.changed = true;
}
static void toggle_trig(int g, bool on, double done0) { toggle(g, on); if (on) spawn_group(g, done0); }   // 디컴파일 toggleGroupTriggered
static void arm(int ti) {
    auto& v = TC.R->armed; auto it = std::lower_bound(v.begin(), v.end(), ti);
    if (it == v.end() || *it != ti) { v.insert(it, ti); TC.changed = true; }
}
static void item_set(int id, int v) {
    TrigRun& R = *TC.R; const Level& L = *TC.L;
    int old = item_get(R, id);
    auto it = std::lower_bound(R.items.begin(), R.items.end(), std::make_pair(id, INT32_MIN));
    if (it != R.items.end() && it->first == id) { if (v == 0) R.items.erase(it); else it->second = v; }
    else if (v != 0) R.items.insert(it, {id, v});
    TC.changed = true;
    // 개수 트리거: 목표를 넘어가거나 내려오면 (디컴파일 updateCountForItem)
    std::vector<int> arm_copy = R.armed;
    for (int ti : arm_copy) {
        const Level::Trg& t = L.trg[ti];
        if (t.id != 1611 || (int)t.k(80) != id) continue;
        int target = (int)t.k(77);
        if ((v >= target && old < target) || (v <= target && old > target)) {
            if (!t.k(104)) { auto& a = R.armed; auto q = std::lower_bound(a.begin(), a.end(), ti); if (q != a.end() && *q == ti) a.erase(q); }   // 여러 번 아니면 한 번만 (추정)
            toggle_trig((int)t.k(51), t.k(56) != 0, 0.0);
        }
    }
}
// ---- 2.2 아이템·타이머 (디컴파일 activateItemEditTrigger / activateItemCompareTrigger, 키는 gdlib 표)
static double timer_get(const TrigRun& R, int id) { for (auto& t : R.timers) if (t.id == id) return t.t; return 0.0; }
static TrigRun::Tm& timer_mut(TrigRun& R, int id) {
    for (auto& t : R.timers) if (t.id == id) return t;
    R.timers.push_back({id, 0.0, 1.0, false}); return R.timers.back();
}
static double item_value(const TrigRun& R, int type, int id) {   // 1 개수, 2 타이머, 3 점수, 4 시도 횟수(1로 봄), 5 레벨 시간
    switch (type) { case 2: return timer_get(R, id); case 3: return R.points; case 4: return 1.0; case 5: return TC.now / 240.0; default: return item_get(R, id); }
}
static double math_op(int op, double a, double b) { switch (op) { case 1: return a + b; case 2: return a - b; case 3: return a * b; case 4: return b == 0 ? 0.0 : a / b; default: return b; } }
static double math_round(double v, int t) { return t == 1 ? std::round(v) : (t == 2 ? std::floor(v) : (t == 3 ? std::ceil(v) : v)); }
static double math_sign(double v, int t) { return t == 1 ? std::fabs(v) : (t == 2 ? -std::fabs(v) : v); }
static double r3(double v) { return (double)std::round((float)(v * 1000.0)) / 1000.0; }
static void item_assign(int type, int id, double v) {
    if (type == 2) { timer_mut(*TC.R, id).t = v; TC.changed = true; }
    else if (type == 3) { TC.R->points = (int)v; TC.changed = true; }
    else item_set(id, (int)v);
}
static void start_move(int ti, double done0) {
    const Level& L = *TC.L; TrigRun& R = *TC.R; Player& p = *TC.p; const Level::Trg& t = L.trg[ti];
    int g = (int)t.k(51), gi = L.dg_of(g); if (gi < 0) return;
    double dx = t.k(28), dy = t.k(29), D = t.k(10);
    bool lx = t.k(58) != 0 || t.k(141) != 0, ly = t.k(59) != 0;    // 카메라 X 따라가기 = 플레이어 X로 봄 (예전과 같음), 카메라 Y는 못 함
    double mx = t.k(143, 1.0), my = t.k(144, 1.0); if (mx == 0) mx = 1.0; if (my == 0) my = 1.0;
    if (t.k(100) != 0 || t.k(394) != 0) {                         // 목표 위치로 / 방향으로 (2.2)
        double tx = 0, ty = 0, cx = 0, cy = 0; bool okt = true;
        if (t.k(138) != 0) { tx = p.x; ty = p.y; }
        else if (t.k(200) != 0) { Player* q = TC.p2 ? TC.p2 : TC.p; tx = q->x; ty = q->y; }
        else okt = anc_pos(L, R, (int)t.k(71), tx, ty);
        bool okc = anc_pos(L, R, t.k(395) != 0 ? (int)t.k(395) : g, cx, cy);
        dx = dy = 0;
        if (okt && okc) {
            dx = tx - cx; dy = ty - cy;
            if (t.k(394) != 0) { double ln = std::hypot(dx, dy); if (ln > 0) { dx /= ln; dy /= ln; } dx *= t.k(396); dy *= t.k(396); }
            else { int m = (int)t.k(101); if (m == 1) dy = 0; else if (m == 2) dx = 0; }
        }
        lx = ly = false;
    }
    if (t.k(544) != 0) { GS& s = R.gs.mut(gi); s.mtx += dx; s.mty += dy; TC.changed = true; return; }   // 조용히: 바로 도착
    Act a; a.t = ti; a.kind = 1; a.gi = gi; a.done = (G_MOVE_LAG && t.how == 0 && !lx && !ly) ? -1.0 : done0; a.dx = dx; a.dy = dy; a.lx = lx; a.ly = ly; a.mx = mx; a.my = my;   // 가로 위치로 켜진 무브: 틱 안쪽 보간 없이, 지나간 틱의 다음 틱부터 0에서 시작 (Deadlocked 26.6초 실제 기록)
    a.forever = (lx || ly) && D == -1.0;
    a.T = D <= 0 ? 0.0 : D * 240.0; a.easing = (int)t.k(30); a.rate = t.k(85, 2.0);
    a.x0 = t.how == 0 ? t.x : p.x - done0 * TC.vx; a.y0 = p.y - done0 * TC.vy;
    if (a.T == 0 && !a.forever) { GS& s = R.gs.mut(gi); s.mtx += lx ? 0.0 : dx; s.mty += ly ? 0.0 : dy; TC.changed = true; return; }
    R.acts.push_back(a); TC.changed = true;
}
static void fire(int ti, double done0) {
    const Level& L = *TC.L; TrigRun& R = *TC.R; const Level::Trg& t = L.trg[ti];
    if (G_DEPTH > 64) return;                                      // 끝없는 생성 고리 막기 (게임도 1000번 넘으면 멈춤)
    if (t.how != 0 && !t.multi) { if (R.was_fired(ti)) return; R.add_fired(ti); TC.changed = true; }
    ++G_DEPTH;
    int g = (int)t.k(51);
    switch (t.id) {
    case 901: start_move(ti, done0); break;
    case 1346: case 1347: case 1814: {                             // 회전 / 따라가기 / 플레이어 Y 따라가기
        int gi = L.dg_of(g); if (gi < 0) break;
        Act a; a.t = ti; a.gi = gi; a.done = done0; a.easing = (int)t.k(30); a.rate = t.k(85, 2.0);
        double D = t.k(10); a.forever = D < 0; a.T = D <= 0 ? 0.0 : D * 240.0;
        if (t.id == 1346) { a.kind = 2; a.forever = false; a.dx = t.k(68) + 360.0 * t.k(69); a.cg = (int)t.k(71); a.lx = t.k(70) != 0; }
        else if (t.id == 1347) { a.kind = 4; a.cg = (int)t.k(71); a.mx = t.k(72, 1.0); a.my = t.k(73, 1.0); }
        else { a.kind = 3; }
        R.acts.push_back(a); TC.changed = true; break;
    }
    case 1049: toggle(g, t.k(56) != 0); break;
    case 1268: {
        double dl = t.k(63) * 240.0;
        if (dl <= 0) spawn_group(g, done0);
        else { R.spawns.push_back({TC.now - done0 + dl, g, ti}); TC.changed = true; }
        break;
    }
    case 1616: {                                                    // 멈춤: 그룹 안 트리거가 시작한 것 멈추기 (키 580: 1 잠깐 멈춤, 2 다시, 추정)
        int mode = (int)t.k(580); bool by_ctl = t.k(535) != 0;       // 535: 그룹 대신 조작 번호(534)로 고름
        auto in_g = [&](int tj) { if (by_ctl) return (int)L.trg[tj].k(534) == g; auto& gs = L.trg[tj].groups; return std::find(gs.begin(), gs.end(), g) != gs.end(); };
        if (mode == 1 || mode == 2) { for (auto& a : R.acts) if (in_g(a.t)) a.paused = (mode == 1); }
        else {
            for (auto& a : R.acts) if (in_g(a.t) && (a.kind == 1 || a.kind == 7) && (a.px != 0 || a.py != 0)) { GS& s = R.gs.mut(a.gi); s.mtx += a.px; s.mty += a.py; a.px = a.py = 0; }   // 멈춘 자리 그대로
            R.acts.erase(std::remove_if(R.acts.begin(), R.acts.end(), [&](const Act& a) { return in_g(a.t); }), R.acts.end());
            R.spawns.erase(std::remove_if(R.spawns.begin(), R.spawns.end(), [&](const Spn& s) { return in_g(s.t); }), R.spawns.end());
        }
        TC.changed = true; break;
    }
    case 1595: case 1611: case 1815: arm(ti); break;              // 터치·개수·충돌: 켜진 뒤 조건을 기다림
    case 1811: {                                                    // 즉시 개수: 지금 비교 (키 88: 0 같음, 1 큼, 2 작음)
        int v = item_get(R, (int)t.k(80)), target = (int)t.k(77), cmp = (int)t.k(88);
        bool ok = cmp == 1 ? v > target : (cmp == 2 ? v < target : v == target);
        if (ok) toggle_trig(g, t.k(56) != 0, done0);
        break;
    }
    case 1817: {                                                    // 줍기: 아이템 개수 바꾸기 (키 139 덮어쓰기·88 곱/나눔은 추정)
        int id = (int)t.k(80), cnt = (int)t.k(77); if (t.k(78) != 0) cnt = -cnt;
        int v = item_get(R, id);
        if (t.k(139) != 0) v = cnt;
        else if ((int)t.k(88) == 1) v = (int)std::llround(v * t.k(449, 1.0));
        else if ((int)t.k(88) == 2 && t.k(449, 1.0) != 0) v = (int)std::llround(v / t.k(449, 1.0));
        else v += cnt;
        item_set(id, v); break;
    }
    case 1912: {                                                    // 랜덤: 게임의 난수는 못 맞춤 → 확률 50% 이상이면 첫째 그룹 (추정)
        double c = t.k(10); if (c > 1.0) c /= 100.0;                // 0~1 또는 0~100 둘 다 받음
        spawn_group(c >= 0.5 ? g : (int)t.k(71), done0); break;
    }
    case 2066: R.grav = t.k(148, 1.0); TC.changed = true; break;  // 중력 배율 (예전과 같은 키)
    case 2067: {                                                    // 크기: 대상 그룹을 중심 그룹 기준으로 늘리고 줄임 (키 150/151 배율, 153/154 나누기, 133 위치만)
        int gi = L.dg_of(g); if (gi < 0) break;
        Act a; a.t = ti; a.kind = 5; a.gi = gi; a.done = done0; a.easing = (int)t.k(30); a.rate = t.k(85, 2.0);
        double D = t.k(10); a.T = D <= 0 ? 0.0 : D * 240.0;
        double fx = t.k(150, 1.0), fy = t.k(151, 1.0);
        if (t.k(153) != 0 && fx != 0) fx = 1.0 / fx; if (t.k(154) != 0 && fy != 0) fy = 1.0 / fy;
        a.dx = fx; a.dy = fy; a.px = 1.0; a.py = 1.0; a.cg = (int)t.k(71); a.lx = t.k(133) != 0;
        R.acts.push_back(a); TC.changed = true; break;
    }
    case 3619: {                                                    // 아이템 편집
        int tt = std::max(1, (int)t.k(478)), tid = g;
        if (tt != 3 && tid <= 0) break;
        int m1 = (int)t.k(476), m2 = (int)t.k(477), id1 = (int)t.k(80), id2 = (int)t.k(95);
        bool u1 = m1 != 0 && (id1 > 0 || m1 == 3 || m1 == 4), u2 = m2 != 0 && (id2 > 0 || m2 == 3 || m2 == 4);
        double mod = r3(t.k(479, 1.0)), v;
        if (!u1 && !u2) v = mod;
        else {
            double base = u1 && u2 ? math_op(t.k(481) > 0 ? (int)t.k(481) : 1, item_value(R, m1, id1), item_value(R, m2, id2)) : (u1 ? item_value(R, m1, id1) : item_value(R, m2, id2));
            v = math_op((int)t.k(482) == 4 ? 4 : 3, base, mod);
        }
        v = math_sign(math_round(v, (int)t.k(485)), (int)t.k(578));
        if (t.k(480) != 0) { v = math_op((int)t.k(480), item_value(R, tt, tid), v); v = math_sign(math_round(v, (int)t.k(486)), (int)t.k(579)); }
        item_assign(tt, tid, v); break;
    }
    case 3620: {                                                    // 아이템 비교 → 참이면 51, 거짓이면 71 생성
        int r1 = t.k(480) > 0 ? (int)t.k(480) : 3, r2 = t.k(481) > 0 ? (int)t.k(481) : 3, cmp = (int)t.k(482);
        int m1 = std::max(1, (int)t.k(476)), m2 = (int)t.k(477), id2 = (int)t.k(95);
        double tol = r3(t.k(484)), lhs = math_op(r1, item_value(R, m1, (int)t.k(80)), r3(t.k(479, 1.0)));
        lhs = math_sign(math_round(lhs, (int)t.k(485)), (int)t.k(578));
        bool u2 = m2 != 0 && (id2 > 0 || m2 == 3 || m2 == 4);
        double rhs = u2 ? math_op(r2, item_value(R, m2, id2), r3(t.k(483))) : r3(t.k(483));
        rhs = math_sign(math_round(rhs, (int)t.k(486)), (int)t.k(579));
        bool ok = false;
        switch (cmp) { case 0: ok = std::fabs(lhs - rhs) <= tol; break; case 1: ok = lhs + tol > rhs; break; case 2: ok = lhs + tol >= rhs; break;
                       case 3: ok = lhs - tol < rhs; break; case 4: ok = lhs - tol <= rhs; break; case 5: ok = std::fabs(lhs - rhs) > tol; break; }
        spawn_group(ok ? g : (int)t.k(71), done0); break;
    }
    case 3609: {                                                    // 즉시 충돌: 지금 겹쳐 있나 (지난 틱 충돌 목록 기준, 추정)
        int a = t.k(201) != 0 ? -1 : (t.k(138) != 0 ? -1 : (t.k(200) != 0 ? -2 : (int)t.k(80)));
        int b = t.k(201) != 0 ? -2 : (int)t.k(95);
        std::pair<int, int> pr{std::min(a, b), std::max(a, b)};
        bool hit = std::binary_search(R.cols.begin(), R.cols.end(), pr);
        spawn_group(hit ? g : (int)t.k(71), done0); break;
    }
    case 2068: {                                                    // 고급 랜덤: 난수 못 맞춤 → 확률 가장 큰 그룹 (추정)
        const std::vector<double>* lst = t.list(152); if (!lst) break;
        int best = 0; double bc = -1;
        for (size_t i = 0; i + 1 < lst->size(); i += 2) if ((*lst)[i + 1] > bc) { bc = (*lst)[i + 1]; best = (int)(*lst)[i]; }
        spawn_group(best, done0); break;
    }
    case 3618: {                                                    // 리셋: 그 그룹 트리거를 다시 켜질 수 있게 (추정)
        auto it = L.trg_by_group.find(g); if (it == L.trg_by_group.end() || !R.fired) break;
        std::vector<int> v = *R.fired; size_t n0 = v.size();
        for (int tj : it->second) { auto q = std::lower_bound(v.begin(), v.end(), tj); if (q != v.end() && *q == tj) v.erase(q); }
        if (v.size() != n0) { R.fired = std::make_shared<const std::vector<int>>(std::move(v)); TC.changed = true; }
        break;
    }
    case 3614: {                                                    // 타이머 시작 (키 80 번호, 467 시작, 470 배율, 471 멈춘 채, 473 목표, 474 목표에서 멈춤)
        int id = (int)t.k(80); bool exists = false; for (auto& tm : R.timers) if (tm.id == id) exists = true;
        auto& tm = timer_mut(R, id);
        if (!(t.k(468) != 0 && exists)) tm.t = t.k(467);
        tm.mod = t.k(470, 1.0) == 0 ? 1.0 : t.k(470, 1.0); tm.run = t.k(471) == 0;
        if (g > 0) R.tlis.push_back({ti, id, t.k(473), g, false, t.k(474) != 0, false});
        TC.changed = true; break;
    }
    case 3615: R.tlis.push_back({ti, (int)t.k(80), t.k(473), g, t.k(475) != 0, false, false}); TC.changed = true; break;   // 시간 이벤트
    case 3617: timer_mut(R, (int)t.k(80)).run = t.k(472) == 0; TC.changed = true; break;                                  // 타이머 멈춤·다시
    case 3604: arm(ti); break;                                      // 이벤트: 켜진 뒤 이벤트를 기다림
    case 1932: {                                                    // 플레이어 조작 (디컴파일 activatePlayerControlTrigger): 점프 멈춤 = 버튼 뗀 것처럼
        bool p2t = t.k(200) != 0, p1t = t.k(138) != 0 || !p2t;
        if (t.k(540) != 0) {
            if (p1t) { release(*TC.p); R.blk1 = true; }
            if (p2t && TC.p2) { release(*TC.p2); R.blk2 = true; }
            R.sig = mix64(R.sig, (uint64_t)ti * 31 + 5);
        }
        break;                                                      // 움직임 멈춤·미끄러짐 멈춤은 플랫포머용, 회전 멈춤은 모양만
    }
    case 1935: R.tw = t.k(120, 1.0) <= 0 ? 1.0 : t.k(120, 1.0); break;   // 시간 왜곡
    case 1917: R.freq = 1; break;                                   // 방향 반대 (틱 끝에 바꿈)
    case 2900: {                                                    // 게임 회전: 166 = 가는 방향, 167 = 땅 방향 (1 위 2 아래 3 왼쪽 4 오른쪽; 디컴파일 이름 기준, 다른 자료는 반대라 추정)
        auto vec = [](int d, int& x, int& y) { x = d == 3 ? -1 : (d == 4 ? 1 : 0); y = d == 1 ? 1 : (d == 2 ? -1 : 0); };
        int mx, my, gx, gy; vec(t.k(166) != 0 ? (int)t.k(166) : 4, mx, my); vec(t.k(167) != 0 ? (int)t.k(167) : 2, gx, gy);
        if (mx * gx + my * gy != 0 || (mx == 0 && my == 0) || (gx == 0 && gy == 0)) break;   // 서로 직각이 아니면 무시
        R.freq = 2; R.fa = mx; R.fc = my; R.fb = -gx; R.fd = -gy; break;
    }
    case 3016: {                                                    // 고급 따라가기: 대상(71 그룹 또는 플레이어)을 속도·가속으로 쫓아감 (디컴파일 processAdvancedFollowAction, 그룹 첫 물체 기준)
        int gi = L.dg_of(g); if (gi < 0) break;
        Act a; a.t = ti; a.kind = 6; a.gi = gi; a.done = done0; a.forever = true;
        a.src = t.k(138) != 0 ? -1 : (t.k(200) != 0 ? -2 : (int)t.k(71));
        R.acts.push_back(a); break;
    }
    case 3006: case 3007: case 3008: case 3017: case 3018: case 3019: {                              // 영역 효과 켜기 (켜진 동안 계속, 시간(10)은 서서히 세지는 시간으로 봄 = 추정)
        R.areas.push_back({ti, -1, done0, 0, 0, 0, false}); G_GSDIRTY = true; break;
    }
    case 3011: case 3012: case 3013: {                              // 영역 효과 편집: 같은 효과 번호(255 켜짐) 또는 같은 대상 그룹의 효과 설정을 바꿈
        int kind = t.id - 5;
        for (auto& A : R.areas) {
            const Level::Trg& at = L.trg[A.ti]; if (at.id != kind) continue;
            bool m = t.k(255) != 0 ? (int)at.k(225) == g : (int)at.k(51) == g;
            if (m) A.et = ti;
        }
        G_GSDIRTY = true; break;
    }
    case 3023: case 3024: {                                         // 영역 멈춤 / 들어올 때 멈춤: 효과 번호(225)가 51인 것
        size_t n0 = R.areas.size();
        R.areas.erase(std::remove_if(R.areas.begin(), R.areas.end(), [&](const TrigRun::Area& A) { return (int)L.trg[A.ti].k(225) == g; }), R.areas.end());
        if (R.areas.size() != n0) G_GSDIRTY = true;
        break;
    }
    case 3660: case 3661: {                                         // 고급 따라가기 편집 / 대상 바꾸기
        int gi = L.dg_of(g);
        for (auto& a : R.acts) if (a.kind == 6 && a.gi == gi) {
            if (t.id == 3660) a.et = ti;
            else a.src = t.k(138) != 0 ? -1 : (t.k(200) != 0 ? -2 : (int)t.k(71));
        }
        break;
    }
    case 3033: {                                                    // 키프레임 애니메이션: 키프레임 점(그룹 76) 경로를 따라 그룹 51을 옮기고 돌리고 크기 바꿈 (직선 보간, 추정)
        int gi = L.dg_of(g); if (gi < 0) break;
        auto it = L.kf_by_group.find((int)t.k(76)); if (it == L.kf_by_group.end() || it->second.size() < 2) break;
        Act a; a.t = ti; a.kind = 7; a.gi = gi; a.done = done0; a.src = (int)t.k(76); a.cg = (int)t.k(71); a.steer = 0;
        R.acts.push_back(a); break;
    }
    case 3607: {                                                    // 순서: (그룹, 횟수) 목록을 켤 때마다 한 칸씩 (키 436 끝: 0 멈춤 1 반복 2 마지막 유지, 437 최소 간격, 438 리셋 시간, 439 리셋 방식) 추정
        const std::vector<double>* lst = t.list(435); if (!lst || lst->size() < 2) break;
        TrigRun::Seq* sq = nullptr; for (auto& q : R.seqs) if (q.ti == ti) sq = &q;
        if (!sq) { R.seqs.push_back({ti, 0, -1e18}); sq = &R.seqs.back(); }
        double now = TC.now - done0;
        if (t.k(437) > 0 && now - sq->last < t.k(437) * 240.0) break;
        if (t.k(438) > 0 && sq->last > -1e17 && now - sq->last > t.k(438) * 240.0) sq->idx = (int)t.k(439) == 1 ? 0 : std::max(0, sq->idx - 1);
        sq->last = now;
        int total = 0; for (size_t i = 1; i < lst->size(); i += 2) total += std::max(1, (int)(*lst)[i]);
        int idx = sq->idx, mode = (int)t.k(436);
        if (idx >= total) { if (mode == 1) idx %= total; else if (mode == 2) idx = total - 1; else break; }
        int acc = 0, gg = 0;
        for (size_t i = 0; i + 1 < lst->size(); i += 2) { acc += std::max(1, (int)(*lst)[i + 1]); if (idx < acc) { gg = (int)(*lst)[i]; break; } }
        sq->idx = idx + 1;
        spawn_group(gg, done0); break;
    }
    case 3022: {                                                    // 텔레포트: 대상 그룹 첫 물체의 지금 자리로
        double x, y; if (!anc_pos(L, R, g, x, y)) break;
        Player& p = *TC.p;
        p.x = f32(x); p.y = f32(y); p.last_x = p.x; p.last_y = p.y; p.on_ground = false; p.tp_wait = F.tp_portal_delay;
        TC.changed = true; break;
    }
    default: break;
    }
    --G_DEPTH;
}
static void apply_rot(TrigRun& R, const Level& L, int gi, double dth, int cg, bool lock) {
    GS& s = R.gs.mut(gi);
    double cx, cy;
    if (cg > 0 && anc_pos(L, R, cg, cx, cy)) {
        // 새 변환 = 회전(중심 C) ∘ 지금 변환  (CW 양수: x' = x cos + y sin, y' = -x sin + y cos)
        double a = dth * 3.141592653589793 / 180.0, co = std::cos(a), si = std::sin(a);
        GS& m = R.gs.mut(gi);
        double na = co * m.a + si * m.c, nb = co * m.b + si * m.d, nc = -si * m.a + co * m.c, nd = -si * m.b + co * m.d;
        double rpx = 0, rpy = 0; run_part(R, gi, rpx, rpy);
        double tx = m.rtx + m.mtx + rpx - cx, ty = m.rty + m.mty + rpy - cy;
        m.rtx = co * tx + si * ty + cx - m.mtx - rpx; m.rty = -si * tx + co * ty + cy - m.mty - rpy;
        m.a = na; m.b = nb; m.c = nc; m.d = nd;
        if (!lock) m.rot += dth;
        return;
    }
    if (!lock) s.rot += dth;                                        // 중심 없음: 물체마다 제자리에서 돎 (자리 그대로)
}
static void apply_scale(TrigRun& R, const Level& L, int gi, double rx, double ry, int cg, bool only_move) {
    double cx, cy;
    int cgg = cg > 0 ? cg : L.dyn_gid[gi];                          // 중심 그룹이 없으면 대상 그룹 첫 물체 (게임은 그룹 가운데, 추정)
    if (anc_pos(L, R, cgg, cx, cy)) {
        GS& m = R.gs.mut(gi);
        double rpx = 0, rpy = 0; run_part(R, gi, rpx, rpy);
        double tx = m.rtx + m.mtx + rpx - cx, ty = m.rty + m.mty + rpy - cy;
        m.a *= rx; m.b *= rx; m.c *= ry; m.d *= ry;
        m.rtx = rx * tx + cx - m.mtx - rpx; m.rty = ry * ty + cy - m.mty - rpy;
    }
    if (!only_move) { GS& m = R.gs.mut(gi); m.sx *= rx; m.sy *= ry; }
}
static bool box_hit(double ax, double ay, double ahw, double ahh, double bx, double by, double bhw, double bhh) {
    return std::fabs(ax - bx) <= ahw + bhw && std::fabs(ay - by) <= ahh + bhh;
}
static double scale_of(const Level::Trg& t, int axis) { double s = t.k(32, 1.0); if (s == 0) s = 1.0; return t.k(axis == 0 ? 128 : 129, s); }

static void trig_step(TrigRun& R, const Level& L, Player& p, Player* p2, bool pressed) {
    double twf = R.tw > 1.0 ? R.tw : 1.0;                             // 시간 왜곡 1보다 크면 트리거도 그만큼 빨리 (추정)
    R.gextra += twf - 1.0;
    TC.R = &R; TC.L = &L; TC.p = &p; TC.p2 = p2; TC.now = (double)p.tick + R.gextra; TC.changed = false; G_GSDIRTY = false; G_MUTS.clear();
    TC.vx = p.x - p.last_x; TC.vy = p.y - p.last_y;
    G_SPAWNED.clear(); G_DEPTH = 0;
    if (L.fpy_hist > 0) R.yh = std::make_shared<const YNode>(YNode{p.y, R.yh, p.x});
    // 타이머 (1/240초씩 × 배율. 시간 왜곡은 무시)
    if (!R.timers.empty()) {
        TC.changed = true;
        for (auto& tm : R.timers) {
            if (!tm.run) continue;
            double t0 = tm.t; tm.t += tm.mod * twf / 240.0;
            for (auto& l : R.tlis) {
                if (l.done || l.id != tm.id) continue;
                if (t0 < l.target && tm.t >= l.target) {
                    if (!l.multi) l.done = true;
                    if (l.pause) { tm.t = l.target; tm.run = false; }
                    spawn_group(l.g, 0.0);
                }
            }
        }
    }
    // 따라가기 원본 그룹 위치 (이 틱 움직이기 전)
    static thread_local std::vector<std::pair<int, std::pair<double, double>>> fsrc; fsrc.clear();
    for (auto& a : R.acts) if (a.kind == 4) { double x, y; if (anc_pos(L, R, a.cg, x, y)) fsrc.push_back({a.cg, {x, y}}); }
    // ① 1틱 진행
    for (auto& a : R.acts) if (!a.paused) a.done += twf;
    // ② x를 지난 트리거
    while (R.xcur < L.trg_x.size() && L.trg_xs[R.xcur] <= p.x) {
        int ti = L.trg_x[R.xcur++]; TC.changed = true;
        const Level::Trg& t = L.trg[ti];
        if (!dg_on(R, t.dg)) continue;                                // 꺼진 그룹 안 트리거는 안 켜지고 지나감
        double f = TC.vx > 0 ? (p.x - t.x) / TC.vx : 0.0; if (f < 0) f = 0;
        fire(ti, f);
    }
    // ③ 닿으면 켜지는 트리거 (트리거 상자 30x30 × 크기, 추정)
    double hh = p.half;
    auto near = [&](const std::vector<int>& lst, double reach, auto fn) {
        auto lo = std::lower_bound(lst.begin(), lst.end(), p.x - reach, [&](int ti, double v) { return L.trg[ti].x < v; });
        for (auto it = lo; it != lst.end() && L.trg[*it].x <= p.x + reach; ++it) fn(*it);
    };
    near(L.trg_touch, 1000.0, [&](int ti) {
        const Level::Trg& t = L.trg[ti]; if (R.was_fired(ti) || !dg_on(R, t.dg)) return;
        double x, y; trg_pos(L, R, t, x, y);
        if (box_hit(p.x, p.y, hh, hh, x, y, 15.0 * scale_of(t, 0), 15.0 * scale_of(t, 1))) { R.sig = mix64(R.sig, (uint64_t)ti); fire(ti, 0.0); }
    });
    // 아이템 (25x20, 디컴파일 commonInteractiveSetup 크기). 둘째 플레이어도 주움
    near(L.trg_items, 1000.0, [&](int ti) {
        const Level::Trg& t = L.trg[ti]; if (R.was_fired(ti) || !dg_on(R, t.dg)) return;
        double x, y; trg_pos(L, R, t, x, y);
        bool hit = box_hit(p.x, p.y, hh, hh, x, y, 12.5 * scale_of(t, 0), 10.0 * scale_of(t, 1));
        if (!hit && p2) hit = box_hit(p2->x, p2->y, p2->half, p2->half, x, y, 12.5 * scale_of(t, 0), 10.0 * scale_of(t, 1));
        if (!hit) return;
        R.add_fired(ti); R.sig = mix64(R.sig, (uint64_t)ti); TC.changed = true;
        if (t.k(381) != 0 || (int)t.k(79) == 1) { int id = (int)t.k(80); item_set(id, item_get(R, id) + (t.k(78) != 0 ? -1 : 1)); }
        if (t.k(382) != 0 || (int)t.k(79) == 2) toggle_trig((int)t.k(51), t.k(56) != 0, 0.0);
    });
    // 힘 블록(30x30)·힘 원(반지름 15): 닿아 있으면 힘 (디컴파일 calculateForceToTarget). 힘 번호(530)마다 틱당 한 번.
    // 힘이 속도에 들어가는 식은 디컴파일에 없음 → 세로 속도에 그대로 더함 (추정). 가로(속도)는 고전 모드라 안 바꿈
    if (!L.trg_force.empty()) {
        double fx = 0, fy = 0; std::vector<int> used_ids;
        near(L.trg_force, 1000.0, [&](int ti) {
            const Level::Trg& t = L.trg[ti]; if (t.k(121) != 0 || !dg_on(R, t.dg)) return;
            double x, y; trg_pos(L, R, t, x, y);
            double hw = 15.0 * scale_of(t, 0), hy = 15.0 * scale_of(t, 1);
            bool hit = t.id == 3645 ? std::hypot(p.x - x, p.y - y) <= std::max(hw, hy) + hh : box_hit(p.x, p.y, hh, hh, x, y, hw, hy);
            if (!hit) return;
            int fid = (int)t.k(530);
            if (fid > 0) { if (std::find(used_ids.begin(), used_ids.end(), fid) != used_ids.end()) return; used_ids.push_back(fid); }
            double ang;
            if (t.k(528) != 0) { ang = std::atan2(y - p.y, x - p.x) + (t.k(5) != 0 ? 0.0 : 3.141592653589793); }
            else { ang = (90.0 - t.k(6)) * 0.017453; if (t.k(5) != 0) ang += 3.141592653589793; }
            double force = t.k(149);
            if (t.k(529) != 0) {
                double rad = std::max(hw, hy), sc;
                if (t.k(528) != 0) { double d = std::hypot(x - p.x, y - p.y); sc = rad > d ? 1.0 - d / rad : 0.0; }
                else { double dot = (x - p.x) * std::cos(ang) + (y - p.y) * std::sin(ang); sc = std::max(0.0, std::min(1.0, (rad + dot) / rad * 0.5)); }
                force = t.k(526) + (t.k(527) - t.k(526)) * sc;
            }
            fx += std::cos(ang) * force; fy += std::sin(ang) * force;
        });
        if (fy != 0) { p.y_vel += fy; R.sig = mix64(R.sig, 0xf0); }
        (void)fx;
    }
    // 충돌 상태 블록 (30x30): 들어가면 51, 나오면 71 생성 (gdlib 설명)
    if (!L.trg_sblock.empty()) {
        std::vector<int> now_in;
        near(L.trg_sblock, 1000.0, [&](int ti) {
            const Level::Trg& t = L.trg[ti]; if (!dg_on(R, t.dg)) return;
            double x, y; trg_pos(L, R, t, x, y);
            if (box_hit(p.x, p.y, hh, hh, x, y, 15.0 * scale_of(t, 0), 15.0 * scale_of(t, 1))) now_in.push_back(ti);
        });
        std::sort(now_in.begin(), now_in.end());
        if (now_in != R.inside) {
            for (int ti : now_in) if (!std::binary_search(R.inside.begin(), R.inside.end(), ti)) { R.sig = mix64(R.sig, (uint64_t)ti * 19 + 1); spawn_group((int)L.trg[ti].k(51), 0.0); }
            for (int ti : R.inside) if (!std::binary_search(now_in.begin(), now_in.end(), ti)) { R.sig = mix64(R.sig, (uint64_t)ti * 19 + 2); spawn_group((int)L.trg[ti].k(71), 0.0); }
            R.inside = now_in; TC.changed = true;
        }
    }
    // 토글 오브 (36x36): 누른 틱에 닿아 있으면 켜짐. 진짜 오브와 같이 써짐 (디컴파일: 토글 오브 먼저, 그다음 보통 오브)
    bool press_edge = pressed && !R.lastin;
    if (press_edge || (p.holding_jump && p.jump_unused)) {
        Player* ps[2] = {&p, p2};
        near(L.trg_rings, 1000.0, [&](int ti) {
            const Level::Trg& t = L.trg[ti]; if ((R.was_fired(ti) && t.k(99) == 0) || !dg_on(R, t.dg)) return;
            double x, y; trg_pos(L, R, t, x, y);
            for (Player* q : ps) {
                if (!q) continue;
                if (!box_hit(q->x, q->y, q->half, q->half, x, y, 18.0 * scale_of(t, 0), 18.0 * scale_of(t, 1))) continue;
                R.add_fired(ti); R.sig = mix64(R.sig, (uint64_t)ti * 7 + 1); TC.changed = true;
                if (t.k(504) != 0) spawn_group((int)t.k(51), 0.0); else toggle_trig((int)t.k(51), t.k(56) != 0, 0.0);   // 504 = 생성만
                break;
            }
        });
        // 터치 토글 블록 (30x30, 안에서 누르면): 토글 오브와 같게 (gdlib 설명)
        near(L.trg_tblock, 1000.0, [&](int ti) {
            const Level::Trg& t = L.trg[ti]; if ((R.was_fired(ti) && t.k(99) == 0) || !dg_on(R, t.dg)) return;
            double x, y; trg_pos(L, R, t, x, y);
            for (Player* q : ps) {
                if (!q || !box_hit(q->x, q->y, q->half, q->half, x, y, 15.0 * scale_of(t, 0), 15.0 * scale_of(t, 1))) continue;
                R.add_fired(ti); R.sig = mix64(R.sig, (uint64_t)ti * 23 + 1); TC.changed = true;
                if (t.k(504) != 0) spawn_group((int)t.k(51), 0.0); else toggle_trig((int)t.k(51), t.k(56) != 0, 0.0);
                break;
            }
        });
        // 텔레포트 오브 (36x36): 누르면 대상 그룹 첫 물체 자리로 (2.2 세부 설정은 무시, 추정)
        near(L.trg_torb, 1000.0, [&](int ti) {
            const Level::Trg& t = L.trg[ti]; if ((R.was_fired(ti) && t.k(99) == 0) || !dg_on(R, t.dg)) return;
            double x, y; trg_pos(L, R, t, x, y);
            if (!box_hit(p.x, p.y, hh, hh, x, y, 18.0 * scale_of(t, 0), 18.0 * scale_of(t, 1))) return;
            double ax, ay; if (!anc_pos(L, R, (int)t.k(51), ax, ay)) return;
            R.add_fired(ti); R.sig = mix64(R.sig, (uint64_t)ti * 29 + 1); TC.changed = true;
            p.x = f32(ax); p.y = f32(ay); p.last_x = p.x; p.last_y = p.y; p.on_ground = false; p.tp_wait = F.tp_portal_delay;
        });
    }
    // 이벤트 트리거 (3604): 지난 틱과 비교해서 생긴 일 (디컴파일 GJGameEvent 번호. 1틱 늦게 알아챔, 추정)
    {
        bool any = false; for (int ti : R.armed) if (L.trg[ti].id == 3604) { any = true; break; }
        if (any) {
            std::vector<int> ev = R.pend;
            if (pressed && !R.lastin) {
                ev.push_back(69);
                switch (p.mode) { case SHIP: ev.push_back(16); break; case UFO: ev.push_back(15); break; case WAVE: ev.push_back(21); break;
                                  case BALL: ev.push_back(19); break; case SWING: ev.push_back(20); break; case SPIDER: ev.push_back(18); break;
                                  case ROBOT: ev.push_back(13); break; default: break; }
            }
            if (!pressed && R.lastin) {
                ev.push_back(70);
                if (p.mode == SHIP) ev.push_back(17); else if (p.mode == WAVE) ev.push_back(22); else if (p.mode == ROBOT) ev.push_back(14);
            }
            if (R.pmode >= 0) {
                if (p.mode != R.pmode) ev.push_back(26 + p.mode);
                if (p.mini != R.pmini) ev.push_back(p.mini ? 56 : 55);
                if (p.upside_down != R.pup) ev.push_back(p.upside_down ? 10 : 11);
                if ((p2 != nullptr) != R.pdual) ev.push_back(p2 ? 57 : 58);
                if (p.is_dashing != R.pdash) ev.push_back(p.is_dashing ? 23 : 24);
                if (p.on_ground && !R.pground) ev.push_back(4);
                if (!p.on_ground && R.pground && p.is_jumping) ev.push_back(12);
            }
            if (!ev.empty()) {
                std::vector<int> arm_copy = R.armed;
                for (int ti : arm_copy) {
                    const Level::Trg& t = L.trg[ti]; if (t.id != 3604) continue;
                    const std::vector<double>* lst = t.list(430); if (!lst) continue;
                    for (double e : *lst) if (std::find(ev.begin(), ev.end(), (int)e) != ev.end()) { spawn_group((int)t.k(51), 0.0); break; }
                }
                TC.changed = true;
            }
        }
        R.pmode = p.mode; R.pmini = p.mini; R.pup = p.upside_down; R.pdual = p2 != nullptr; R.pdash = p.is_dashing; R.pground = p.on_ground;
        R.pend.clear();
    }
    // 터치 트리거 (1595): 누름·뗌 (디컴파일 playerButton. 키 81 누르고 있는 동안, 82: 0 바꾸기 1 켜기 2 끄기)
    if (pressed != R.lastin) {
        std::vector<int> arm_copy = R.armed;
        for (int ti : arm_copy) {
            const Level::Trg& t = L.trg[ti]; if (t.id != 1595) continue;
            bool hold = t.k(81) != 0; int mode = (int)t.k(82); int g = (int)t.k(51);
            if (!pressed && !hold) continue;
            bool en = group_on(L, R, g), act;
            if (hold) act = mode == 1 ? pressed : (mode == 2 ? !pressed : !en);
            else act = mode == 1 ? true : (mode == 2 ? false : !en);
            R.sig = mix64(R.sig, (uint64_t)ti * 13 + (pressed ? 1 : 2));
            toggle_trig(g, act, 0.0);
        }
        R.lastin = pressed; TC.changed = true;
    }
    // ④ 기다리던 생성
    if (!R.spawns.empty()) {
        std::vector<Spn> due;
        for (auto it = R.spawns.begin(); it != R.spawns.end();) { if (it->due <= TC.now) { due.push_back(*it); it = R.spawns.erase(it); } else ++it; }
        std::stable_sort(due.begin(), due.end(), [](const Spn& a, const Spn& b) { return a.due < b.due; });
        for (auto& s : due) { TC.changed = true; spawn_group(s.g, TC.now - s.due); }
    }
    R.gdy.clear();                                                    // 이번 틱 그룹별 세로 이동량 (충돌 판정용)
    // ⑤ 그룹 옮기기: 회전 → 이동 → 플레이어 Y 따라가기 → 따라가기
    if (!R.acts.empty()) {
        TC.changed = true;
        static thread_local std::vector<char> gone; gone.assign(R.acts.size(), 0);
        for (int kind : {5, 2, 7, 1, 3, 6, 4}) {
            for (size_t k = 0; k < R.acts.size(); ++k) {
                Act& a = R.acts[k]; if (a.kind != kind || a.paused || gone[k]) continue;
                bool end = !a.forever && a.done >= a.T;
                double e = a.T <= 0 ? 1.0 : ease(a.easing, a.rate, std::max(0.0, std::min(1.0, a.done / a.T)));
                if (kind == 1) {
                    if (a.forever) e = 1.0;
                    double xn = p.x, yn = p.y;
                    if (end) { double over = a.done - a.T; xn = p.x - over * TC.vx; yn = p.y - over * TC.vy; }
                    double nx = a.lx ? (xn - a.x0) * a.mx : a.dx * e, ny = a.ly ? (yn - a.y0) * a.my : a.dy * e;
                    if (ny != a.py) R.gdy.push_back({a.gi, ny - a.py});
                    if (nx != a.px || ny != a.py) { a.px = nx; a.py = ny; G_GSDIRTY = true; }
                    if (end) { GS& s = R.gs.mut(a.gi); s.mtx += a.px; s.mty += a.py; a.px = a.py = 0; }   // 끝: 그룹 상태로 넘김
                } else if (kind == 2) {
                    double want = a.dx * e, d = want - a.ang; a.ang = want;
                    if (d != 0) apply_rot(R, L, a.gi, d, a.cg, a.lx);
                } else if (kind == 7) {                               // 키프레임
                    const Level::Trg& t = L.trg[a.t];
                    const std::vector<int>& kf = L.kf_by_group.find(a.src)->second;
                    size_t n = kf.size(); bool loop = false; for (int ki : kf) if (L.trg[ki].k(376) != 0) loop = true;
                    double tm = t.k(520, 1.0); if (tm == 0) tm = 1.0;
                    double tsec = a.done / 240.0 * tm, total = 0;
                    size_t nseg = loop ? n : n - 1;
                    for (size_t i = 0; i < nseg; ++i) total += std::max(0.0, L.trg[kf[i]].k(10));
                    bool fin = false;
                    if (total <= 0) { tsec = 0; fin = !loop; }
                    else if (tsec >= total) { if (loop) tsec = std::fmod(tsec, total); else { tsec = total; fin = true; } }
                    size_t si = 0; double f = 0, acc = 0;
                    for (si = 0; si < nseg; ++si) { double d = std::max(0.0, L.trg[kf[si]].k(10)); if (tsec < acc + d || si == nseg - 1) { f = d > 0 ? (tsec - acc) / d : 1.0; break; } acc += d; }
                    f = std::max(0.0, std::min(1.0, f));
                    const Level::Trg &k0 = L.trg[kf[0]], &ka = L.trg[kf[si]], &kb = L.trg[kf[(si + 1) % n]];
                    double nx = (ka.x + (kb.x - ka.x) * f - k0.x) * t.k(521, 1.0), ny = (ka.y + (kb.y - ka.y) * f - k0.y) * t.k(545, 1.0);
                    if (nx != a.px || ny != a.py) { a.px = nx; a.py = ny; G_GSDIRTY = true; }
                    auto rotof = [&](const Level::Trg& k) { return k.k(6) + 360.0 * k.k(537) * (k.k(536) == 2 ? -1.0 : 1.0); };
                    double want = (rotof(ka) + (rotof(kb) - rotof(ka)) * f - rotof(k0)) * t.k(522, 1.0);
                    if (want != a.ang) { double d = want - a.ang; a.ang = want; apply_rot(R, L, a.gi, d, a.cg, false); }
                    auto scx = [&](const Level::Trg& k) { return k.k(128, k.k(32, 1.0)); }; auto scy = [&](const Level::Trg& k) { return k.k(129, k.k(32, 1.0)); };
                    double wx = (scx(ka) + (scx(kb) - scx(ka)) * f) / (scx(k0) != 0 ? scx(k0) : 1.0), wy = (scy(ka) + (scy(kb) - scy(ka)) * f) / (scy(k0) != 0 ? scy(k0) : 1.0);
                    wx = 1.0 + (wx - 1.0) * t.k(523, 1.0); wy = 1.0 + (wy - 1.0) * t.k(546, 1.0);
                    if (wx != 0 && wy != 0 && (wx != a.sx || wy != a.sy)) { apply_scale(R, L, a.gi, wx / a.sx, wy / a.sy, a.cg, false); a.sx = wx; a.sy = wy; }
                    if ((double)si > a.steer || (loop && (double)si < a.steer)) {                 // 키프레임 점에 닿음 → 그 점의 생성 그룹(71, 지연 557)
                        const Level::Trg& kr = L.trg[kf[si]];
                        if (kr.k(71) > 0) { double dl = kr.k(557) * 240.0; if (dl <= 0) spawn_group((int)kr.k(71), 0.0); else R.spawns.push_back({TC.now + dl, (int)kr.k(71), a.t}); }
                        a.steer = (double)si;
                    }
                    if (fin) { GS& s = R.gs.mut(a.gi); s.mtx += a.px; s.mty += a.py; a.px = a.py = 0; end = true; }
                    else end = false;
                } else if (kind == 6) {                               // 고급 따라가기 (속도는 그룹마다 하나, 난수 흔들림은 0)
                    const Level::Trg& t = L.trg[a.t];
                    auto P = [&](int key, double d = 0.0) { return (a.et >= 0 && L.trg[a.et].has(key)) ? L.trg[a.et].k(key) : t.k(key, d); };
                    double tx = 0, ty = 0; bool ok = true;
                    if (a.src < 0) {
                        Player* q = (a.src == -2 && TC.p2) ? TC.p2 : TC.p; tx = q->x; ty = q->y;
                        int dl = (int)std::lround(P(292) * 240.0);
                        if (dl > 0 && q == TC.p) { const YNode* nd = R.yh.get(); for (int i = 0; i < dl && nd; ++i) nd = nd->prev.get(); if (nd) { tx = nd->x; ty = nd->y; } else ok = false; }
                    } else ok = anc_pos(L, R, a.src, tx, ty);
                    double ax, ay;
                    if (ok && anc_pos(L, R, L.dyn_gid[a.gi], ax, ay)) {
                        double dx = tx - ax, dy = ty - ay, dist = std::hypot(dx, dy);
                        bool xo = P(306) != 0, yo = P(307) != 0;
                        if (xo) dy = 0; else if (yo) dx = 0;
                        double mr = P(308);
                        if (!(mr != 0 && dist > mr)) {
                            if (!a.started) {
                                a.started = true;
                                double ss = P(300);
                                if (ss != 0) { double ang = P(563) * 3.141592653589793 / 180.0; if (!xo) a.vy = std::sin(ang) * ss; if (!yo) a.vx = std::cos(ang) * ss; }   // 시작 방향 기준 추정
                            }
                            double dt = twf; int mode = (int)P(367);
                            if (mode == 1) {
                                double nd2 = P(359), acc = P(334) * 0.01, fr = P(558) * 0.01;
                                if (nd2 > 0 && nd2 > dist) { double q = 1.0 - dist / nd2; acc += (P(357) * 0.01 - acc) * q; fr += (P(561) * 0.01 - fr) * q; }
                                double nx2 = dist > 0 ? dx / dist : 0, ny2 = dist > 0 ? dy / dist : 0;
                                if (fr != 0) { if (!yo) a.vx = a.vx * (1.0 - fr) + nx2 * acc * dt; if (!xo) a.vy = a.vy * (1.0 - fr) + ny2 * acc * dt; }
                                else { if (!yo) a.vx += nx2 * acc * dt; if (!xo) a.vy += ny2 * acc * dt; }
                            } else if (mode == 2) {
                                double sp = std::hypot(a.vx, a.vy), ang = std::atan2(a.vy, a.vx), tang = P(305) != 0 ? std::atan2(dy, dx) : ang;
                                double diff = std::remainder(tang - ang, 2 * 3.141592653589793);
                                double st = P(316) * 0.01;
                                if (P(318) > 0 && sp <= P(322)) st = P(318) * 0.01;
                                else if (P(320) > 0 && P(324) > 0 && sp >= P(324)) st = P(320) * 0.01;
                                if (P(326) > 0 && std::fabs(diff) > P(328) * 0.017453 && sp > P(332)) st = P(330) * 0.01;
                                a.steer += (st - a.steer) / 10.0;
                                double ap = a.steer; if (std::fabs(diff) < ap) ap = diff;
                                ang += ap * dt;
                                if (!xo) a.vy = std::sin(ang) * sp; if (!yo) a.vx = std::cos(ang) * sp;
                            } else {
                                double e = P(361); double inv = e > 1.0 ? 1.0 / e : 1.0;
                                if (dist > 0) { if (!xo) a.vy = dy * inv; if (!yo) a.vx = dx * inv; }
                                else { a.vx = a.vy = 0; }                   // 딱 닿으면 멈춤 (디컴파일은 속도 유지 → 정확히 0일 때 왕복하는 문제)
                            }
                            double ms = P(298);
                            if (ms > 0) { double sp = std::hypot(a.vx, a.vy); if (sp > ms) { a.vx *= ms / sp; a.vy *= ms / sp; } }
                            double sx2 = a.vx * dt, sy2 = a.vy * dt; if (xo) sy2 = 0; else if (yo) sx2 = 0;
                            if (sx2 != 0 || sy2 != 0) { GS& s = R.gs.mut(a.gi); s.mtx += sx2; s.mty += sy2; }
                        }
                    }
                    end = false;
                } else if (kind == 5) {                               // 크기: 지금 배율 = 1 + (목표-1)·진행 → 지난 틱 대비 비율만큼
                    double cx = 1.0 + (a.dx - 1.0) * e, cy = 1.0 + (a.dy - 1.0) * e;
                    double rx = a.px != 0 ? cx / a.px : 1.0, ry = a.py != 0 ? cy / a.py : 1.0;
                    if (cx != 0) a.px = cx; if (cy != 0) a.py = cy;
                    if (rx != 1.0 || ry != 1.0) apply_scale(R, L, a.gi, rx, ry, a.cg, a.lx);
                } else if (kind == 3) {                               // 플레이어 Y 따라가기 (디컴파일 processPlayerFollowActions. 그룹 첫 물체 기준으로 그룹 전체를 옮김 = 추정)
                    const Level::Trg& t = L.trg[a.t];
                    double factor = std::max(0.0, std::min(1.0, 0.25 * twf * t.k(90))), maxd = 0.25 * twf * t.k(105);
                    int dl = (int)std::lround(t.k(91) * 240.0); const YNode* nd = R.yh.get();
                    for (int i = 0; i < dl && nd; ++i) nd = nd->prev.get();
                    double ax, ay;
                    if (nd && anc_pos(L, R, L.dyn_gid[a.gi], ax, ay)) {
                        double d = factor * (nd->y + t.k(92) - ay);
                        if (maxd > 0) d = std::max(-maxd, std::min(maxd, d));
                        if (d != 0) { GS& s = R.gs.mut(a.gi); s.mty += d; }
                    }
                } else {                                              // 따라가기: 원본 그룹이 이 틱에 움직인 만큼 × 배율
                    double bx = 0, by = 0, ax2, ay2; bool found = false;
                    for (auto& f : fsrc) if (f.first == a.cg) { bx = f.second.first; by = f.second.second; found = true; break; }
                    if (found && anc_pos(L, R, a.cg, ax2, ay2)) {
                        double ddx = (ax2 - bx) * a.mx, ddy = (ay2 - by) * a.my;
                        if (ddx != 0 || ddy != 0) { GS& s = R.gs.mut(a.gi); s.mtx += ddx; s.mty += ddy; }
                    }
                }
                if (end) gone[k] = 1;
            }
        }
        size_t w = 0; for (size_t k = 0; k < R.acts.size(); ++k) if (!gone[k]) R.acts[w++] = R.acts[k];
        R.acts.resize(w);
    }
    // 영역 효과: 중심(71 그룹, 없으면 플레이어) 위치와 세기 갱신
    for (auto& A : R.areas) {
        const Level::Trg& t = L.trg[A.ti];
        A.done += twf;
        double T = t.k(10) * 240.0;
        double str = T > 0 ? ease((int)t.k(30), t.k(85, 2.0), std::min(1.0, A.done / T)) : 1.0;
        double cx = p.x, cy = p.y; int cg = (int)t.k(71);
        if (t.id >= 3017) cx = p.x + 285.0;                           // 화면 오른쪽 끝 ≈ 플레이어 x + 285 (카메라를 몰라서 추정)
        else if (cg > 0 && t.k(538) == 0) { if (!anc_pos(L, R, cg, cx, cy)) { cx = p.x; cy = p.y; } }
        if (!A.has_c || cx != A.cx || cy != A.cy || str != A.str) { A.cx = cx; A.cy = cy; A.str = str; A.has_c = true; G_GSDIRTY = true; }
    }
    // ⑥ 충돌 블록 (30x30 × 크기, 추정): 움직이는 블록(키 94)이 다른 블록과 겹치면 쌍 (디컴파일 updateCollisionBlocks)
    if (!L.trg_colb.empty() || L.col_players) {
        struct B { int id; bool dyn; double x, y, hw, hh; };
        std::vector<B> bs;
        if (L.col_players) {                                          // 플레이어도 움직이는 블록으로 (1 = -1, 2 = -2)
            bs.push_back({-1, true, p.x, p.y, hh, hh});
            if (p2) bs.push_back({-2, true, p2->x, p2->y, p2->half, p2->half});
        }
        for (int ti : L.trg_colb) {
            const Level::Trg& t = L.trg[ti]; if (!dg_on(R, t.dg)) continue;
            double x, y; trg_pos(L, R, t, x, y);
            bs.push_back({(int)t.k(80), t.k(94) != 0, x, y, 15.0 * scale_of(t, 0), 15.0 * scale_of(t, 1)});
        }
        std::vector<std::pair<int, int>> now;
        for (size_t i = 0; i < bs.size(); ++i) {
            if (!bs[i].dyn) continue;
            for (size_t j = 0; j < bs.size(); ++j) {
                if (i == j) continue;
                if (box_hit(bs[i].x, bs[i].y, bs[i].hw, bs[i].hh, bs[j].x, bs[j].y, bs[j].hw, bs[j].hh))
                    now.push_back({std::min(bs[i].id, bs[j].id), std::max(bs[i].id, bs[j].id)});
            }
        }
        std::sort(now.begin(), now.end()); now.erase(std::unique(now.begin(), now.end()), now.end());
        if (now != R.cols) {
            std::vector<std::pair<int, int>> enter, leave;
            std::set_difference(now.begin(), now.end(), R.cols.begin(), R.cols.end(), std::back_inserter(enter));
            std::set_difference(R.cols.begin(), R.cols.end(), now.begin(), now.end(), std::back_inserter(leave));
            R.cols = now; TC.changed = true;
            std::vector<int> arm_copy = R.armed;
            for (int pass = 0; pass < 2; ++pass) for (auto& pr : (pass == 0 ? enter : leave)) {
                for (int ti : arm_copy) {
                    const Level::Trg& t = L.trg[ti]; if (t.id != 1815) continue;
                    if ((t.k(93) != 0) != (pass == 1)) continue;
                    int a = t.k(201) != 0 ? -1 : (t.k(138) != 0 ? -1 : (t.k(200) != 0 ? -2 : (int)t.k(80)));
                    int b = t.k(201) != 0 ? -2 : (int)t.k(95);
                    if (std::min(a, b) != pr.first || std::max(a, b) != pr.second) continue;
                    R.sig = mix64(R.sig, (uint64_t)ti * 17 + pass);
                    toggle_trig((int)t.k(51), t.k(56) != 0, 0.0);
                }
            }
        }
    }
    if (G_GSDIRTY) {                                                  // 물체 위치가 바뀔 수 있을 때만 새 판. 번호 = 내용 해시
        auto bits = [](double v) { uint64_t u; std::memcpy(&u, &v, 8); return u; };
        uint64_t h = R.gsh;
        std::sort(G_MUTS.begin(), G_MUTS.end()); G_MUTS.erase(std::unique(G_MUTS.begin(), G_MUTS.end()), G_MUTS.end());
        for (int gi : G_MUTS) {
            const GS& g = R.gs.get(gi); h = mix64(h, (uint64_t)gi + 1);
            for (double v : {g.a, g.b, g.c, g.d, g.rtx, g.rty, g.mtx, g.mty, g.rot, g.sx, g.sy}) h = mix64(h, bits(v));
            h = mix64(h, (uint64_t)g.order * 2 + g.on);
        }
        R.gsh = h;
        uint64_t v = h;
        for (auto& a : R.acts) if (a.kind == 1 || a.kind == 7) { v = mix64(v, (uint64_t)a.gi + 7); v = mix64(v, bits(a.px)); v = mix64(v, bits(a.py)); }
        for (auto& A : R.areas) { v = mix64(v, (uint64_t)A.ti * 3 + 1 + (uint64_t)(A.et + 1) * 977); v = mix64(v, bits(A.cx)); v = mix64(v, bits(A.cy)); v = mix64(v, bits(A.str)); }
        R.ver = v | 1;
    }
    G_MUTS.clear();
}

static thread_local Player* G_TWIN = nullptr;
static void step_one(Player& p, const Level& L, bool pressed, bool main) {
    if (p.dead) return;
    G_L = &L;
    bool raw_pressed = pressed;
    if (!pressed) p.spider_hold = false;
    else if (p.spider_hold && (G_SPORB_HOLD || p.mode == SPIDER || p.mode == BALL) && !p.holding_jump) pressed = false;
    if (pressed != p.holding_jump) {
        if (pressed && (G_OFA || p.mode == UFO || p.mode == SWING || (p.mode == BALL && F.ball_orb_first) || (p.mode == SPIDER && F.spider_orb_first)) && orb_on_press(p, L)) {}
        else { if (pressed) push(p); else release(p); }
    }
    if (main && !L.teleports.empty() && F.tp_before_update) teleport_trigger(p, L, true);
    update(p);
    if (main && !L.teleports.empty() && !F.tp_before_update) teleport_trigger(p, L, false);
    if (main && L.strig && G_TRW) {
        if (!G_FRM) trig_step(*G_TRW, L, p, G_TWIN, raw_pressed);
        else {                                                          // 트리거는 세계 좌표로: 세계로 바꾼 복사본으로 돌리고 위치만 로컬로 되돌림
            const Frame F = *G_FRM; Player pw = p, tw2; Player* p2w = nullptr;
            F.to_world(p.x, p.y, pw.x, pw.y); F.to_world(p.last_x, p.last_y, pw.last_x, pw.last_y);
            if (G_TWIN) { tw2 = *G_TWIN; F.to_world(G_TWIN->x, G_TWIN->y, tw2.x, tw2.y); p2w = &tw2; }
            double wx0 = pw.x, wy0 = pw.y;
            trig_step(*G_TRW, L, pw, p2w, raw_pressed);
            double lx = p.x, ly = p.y, llx = p.last_x, lly = p.last_y;
            p = pw;
            if (pw.x != wx0 || pw.y != wy0) { F.to_local(pw.x, pw.y, p.x, p.y); F.to_local(pw.last_x, pw.last_y, p.last_x, p.last_y); }   // 텔레포트
            else { p.x = lx; p.y = ly; p.last_x = llx; p.last_y = lly; }
        }
    }
    collide(p, L);
    if (p.tp_wait > 0) p.tp_wait -= 1;
    p.tick += 1;
}

static bool linked(const Level& L, double x) {
    int st = 0;
    for (auto& g : L.grav_unlink) { if (g.first <= x) st = g.second; else break; }
    return F.dual_grav_link && st != 1;
}

// 상태 = 첫째 + (듀얼이면) 둘째
struct State {
    Player p; bool dual = false; Player tw;
    TrigRun trig;                                   // 상태 트리거 (STRIG 레벨만 씀)
    bool dead() const { return p.dead; }
};

static void dual_bounds(State& s, double y) {
    double H = (s.p.mode == SHIP || s.p.mode == UFO || s.p.mode == WAVE || s.tw.mode == SHIP || s.tw.mode == UFO || s.tw.mode == WAVE) ? 300.0 : 270.0;
    double fl = G_DBU ? std::max(0.0, y - H / 2.0) : std::max(0.0, 30.0 * std::floor((y - H / 2.0) / 30.0)), ce = fl + H;
    for (auto& b : G_BANDS) if (b[0] <= s.p.x && s.p.x <= b[1]) { fl = b[2]; ce = b[3]; }
    s.p.floor = fl; s.p.has_ceiling = true; s.p.ceiling = ce;
    s.tw.floor = fl; s.tw.has_ceiling = true; s.tw.ceiling = ce;
}

static void step(State& s, const Level& L, bool pressed) {
    Player& p = s.p;
    if (p.dead) return;
    G_TR = &s.trig; G_TRW = &s.trig; G_TWIN = s.dual ? &s.tw : nullptr;
    G_FRM = (L.strig && !s.trig.fr.id()) ? &s.trig.fr : nullptr;
    bool pressed2 = pressed;
    if (L.strig) {
        G_EVQ.clear(); G_TWF = s.trig.tw > 1.0 ? s.trig.tw : 1.0;
        if (s.trig.blk1) { if (!pressed) s.trig.blk1 = false; else pressed = false; }    // 점프 멈춤 뒤: 뗄 때까지 누름 무시
        if (s.trig.blk2) { if (!pressed2) s.trig.blk2 = false; else pressed2 = false; }
    } else G_TWF = 1.0;
    bool had_tw = s.dual;
    G_INDUAL = had_tw; G_SP0 = p.speed;
    p.mode_hit = 0;
    G_SNAP.set = false; G_GRAV = nullptr;
    step_one(p, L, pressed, true);
    bool snap = G_SNAP.set; G_SNAP.set = false;
    if (had_tw && G_GRAV == &p && !s.tw.dead && linked(L, p.x)) s.tw.flip_gravity(!p.upside_down);
    if (had_tw && F.dual_off_first && p.mode_hit == 287) {
        s.dual = false; p.has_dual_y_ref = false; had_tw = false;
    }
    if (had_tw) {
        Player& tw = s.tw;
        int tsp0 = tw.speed;
        tw.mode_hit = 0;
        G_GRAV = nullptr;
        step_one(tw, L, pressed2, false);
        if (G_GRAV == &tw && !p.dead && linked(L, p.x)) p.flip_gravity(!tw.upside_down);
        if (tw.dead) { p.die("듀얼 두 번째 플레이어: " + tw.cause); return; }
        if (G_DSP) { if (tw.speed != tsp0) p.set_speed(tw.speed); else if (p.speed != G_SP0) tw.set_speed(p.speed); }   // 듀얼 속도 같이 씀
        if (tw.mode_hit == 287) { if (F.dual_exit_swap) p = tw; s.dual = false; }
    }
    if (snap && !s.dual && !had_tw) {
        Player t2 = G_SNAP.p;
        t2.upside_down = !p.upside_down;
        t2.can_jump = t2.on_ground = t2.is_jumping = false;
        t2.used = p.used;
        if (G_DSS) t2.y_vel = -G_SNAP.p.y_vel;
        s.tw = t2; s.dual = true;
        dual_bounds(s, G_SNAP.y);
        p.has_dual_y_ref = true; p.dual_y_ref = G_SNAP.y;
        if (F.twin_spawn_push && pressed) {
            s.tw.holding_jump = false; s.tw.spider_hold = false;
            G_L = &L;
            if (s.tw.mode == SPIDER) spider_jump(s.tw); else push(s.tw);
        }
    } else if (p.mode_hit == 287) {
        s.dual = false; p.has_dual_y_ref = false;
    }
    if (!s.dual) p.has_dual_y_ref = false;
    if (s.dual && p.has_dual_y_ref) dual_bounds(s, p.dual_y_ref);
    if (F.shared_used && s.dual) {
        std::vector<int> u; std::set_union(p.used.begin(), p.used.end(), s.tw.used.begin(), s.tw.used.end(), std::back_inserter(u));
        p.used.set(u); s.tw.used = p.used;
    }
    if (L.strig && s.trig.freq) {                                      // 좌표 바꾸기 (방향 반대·게임 회전): 플레이어 로컬 위치·속도는 그대로, 세계 쪽 변환을 바꿈
        Frame& F = s.trig.fr; Frame N = F;
        if (s.trig.freq == 1) { N.a = -F.a; N.c = -F.c; G_EVQ.push_back(75); }
        else { N.a = s.trig.fa; N.b = s.trig.fb; N.c = s.trig.fc; N.d = s.trig.fd; }
        s.trig.freq = 0;
        double wx, wy, wlx, wly; F.to_world(p.x, p.y, wx, wy); F.to_world(p.last_x, p.last_y, wlx, wly);
        N.tx = wx - (N.a * p.x + N.b * p.y); N.ty = wy - (N.c * p.x + N.d * p.y);
        N.to_local(wlx, wly, p.last_x, p.last_y);
        if (s.dual) { double tx, ty, tlx, tly; F.to_world(s.tw.x, s.tw.y, tx, ty); F.to_world(s.tw.last_x, s.tw.last_y, tlx, tly); N.to_local(tx, ty, s.tw.x, s.tw.y); N.to_local(tlx, tly, s.tw.last_x, s.tw.last_y); }
        N.h = mix64(mix64(mix64((uint64_t)(N.a + 2) * 7 + (N.b + 2) * 49 + (N.c + 2) * 343 + (N.d + 2) * 2401, 0), (uint64_t)std::llround(N.tx * 1024)), (uint64_t)std::llround(N.ty * 1024));
        F = N; p.snap_obj = -1; if (s.dual) s.tw.snap_obj = -1;
        s.trig.sig = mix64(s.trig.sig, N.h);
    }
    if (L.strig && !G_EVQ.empty()) { s.trig.pend.insert(s.trig.pend.end(), G_EVQ.begin(), G_EVQ.end()); G_EVQ.clear(); }
}

// ---------------------------------------------------------------- 읽기
static Player read_player(std::istringstream& ss) {
    Player p; std::string t;
    auto D = [&]() { ss >> t; return t == "nan" ? std::numeric_limits<double>::quiet_NaN() : std::strtod(t.c_str(), nullptr); };
    auto B = [&]() { ss >> t; return t == "1"; };
    auto I = [&]() { ss >> t; return std::strtoll(t.c_str(), nullptr, 10); };
    p.x = D(); p.y = D(); p.last_x = D(); p.last_y = D(); p.y_vel = D(); p.x_vel = D(); p.speed = (int)I(); p.y_start = D(); p.gravity = D();
    p.mini = B(); p.upside_down = B(); p.holding_jump = B(); p.jump_unused = B(); p.can_jump = B(); p.on_ground = B(); p.is_jumping = B();
    p.holding_release_delayed = B(); p.can_ring_jump = B(); p.touched_ring = B(); p.is_dashing = B(); p.dash_y = D();
    p.velocity_uncapped = B(); p.last_flip_tick = I(); p.tick = I(); p.snap_obj = (int)I(); p.snap_distance = D(); p.is_on_slope = B(); p.was_on_slope = B();
    p.slope = (int)I(); p.slope_velocity = D(); p.slope_start_tick = I(); p.dead = B(); p.half = D(); p.mode = (int)I();
    p.robot_boost_charge = D(); p.robot_boost_invalidated = B(); p.floor = D();
    double ce = D(); p.has_ceiling = !std::isnan(ce); p.ceiling = p.has_ceiling ? ce : 0.0;
    p.spider_hold = B(); double dr = D(); p.has_dual_y_ref = !std::isnan(dr); p.dual_y_ref = p.has_dual_y_ref ? dr : 0.0; p.tp_wait = (int)I();
    long long n = I(); std::vector<int> uv; for (long long i = 0; i < n; ++i) uv.push_back((int)I());
    std::sort(uv.begin(), uv.end()); p.used.clear(); if (!uv.empty()) p.used.set(std::move(uv));
    return p;
}

static bool load_level(const std::string& path, Level& L, State& start) {
    std::ifstream in(path); if (!in) return false;
    std::string line; int maxuid = -1; std::vector<Obj> tmp;
    while (std::getline(in, line)) {
        std::istringstream ss(line); std::string tag; ss >> tag;
        if (tag == "SET") { int a, b; ss >> a >> b; F.fix_gravity_bug = a; F.fix_radius = b; }
        else if (tag == "FLAGS") {
            int v[12]; for (int i = 0; i < 12; ++i) ss >> v[i];
            F.rot131 = v[0]; F.spider_entry_hold = v[1]; F.twin_spawn_push = v[2]; F.dual_off_first = v[3]; F.dual_bounds_player = v[4];
            F.mini_wave_vel2 = v[5]; F.tp_before_update = v[6]; F.tp_portal_delay = v[7]; F.portal_reverse = v[8]; F.shared_used = v[9];
            F.ball_orb_first = v[10]; F.dual_grav_link = v[11];
            { int e; if (ss >> e) F.dual_exit_swap = e; if (ss >> e) F.spider_ray_solid = e; if (ss >> e) F.spider27 = e; if (ss >> e) F.spider_orb_first = e; }
        } else if (tag == "SEC") { std::string t; ss >> t; L.sec = std::strtod(t.c_str(), nullptr); }
        else if (tag == "OBJ") {
            Obj o; std::string t; int fx, up, hs, nc;
            auto D = [&]() { ss >> t; return std::strtod(t.c_str(), nullptr); };
            ss >> o.uid >> o.id >> o.kind >> o.arg;
            o.x = D(); o.y = D(); o.w = D(); o.h = D(); o.minx = D(); o.maxx = D(); o.miny = D(); o.maxy = D(); o.rot = D();
            ss >> fx >> up >> o.orient >> hs; o.flip_x = fx; o.upside = up; o.hazard_slope = hs;
            o.radius = D(); o.tp_y = D(); ss >> nc;
            for (int i = 0; i < nc / 2 + 0 && false; ++i) {}
            for (int i = 0; i < nc; ++i) { double cx = D(), cy = D(); o.corners.push_back({cx, cy}); }
            maxuid = std::max(maxuid, o.uid); tmp.push_back(o);
        } else if (tag == "CELL") {
            long long c; int n; ss >> c >> n; auto& v = L.cells[c]; for (int i = 0; i < n; ++i) { int u; ss >> u; v.push_back(u); }
        } else if (tag == "HB" || tag == "DB") {
            long long c; int n; ss >> c >> n; auto& v = (tag == "HB" ? L.hblocks : L.dblocks)[c];
            for (int i = 0; i < n; ++i) { std::string a, b, cc, d; ss >> a >> b >> cc >> d; v.push_back({std::strtod(a.c_str(), 0), std::strtod(b.c_str(), 0), std::strtod(cc.c_str(), 0), std::strtod(d.c_str(), 0)}); }
        } else if (tag == "TP") { std::string t; Level::Tp tp; ss >> t >> tp.g >> tp.k; tp.tx = std::strtod(t.c_str(), 0); L.teleports.push_back(tp); }
        else if (tag == "GROUP") { int g; std::string a, b; ss >> g >> a >> b; L.groups[g] = {std::strtod(a.c_str(), 0), std::strtod(b.c_str(), 0)}; }
        else if (tag == "BAND") { std::array<double, 4> b; for (auto& v : b) { std::string t; ss >> t; v = std::strtod(t.c_str(), 0); } G_BANDS.push_back(b); }
        else if (tag == "S27X") { std::string t; ss >> t; F.spider27_x = std::strtod(t.c_str(), 0); }
        else if (tag == "OFF") { int a, b; ss >> a >> b; L.off_by[a] = b; }
        else if (tag == "FREE") { int a; ss >> a; L.free_uids.insert(a); }
        else if (tag == "DSG") { std::string a, b; ss >> a >> b; G_DSG = std::strtod(a.c_str(), 0); G_DSG_X0 = std::strtod(b.c_str(), 0); }
        else if (tag == "DFF") { int v; ss >> v; G_DFF = v; }
        else if (tag == "SU") { int v; ss >> v; G_SU = v; }
        else if (tag == "OFA") { int v; ss >> v; G_OFA = v; }
        else if (tag == "DSS") { int v; ss >> v; G_DSS = v; }
        else if (tag == "DBU") { int v; ss >> v; G_DBU = v; }
        else if (tag == "DSP") { int v; ss >> v; G_DSP = v; }
        else if (tag == "MV") {   // g tx dx dy ticks easing rate lock_x mod_x x_end
            int g; Level::Move m; std::string t; auto D = [&]() { ss >> t; return std::strtod(t.c_str(), nullptr); };
            ss >> g; m.tx = D(); m.dx = D(); m.dy = D(); m.ticks = D(); ss >> m.easing; m.rate = D(); int lk; ss >> lk; m.lock_x = lk; m.mod_x = D(); m.x_end = D();
            L.moves[g].push_back(m);
        } else if (tag == "SPD") { std::string a, b; ss >> a >> b; L.spd_x.push_back(std::strtod(a.c_str(), 0)); L.spd_v.push_back(std::strtod(b.c_str(), 0)); }
        else if (tag == "GM") { std::string a, b; ss >> a >> b; L.gm_x.push_back(std::strtod(a.c_str(), 0)); L.gm_v.push_back(std::strtod(b.c_str(), 0)); }
        else if (tag == "MOVER") { int u, n; ss >> u >> n; std::vector<int> gs(n); for (auto& g : gs) ss >> g; L.mover_uid.push_back(u); L.mover_groups.push_back(gs); }
        else if (tag == "HM") { Level::HM h; int n; std::string a, b, c, d; ss >> h.which >> a >> b >> c >> d >> n;
            h.r = {std::strtod(a.c_str(), 0), std::strtod(b.c_str(), 0), std::strtod(c.c_str(), 0), std::strtod(d.c_str(), 0)};
            h.groups.resize(n); for (auto& g : h.groups) ss >> g; L.hmovers.push_back(h); }
        else if (tag == "GU") { std::string a; int v; ss >> a >> v; L.grav_unlink.push_back({std::strtod(a.c_str(), 0), v}); }
        else if (tag == "STRIG") { int v; ss >> v; L.strig = v; }
        else if (tag == "DYN") { int n; ss >> n; for (int i = 0; i < n; ++i) { int g; ss >> g; L.dyn_idx[g] = (int)L.dyn_gid.size(); L.dyn_gid.push_back(g); } }
        else if (tag == "TRG") {   // 번호 ID x y how multi 그룹수 그룹... 키수 (키 값)...
            Level::Trg t; std::string a, b; int ng, nk, mu; ss >> t.idx >> t.id >> a >> b >> t.how >> mu >> ng; t.multi = mu;
            t.x = std::strtod(a.c_str(), 0); t.y = std::strtod(b.c_str(), 0);
            t.groups.resize(ng); for (auto& g : t.groups) ss >> g;
            ss >> nk; for (int i = 0; i < nk; ++i) { int k; std::string v; ss >> k >> v; t.kv.push_back({k, std::strtod(v.c_str(), 0)}); }
            std::sort(t.kv.begin(), t.kv.end());
            L.trg.push_back(t);
        } else if (tag == "TRGL") { int i, k, n; ss >> i >> k >> n; std::vector<double> v(n); for (auto& x : v) { std::string t; ss >> t; x = std::strtod(t.c_str(), 0); }
            if (i >= 0 && i < (int)L.trg.size()) L.trg[i].lists.push_back({k, v});
        } else if (tag == "ANC") { int g, n; std::string a, b; ss >> g >> a >> b >> n; Level::Anc an; an.x = std::strtod(a.c_str(), 0); an.y = std::strtod(b.c_str(), 0);
            for (int i = 0; i < n; ++i) { int h; ss >> h; an.dg.push_back(h); } L.anc[g] = an; }
        else if (tag == "REV") { int u; ss >> u; L.rev_uids.insert(u); }
        else if (tag == "MAIN") { start.p = read_player(ss); }
        else if (tag == "TWIN") { start.tw = read_player(ss); start.dual = true; }
    }
    L.objs.assign(maxuid + 1, Obj());
    for (auto& o : tmp) L.objs[o.uid] = o;
    L.mover_idx.assign(L.objs.size(), -1);
    for (size_t i = 0; i < L.mover_uid.size(); ++i) L.mover_idx[L.mover_uid[i]] = (int)i;
    // 움직이는 물체 범위: 이동 트리거마다 0 ~ 최대 이동을 넉넉하게 (이징이 넘쳐도 되게 -1배 ~ 2배)
    L.mover_env.resize(L.mover_uid.size());
    for (size_t i = 0; i < L.mover_uid.size(); ++i) {
        const Obj& o = L.objs[L.mover_uid[i]];
        double xl = 0, xh = 0, yl = 0, yh = 0;
        for (int g : L.mover_groups[i]) {
            auto it = L.moves.find(g); if (it == L.moves.end()) continue;
            for (auto& m : it->second) {
                double ax = m.lock_x ? (m.x_end - m.tx) * m.mod_x : m.dx;
                xl += std::min({0.0, -std::fabs(ax), 2.0 * ax}); xh += std::max({0.0, std::fabs(ax), 2.0 * ax});
                yl += std::min({0.0, -std::fabs(m.dy), 2.0 * m.dy}); yh += std::max({0.0, std::fabs(m.dy), 2.0 * m.dy});
            }
        }
        L.mover_env[i] = {o.minx + xl - 1.0, o.maxx + xh + 1.0, o.miny + yl - 1.0, o.maxy + yh + 1.0};
    }
    if (L.strig) {
        // 그룹 번호 → 움직이는 그룹 번호(dg)
        auto to_dg = [&](const std::vector<int>& gs) { std::vector<int> v; for (int g : gs) { int i = L.dg_of(g); if (i >= 0) v.push_back(i); } std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); return v; };
        for (auto& kv : L.anc) { std::vector<int> v; for (int g : kv.second.dg) { int i = L.dg_of(g); if (i >= 0) v.push_back(i); } kv.second.dg = v; }   // 순서 그대로
        for (auto& h : L.hmovers) h.dg = to_dg(h.groups);
        std::map<std::vector<int>, int> cmap;
        L.mover_clu.assign(L.mover_uid.size(), -1);
        for (size_t i = 0; i < L.mover_uid.size(); ++i) {
            std::vector<int> dg; for (int g : L.mover_groups[i]) { int k = L.dg_of(g); if (k >= 0) dg.push_back(k); }   // 그룹 순서 그대로 (더하는 순서)
            auto it = cmap.find(dg);
            if (it == cmap.end()) { it = cmap.emplace(dg, (int)L.clu.size()).first; Level::Clu c; c.dg = dg; L.clu.push_back(c); }
            Level::Clu& c = L.clu[it->second]; c.movers.push_back((int)i); L.mover_clu[i] = it->second;
            const Obj& o = L.objs[L.mover_uid[i]];
            c.minx = std::min(c.minx, o.minx); c.maxx = std::max(c.maxx, o.maxx); c.miny = std::min(c.miny, o.miny); c.maxy = std::max(c.maxy, o.maxy);
            c.pad = std::max(c.pad, std::hypot(o.maxx - o.minx, o.maxy - o.miny) / 2.0 + std::max(o.w, o.h) + 1.0);
        }
        for (auto& t : L.trg) {
            t.dg = to_dg(t.groups);
            for (int g : t.groups) L.trg_by_group[g].push_back(t.idx);
            if ((t.id == 1815 || t.id == 3609) && (t.k(138) != 0 || t.k(200) != 0 || t.k(201) != 0)) L.col_players = true;
            if (t.id == 1594) L.trg_rings.push_back(t.idx);
            else if (t.id == 3643) L.trg_tblock.push_back(t.idx);
            else if (t.id == 3027) L.trg_torb.push_back(t.idx);
            else if (t.id == 3640) L.trg_sblock.push_back(t.idx);
            else if (t.id == 3032 || t.id == 2069 || t.id == 3645) {}       // 키프레임 점·힘 블록: 켜지는 트리거 아님
            else if (t.id == 1816) L.trg_colb.push_back(t.idx);
            else if (t.id == 1275 || t.id == 1587 || t.id == 1589 || t.id == 1598 || t.id == 3601 || t.id == 1614) L.trg_items.push_back(t.idx);
            else if (t.how == 0) L.trg_x.push_back(t.idx);
            else if (t.how == 1) L.trg_touch.push_back(t.idx);
            if (t.id == 1814) L.fpy_hist = std::max(L.fpy_hist, (int)std::lround(t.k(91) * 240.0) + 1);
            if (t.id == 3016 || t.id == 3660) L.fpy_hist = std::max(L.fpy_hist, (int)std::lround(t.k(292) * 240.0) + 1);
            if (t.id == 3032) for (int g : t.groups) L.kf_by_group[g].push_back(t.idx);
            if (t.id == 2069 || t.id == 3645) L.trg_force.push_back(t.idx);
        }
        // x 순 (같은 x는 레벨 순서) — 생성 목록은 레벨 순서 그대로
        auto byx = [&](std::vector<int>& v) { std::stable_sort(v.begin(), v.end(), [&](int a, int b) { return L.trg[a].x < L.trg[b].x; }); };
        for (auto& kv : L.kf_by_group) std::stable_sort(kv.second.begin(), kv.second.end(), [&](int a, int b) {   // 키프레임 순서 (374), 같으면 x
            double oa = L.trg[a].k(374), ob = L.trg[b].k(374); return oa != ob ? oa < ob : L.trg[a].x < L.trg[b].x; });
        byx(L.trg_force);
        byx(L.trg_x); byx(L.trg_touch); byx(L.trg_rings); byx(L.trg_items); byx(L.trg_tblock); byx(L.trg_torb); byx(L.trg_sblock);
        for (int ti : L.trg_x) L.trg_xs.push_back(L.trg[ti].x);
        start.trig.gs.init(L.dyn_gid.size()); start.trig.ver = 0x9e3779b97f4a7c15ULL | 1; G_MUTS.clear();
    }
    // 시작 상태의 snap 위치 (원래 자리)
    if (start.p.snap_obj >= 0) { start.p.snap_ox = L.objs[start.p.snap_obj].x; start.p.snap_oy = L.objs[start.p.snap_obj].y; }
    if (start.dual && start.tw.snap_obj >= 0) { start.tw.snap_ox = L.objs[start.tw.snap_obj].x; start.tw.snap_oy = L.objs[start.tw.snap_obj].y; }
    L.build_grid();
    // 파이썬 텔레포트 목록은 (tx, g, k) 정렬 → 내보낼 때 이미 정렬됨
    return true;
}

} // namespace gd
