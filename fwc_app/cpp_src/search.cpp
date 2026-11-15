// search.cpp — partrun.search 를 C++로 옮긴 것 (같은 순서·같은 비용·같은 가지치기)
// 사용: ./search 레벨.txt 규칙.txt cap 출력.txt
//   출력: 첫 줄 "OK n" 또는 "FAIL t x y mode", 둘째 줄 입력(0/1) 목록 (실패면 가장 앞 상태까지 경로)
#include "gdcore.hpp"
#include <chrono>
#include <set>
using namespace gd;

struct RuleU { double lo, hi; bool need_dual; int who, need; std::vector<int> uids; };
struct RuleY { double lo, hi; bool need_dual; int who; double cy, tol; };
struct RuleYE { double lo, hi, cy, tol; };
static bool ANY_MODE = false;
static std::unordered_map<long long, double> CEIL_C;   // 순수 모드: 위로 레벨 물체 범위(+150) 넘으면 버림   // 순수 모드: 모드 순서 가정 없음
struct RuleYC { double lo, hi, cy, tol, w; };
struct RuleYD { double lo, hi, a, b, tol, w; };   // 듀얼 기준점 둘(a 아래, b 위): 두 플레이어가 하나씩 맞아야 함 (짝은 더 잘 맞는 쪽)   // 부드러운 기준점: 벗어난 만큼 비용 (버리지 않음)   // 둘 중 한 명이라도 cy±tol 안에 있어야 함 (듀얼 아니면 첫째)
struct Rules {
    double x_end = 0; int start_mode = 0; std::set<int> end_modes, env_modes, ban;
    int press_cost = 24, wave_press = 120; double near = 14.0, near_w = 1.0;
    std::unordered_map<long long, double> floor_c;
    std::vector<std::pair<double, int>> marks; bool marks_sorted = true;
    std::unordered_map<long long, std::pair<double, double>> env;
    struct Block { double x0, x1, y0, y1; int m; }; std::vector<Block> blocks;
    std::vector<std::pair<double, int>> musts; std::vector<int> avoids;
    std::unordered_map<int, int> forced;
    std::vector<RuleU> us; std::vector<RuleY> ys; std::vector<RuleYE> yes; std::vector<RuleYC> ycs; std::vector<RuleYD> yds;
    bool has_robotside = false; double rs_lo = 0, rs_hi = 0; std::vector<int> rs_uids;
    bool has_tro = false; double tro_lo = 0, tro_hi = 0; std::vector<int> tro_uids;
};

static bool load_rules(const char* path, Rules& R) {
    std::ifstream in(path); if (!in) return false;
    std::string line;
    auto D = [](std::istringstream& ss) { std::string t; ss >> t; return std::strtod(t.c_str(), nullptr); };
    while (std::getline(in, line)) {
        std::istringstream ss(line); std::string tag; ss >> tag;
        if (tag == "XEND") R.x_end = D(ss);
        else if (tag == "STARTMODE") ss >> R.start_mode;
        else if (tag == "ENDMODES") { int m; while (ss >> m) R.end_modes.insert(m); }
        else if (tag == "ENVMODES") { int m; while (ss >> m) R.env_modes.insert(m); }
        else if (tag == "BAN") { int m; while (ss >> m) R.ban.insert(m); }
        else if (tag == "COSTS") { ss >> R.press_cost >> R.wave_press; R.near = D(ss); R.near_w = D(ss); }
        else if (tag == "FLOORC") { long long c; ss >> c; R.floor_c[c] = D(ss); }
        else if (tag == "CEILC") { long long c; ss >> c; CEIL_C[c] = D(ss); }
        else if (tag == "MARK") { double x = D(ss); int m; ss >> m; R.marks.push_back({x, m}); }
        else if (tag == "ENV") { long long c; ss >> c; double lo = D(ss), hi = D(ss); R.env[c] = {lo, hi}; }
        else if (tag == "BLOCK") { Rules::Block b; b.x0 = D(ss); b.x1 = D(ss); b.y0 = D(ss); b.y1 = D(ss); ss >> b.m; R.blocks.push_back(b); }
        else if (tag == "MUST") { double x = D(ss); int u; ss >> u; R.musts.push_back({x, u}); }
        else if (tag == "AVOID") { int u; while (ss >> u) R.avoids.push_back(u); std::sort(R.avoids.begin(), R.avoids.end()); }
        else if (tag == "ANYMODE") ANY_MODE = true;
        else if (tag == "FORCED") { int t, h; ss >> t >> h; R.forced[t] = h; }
        else if (tag == "U") {
            RuleU u; u.lo = D(ss); u.hi = D(ss); int nd, n; ss >> nd >> u.who >> u.need >> n; u.need_dual = nd;
            for (int i = 0; i < n; ++i) { int x; ss >> x; u.uids.push_back(x); } R.us.push_back(u);
        } else if (tag == "Y") { RuleY y; y.lo = D(ss); y.hi = D(ss); int nd; ss >> nd >> y.who; y.need_dual = nd; y.cy = D(ss); y.tol = D(ss); R.ys.push_back(y); }
        else if (tag == "YE") { RuleYE y; y.lo = D(ss); y.hi = D(ss); y.cy = D(ss); y.tol = D(ss); R.yes.push_back(y); }
        else if (tag == "YD") { RuleYD y; y.lo = D(ss); y.hi = D(ss); y.a = D(ss); y.b = D(ss); y.tol = D(ss); y.w = D(ss); R.yds.push_back(y); }
        else if (tag == "YC") { RuleYC y; y.lo = D(ss); y.hi = D(ss); y.cy = D(ss); y.tol = D(ss); y.w = D(ss); R.ycs.push_back(y); }
        else if (tag == "ROBOTSIDE") { R.has_robotside = true; R.rs_lo = D(ss); R.rs_hi = D(ss); int n; ss >> n; for (int i = 0; i < n; ++i) { int x; ss >> x; R.rs_uids.push_back(x); } }
        else if (tag == "TWROBOTORALL") { R.has_tro = true; R.tro_lo = D(ss); R.tro_hi = D(ss); int n; ss >> n; for (int i = 0; i < n; ++i) { int x; ss >> x; R.tro_uids.push_back(x); } }
    }
    for (size_t i = 1; i < R.marks.size(); ++i) if (R.marks[i].first < R.marks[i - 1].first) R.marks_sorted = false;
    return true;
}

static Rules RU;
static const Level* LV;

static bool allowed_mode(double x, int mode) {
    if (ANY_MODE) return true;   // 순수 모드: 모드 순서 가정 없음
    // Part.allowed_modes: 포탈 근처(x-60 ~ x+45)에서는 바뀌기 전·후 둘 다 허용
    // 빠르게: 표지가 x 순서면 이분 탐색, 메모리 할당 없음 (예전 반복문과 결과 같음). 순서가 아니면 예전 방식
    const auto& mk = RU.marks; size_t n = mk.size();
    if (!RU.marks_sorted) {
        int m = RU.start_mode; std::vector<int> ok = {m};
        for (auto& q : mk) {
            if (x > q.first + 45) { m = q.second; ok.assign(1, m); }
            else if (x > q.first - 60) ok.push_back(q.second);
            else break;
        }
        return std::find(ok.begin(), ok.end(), mode) != ok.end();
    }
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t md = (lo + hi) / 2; if (x > mk[md].first + 45) lo = md + 1; else hi = md; }
    int m = lo > 0 ? mk[lo - 1].second : RU.start_mode;
    if (m == mode) return true;
    for (size_t k = lo; k < n; ++k) {
        if (x > mk[k].first - 60) { if (mk[k].second == mode) return true; }
        else break;
    }
    return false;
}

static int count_in(const std::vector<int>& used, const std::vector<int>& uids) {
    int c = 0; for (int u : uids) if (std::binary_search(used.begin(), used.end(), u)) ++c; return c;
}

static bool out(const State& s) {
    const Player& q = s.p;
    // PartNoBound.out
    if (!RU.avoids.empty()) for (int u : RU.avoids) if (q.has_used(u)) return true;
    for (auto& m : RU.musts) if (q.x > m.first && !q.has_used(m.second)) return true;
    if (RU.env_modes.count(q.mode)) {
        auto it = RU.env.find(pyidiv(q.x, 30.0));
        if (it != RU.env.end() && !(it->second.first - 15 <= q.y && q.y <= it->second.second + 15)) return true;
    }
    if (RU.ban.count(q.mode) && q.has_ceiling) {
        double ext = q.mode == WAVE ? q.half * 2.0 : q.half;
        if (q.y + ext >= q.ceiling - 0.01 || q.y - ext <= q.floor + 0.01) return true;
    }
    for (auto& b : RU.blocks) if (b.x0 <= q.x && q.x <= b.x1 && b.y0 <= q.y && q.y <= b.y1 && (b.m < 0 || q.mode == b.m)) return true;
    {   // Part.out
        auto it = RU.floor_c.find(pyidiv(q.x, 60.0));
        double fl = it == RU.floor_c.end() ? -1e9 : it->second;
        { auto ic = CEIL_C.find(pyidiv(q.x, 60.0)); if (ic != CEIL_C.end() && q.y > ic->second) return true; }
        if (q.y < fl || (!allowed_mode(q.x, q.mode) && !(s.dual && allowed_mode(q.x, s.tw.mode)))) return true;   // 듀얼: 둘 중 하나가 그 모드면 됨 (포탈은 한쪽만 닿을 수 있음)
    }
    // 파트 전용 규칙
    for (auto& u : RU.us) {
        if (!(u.lo < q.x && q.x < u.hi)) continue;
        if (u.need_dual && !s.dual) continue;
        int c;
        if (u.who == 0) c = count_in(q.used, u.uids);
        else if (u.who == 1) c = count_in(s.tw.used, u.uids);
        else if (u.who == 2) { c = 0; for (int x : u.uids) if (q.has_used(x) || s.tw.has_used(x)) ++c; }
        else { c = 0; for (int x : u.uids) if (q.has_used(x) && s.tw.has_used(x)) ++c; }
        if (c < u.need) return true;
    }
    for (auto& y : RU.ys) {
        if (!(y.lo <= q.x && q.x < y.hi)) continue;
        if (y.need_dual && !s.dual) continue;
        double yy = y.who == 0 ? q.y : s.tw.y;
        if (std::fabs(yy - y.cy) > y.tol) return true;
    }
    for (auto& y : RU.yes) {
        if (!(y.lo <= q.x && q.x < y.hi)) continue;
        double dm = std::fabs(q.y - y.cy);
        if (s.dual) dm = std::min(dm, std::fabs(s.tw.y - y.cy));
        if (dm > y.tol) return true;
    }
    if (s.dual && RU.has_robotside && RU.rs_lo < q.x && q.x < RU.rs_hi) {
        const Player* ab[2] = {&q, &s.tw};
        for (auto a : ab) if (a->mode == ROBOT && count_in(a->used, RU.rs_uids) > 0) return true;
    }
    if (s.dual && RU.has_tro && RU.tro_lo < q.x && q.x < RU.tro_hi) {
        if (!(s.tw.mode == ROBOT || count_in(s.tw.used, RU.tro_uids) == (int)RU.tro_uids.size())) return true;
    }
    return false;
}

static bool done(const State& s) { return s.p.x >= RU.x_end && RU.end_modes.count(s.p.mode); }

// YC 빠르게 찾기: x 10칸 묶음마다 걸치는 규칙 번호 (파일 순서 그대로 → 더하는 순서도 같아서 결과 같음)
static std::unordered_map<long long, std::vector<int>> YC_IDX; static bool YC_READY = false;
static std::unordered_map<long long, std::vector<int>> YD_IDX;
static double YC_DUAL_PEN = getenv("YC_DUAL_PEN") ? atof(getenv("YC_DUAL_PEN")) : 10.0;
static void build_yc_idx() {
    for (int i = 0; i < (int)RU.ycs.size(); ++i)
        for (long long b = (long long)std::floor(RU.ycs[i].lo / 10.0); b <= (long long)std::floor(RU.ycs[i].hi / 10.0); ++b) YC_IDX[b].push_back(i);
    for (int i = 0; i < (int)RU.yds.size(); ++i)
        for (long long b = (long long)std::floor(RU.yds[i].lo / 10.0); b <= (long long)std::floor(RU.yds[i].hi / 10.0); ++b) YD_IDX[b].push_back(i);
    YC_READY = true;
}
static double soft_cost(const State& s) {
    double c = 0; const Player& q = s.p;
    auto ex = [](double d, double tol) { return d > tol ? d - tol : 0.0; };
    auto id = YD_IDX.find((long long)std::floor(q.x / 10.0));
    if (id != YD_IDX.end()) for (int yi : id->second) { const RuleYD& y = RU.yds[yi];
        if (!(y.lo <= q.x && q.x < y.hi)) continue;
        if (s.dual) {
            double c1 = ex(std::fabs(q.y - y.a), y.tol) + ex(std::fabs(s.tw.y - y.b), y.tol);
            double c2 = ex(std::fabs(q.y - y.b), y.tol) + ex(std::fabs(s.tw.y - y.a), y.tol);
            c += y.w * std::min(c1, c2);
        } else c += y.w * (std::min(ex(std::fabs(q.y - y.a), y.tol), ex(std::fabs(q.y - y.b), y.tol)) + 30.0);   // 쇼케이스는 둘인데 혼자면 벌점
    }
    auto it = YC_IDX.find((long long)std::floor(q.x / 10.0));
    if (it == YC_IDX.end()) return c;
    for (int yi : it->second) { const RuleYC& y = RU.ycs[yi];
        if (!(y.lo <= q.x && q.x < y.hi)) continue;
        double dm = std::fabs(q.y - y.cy);
        if (s.dual) dm = std::min(dm, std::fabs(s.tw.y - y.cy));
        if (dm > y.tol) c += y.w * (dm - y.tol);
        if (s.dual && YC_DUAL_PEN > 0) c += y.w * YC_DUAL_PEN;   // 쇼케이스는 아이콘 한 무리인데 우리는 듀얼 → 벌점
    }
    return c;
}
static double tick_cost(const State& s) {
    const Player& q = s.p;
    if (q.mode != WAVE) return 0;
    double h = q.half, best = RU.near;
    thread_local std::vector<int> qb;
    G_CURX = q.x; G_TR = &s.trig;
    LV->query(q.x - h - RU.near, q.x + h + RU.near, q.y - h - RU.near, q.y + h + RU.near, qb);
    for (int u : qb) {
        const Obj& o = OB(*LV, u);
        if (o.kind != K_HAZARD && o.kind != K_SAW) continue;
        double d;
        if (o.kind == K_SAW) d = std::hypot(o.x - q.x, o.y - q.y) - o.radius - h;
        else {
            double dx = std::max(std::max(o.minx - (q.x + h), (q.x - h) - o.maxx), 0.0);
            double dy = std::max(std::max(o.miny - (q.y + h), (q.y - h) - o.maxy), 0.0);
            d = std::hypot(dx, dy);
        }
        if (d < best) best = d;
    }
    return best < RU.near ? (RU.near - best) * RU.near_w : 0;
}

// ---- 상태 키 (Player.key + 둘째 키)
static inline uint64_t hmix(uint64_t h, uint64_t v) { v *= 0x9E3779B97F4A7C15ULL; h ^= v + 0x7F4A7C159E3779B9ULL + (h << 6) + (h >> 2); return h; }
static inline uint64_t hd(double d) { d += 0.0; uint64_t u; std::memcpy(&u, &d, 8); return u; }
static uint64_t pkey_hash(const Player& p, uint64_t h) {
    h = hmix(h, hd(p.x)); h = hmix(h, hd(p.y)); h = hmix(h, hd(p.y_vel)); h = hmix(h, p.speed);
    h = hmix(h, (p.mini) | (p.upside_down << 1) | (p.holding_jump << 2) | (p.jump_unused << 3) | (p.can_jump << 4) | (p.on_ground << 5) |
                (p.is_jumping << 6) | (p.is_dashing << 7) | (p.velocity_uncapped << 8) | (p.is_on_slope << 9) | (p.robot_boost_invalidated << 10) |
                (p.spider_hold << 11) | (p.has_ceiling << 12));
    { size_t n = p.used.size(); for (size_t i = n > 4 ? n - 4 : 0; i < n; ++i) h = hmix(h, (uint64_t)(int64_t)p.used[i]); }   // 끝 4개만 (같음 판정은 key_eq가 전부)
    h = hmix(h, p.used.size()); h = hmix(h, (uint64_t)(int64_t)p.slope); h = hmix(h, p.mode); h = hmix(h, hd(p.robot_boost_charge));
    h = hmix(h, hd(p.floor)); if (p.has_ceiling) h = hmix(h, hd(p.ceiling));
    return h;
}
static uint64_t key_hash(const State& s) { uint64_t h = pkey_hash(s.p, 1469598103934665603ULL); h = hmix(h, s.dual); if (s.dual) h = pkey_hash(s.tw, h); return h; }
static bool pkey_eq(const Player& a, const Player& b) {
    return a.x == b.x && a.y == b.y && a.y_vel == b.y_vel && a.speed == b.speed && a.mini == b.mini && a.upside_down == b.upside_down &&
           a.holding_jump == b.holding_jump && a.jump_unused == b.jump_unused && a.can_jump == b.can_jump && a.on_ground == b.on_ground &&
           a.is_jumping == b.is_jumping && a.is_dashing == b.is_dashing && a.velocity_uncapped == b.velocity_uncapped && a.used == b.used &&
           a.is_on_slope == b.is_on_slope && a.slope == b.slope && a.mode == b.mode && a.robot_boost_charge == b.robot_boost_charge &&
           a.robot_boost_invalidated == b.robot_boost_invalidated && a.floor == b.floor && a.has_ceiling == b.has_ceiling &&
           (!a.has_ceiling || a.ceiling == b.ceiling) && a.spider_hold == b.spider_hold;
}
static bool key_eq(const State& a, const State& b) {
    if (!pkey_eq(a.p, b.p) || a.dual != b.dual) return false;
    if (a.trig.sig != b.trig.sig) return false;                 // 상태 트리거: 길에 따라 달라지는 것 (터치·아이템·토글 오브·충돌)
    return !a.dual || pkey_eq(a.tw, b.tw);
}
// bucket
struct BKey { long long v[20]; int n; bool operator==(const BKey& o) const { return n == o.n && std::equal(v, v + n, o.v); } };
struct BHash { size_t operator()(const BKey& k) const { uint64_t h = 7; for (int i = 0; i < k.n; ++i) h = hmix(h, (uint64_t)k.v[i]); return h; } };
static BKey bucket(const State& s) {
    const Player& p = s.p; BKey k; int i = 0;
    k.v[i++] = p.mode; k.v[i++] = pyidiv(p.y, 1.0); k.v[i++] = (long long)(p.y_vel * 2); k.v[i++] = p.holding_jump; k.v[i++] = p.jump_unused;
    k.v[i++] = p.upside_down; k.v[i++] = p.mini; k.v[i++] = p.speed; k.v[i++] = p.on_ground; k.v[i++] = p.is_dashing; k.v[i++] = (long long)p.used.size();
    k.v[i++] = s.dual;
    if (s.dual) {
        const Player& t = s.tw;
        k.v[i++] = t.mode; k.v[i++] = pyidiv(t.y, 2.0); k.v[i++] = (long long)(t.y_vel * 2); k.v[i++] = t.upside_down; k.v[i++] = t.on_ground;
        k.v[i++] = t.holding_jump; k.v[i++] = (long long)t.used.size();
    }
    k.n = i; return k;
}

struct Entry { double cost; State st; int pid; bool hold; int lc = -1000; };   // lc = 마지막으로 누름/뗌이 바뀐 틱

static int MIN_GAP = getenv("MIN_GAP") ? atoi(getenv("MIN_GAP")) : 0;
#ifndef NO_SEARCH_MAIN
int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "사용: search 레벨.txt 규칙.txt cap 출력.txt\n"); return 2; }
    Level L; State start;
    if (!load_level(argv[1], L, start)) { fprintf(stderr, "레벨 못 읽음\n"); return 1; }
    if (!load_rules(argv[2], RU)) { fprintf(stderr, "규칙 못 읽음\n"); return 1; }
    build_yc_idx();
    LV = &L; int cap = atoi(argv[3]);
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

    std::vector<Entry> layer; layer.push_back({0, start, 0, false});
    std::vector<std::vector<std::pair<int, bool>>> hist;
    auto trace = [&](int tt_end, int i) {
        std::vector<int> outv;
        for (int tt = tt_end; tt >= 0; --tt) { auto pr = hist[tt][i]; outv.push_back(pr.second); i = pr.first; }
        std::reverse(outv.begin(), outv.end()); return outv;
    };
    auto write = [&](const std::string& head, const std::vector<int>& v) {
        FILE* f = fopen(argv[4], "w"); fprintf(f, "%s\n", head.c_str());
        for (size_t i = 0; i < v.size(); ++i) fprintf(f, "%d%c", v[i], i + 1 < v.size() ? ' ' : '\n');
        if (v.empty()) fprintf(f, "\n");
        fclose(f);
    };
    std::vector<Entry> nxt; nxt.reserve(cap * 3);
    int done_extra = getenv("DONE_EXTRA") ? atoi(getenv("DONE_EXTRA")) : 0;
    bool have_best = false; double best_cost = 0; int first_done = 0; std::vector<int> best_path;
    // 같은 상태 찾기용 표 (열린 주소 방식: 해시 → nxt 번호). 같은 상태는 하나만 있으므로 결과는 전과 같음
    std::vector<uint64_t> th; std::vector<int> ti; size_t tmask = 0;
    for (int t = 0; t < 40000; ++t) {
        nxt.clear();
        { size_t need = 1; while (need < (size_t)layer.size() * 4 + 64) need <<= 1;
          if (th.size() != need) { th.assign(need, 0); ti.assign(need, -1); } else std::fill(ti.begin(), ti.end(), -1); tmask = need - 1; }
        std::vector<bool> choices;
        auto fi = RU.forced.find(t);
        if (fi != RU.forced.end()) choices = {(bool)fi->second}; else choices = {false, true};
        // 1) 펼치기는 여러 코어로 (상태마다 독립), 2) 합치기는 원래 순서대로 한 코어로 → 결과는 한 코어일 때와 같음
        const int nc = (int)choices.size(), NL = (int)layer.size();
        static std::vector<State> qs; static std::vector<char> okv; static std::vector<double> costv; static std::vector<uint64_t> hv;
        qs.resize((size_t)NL * nc); okv.assign((size_t)NL * nc, 0); costv.resize((size_t)NL * nc); hv.resize((size_t)NL * nc);
        #pragma omp parallel for schedule(dynamic, 64)
        for (int li = 0; li < NL; ++li) {
            const Entry& e = layer[li];
            for (int ci = 0; ci < nc; ++ci) {
                bool hold = choices[ci]; size_t j = (size_t)li * nc + ci;
                // MIN_GAP: 떼자마자 다시 누르기(뗀 지 MIN_GAP틱 안)는 안 함 — 사람이 못 하는 1프레임 다시 누르기 막기 (기본 0 = 끔)
                if (MIN_GAP > 0 && hold && !e.hold && t - e.lc < MIN_GAP && t > 0) continue;
                State& q = qs[j]; q = e.st;
                step(q, L, hold);
                if (q.p.dead || out(q) || (q.p.x >= RU.x_end && !done(q))) continue;
                double pc = q.p.mode == WAVE ? RU.wave_press : RU.press_cost;
                double s2 = e.cost + ((hold && !e.st.p.holding_jump) ? pc : 0) + ((hold && q.p.mode == CUBE) ? 1 : 0);
                s2 += tick_cost(q) + soft_cost(q);
                okv[j] = 1; costv[j] = s2; hv[j] = key_hash(q);
            }
        }
        for (int li = 0; li < NL; ++li) {
            for (int ci = 0; ci < nc; ++ci) {
                size_t j = (size_t)li * nc + ci; if (!okv[j]) continue;
                bool hold = choices[ci]; State& q = qs[j]; double s2 = costv[j]; uint64_t h = hv[j];
                int found = -1;
                size_t slot = (size_t)(h ^ (h >> 29)) & tmask;
                while (ti[slot] >= 0) {
                    if (th[slot] == h && key_eq(nxt[ti[slot]].st, q)) { found = ti[slot]; break; }
                    slot = (slot + 1) & tmask;
                }
                if (found < 0) { th[slot] = h; ti[slot] = (int)nxt.size(); nxt.push_back({s2, std::move(q), li, hold, hold != layer[li].hold ? t : layer[li].lc}); }
                else if (s2 < nxt[found].cost) { nxt[found].cost = s2; nxt[found].st = std::move(q); nxt[found].pid = li; nxt[found].hold = hold; nxt[found].lc = hold != layer[li].hold ? t : layer[li].lc; }
            }
        }
        if (nxt.empty()) {
            int fb = 0; for (int i = 1; i < (int)layer.size(); ++i) if (layer[i].st.p.x > layer[fb].st.p.x) fb = i;
            std::vector<int> partial = hist.empty() ? std::vector<int>() : trace((int)hist.size() - 1, fb);
            char b[200]; snprintf(b, sizeof b, "FAIL %d %.1f %.1f %s", t, layer[fb].st.p.x, layer[fb].st.p.y, MODE_NAME[layer[fb].st.p.mode]);
            write(b, partial); printf("실패: 틱 %d에서 살아남는 경로 없음 (가장 멀리 x %.1f y %.1f, %s) (%.1f s)\n", t, layer[fb].st.p.x, layer[fb].st.p.y, MODE_NAME[layer[fb].st.p.mode], secs());
            return 1;
        }
        std::vector<int> keep;   // nxt 인덱스 (남길 순서)
        if ((int)nxt.size() > cap) {
            std::unordered_map<BKey, int, BHash> best; std::vector<int> bestOrder;   // bucket → bestOrder 자리, bestOrder = nxt 인덱스 (처음 본 순서)
            for (int i = 0; i < (int)nxt.size(); ++i) {
                BKey b = bucket(nxt[i].st);
                auto it = best.find(b);
                if (it == best.end()) { best.emplace(b, (int)bestOrder.size()); bestOrder.push_back(i); }
                else if (nxt[i].cost < nxt[bestOrder[it->second]].cost) {
                    bestOrder[it->second] = i;   // 파이썬: best[b] = kk (같은 칸 자리 유지, 값만 바뀜)
                }
            }
            std::vector<int> k1 = bestOrder;
            std::stable_sort(k1.begin(), k1.end(), [&](int a, int b) {
                long long la = -(long long)nxt[a].st.p.used.size(), lb = -(long long)nxt[b].st.p.used.size();
                if (la != lb) return la < lb; return nxt[a].cost < nxt[b].cost; });
            if ((int)k1.size() > cap / 2) k1.resize(cap / 2);
            std::vector<char> in1(nxt.size(), 0); for (int x : k1) in1[x] = 1;
            std::vector<int> k2 = bestOrder;
            std::stable_sort(k2.begin(), k2.end(), [&](int a, int b) { return nxt[a].cost < nxt[b].cost; });
            keep = k1; int room = cap - (int)k1.size();
            for (int x : k2) { if (room <= 0) break; if (!in1[x]) { keep.push_back(x); --room; } }
        } else { keep.resize(nxt.size()); for (int i = 0; i < (int)nxt.size(); ++i) keep[i] = i; }
        std::vector<std::pair<int, bool>> back; back.reserve(keep.size());
        std::vector<Entry> nl; nl.reserve(keep.size());
        for (int i : keep) { back.push_back({nxt[i].pid, nxt[i].hold}); nl.push_back(std::move(nxt[i])); }
        hist.push_back(std::move(back)); layer.swap(nl);
        if (t % 500 == 0 || ((int)layer.size() >= cap && t % 50 == 0)) {
            int fb = 0; for (int i = 1; i < (int)layer.size(); ++i) if (layer[i].st.p.x > layer[fb].st.p.x) fb = i;
            printf("  틱 %d: 상태 %zu개, 가장 앞 x %.0f (%s) (%.1f s)\n", t, layer.size(), layer[fb].st.p.x, MODE_NAME[layer[fb].st.p.mode], secs());
            fflush(stdout);
        }
        int bi = -1;
        for (int i = 0; i < (int)layer.size(); ++i) if (done(layer[i].st)) { if (bi < 0 || layer[i].cost < layer[bi].cost) bi = i; }
        if (bi >= 0 && (!have_best || layer[bi].cost < best_cost)) {
            best_path = trace((int)hist.size() - 1, bi); best_cost = layer[bi].cost;
            if (!have_best) first_done = t;
            have_best = true;
        }
        // DONE_EXTRA: 처음 끝에 닿은 뒤에도 몇 틱 더 찾아서 비용이 가장 낮은 길을 고름 (늦게 닿는 길도 후보: 쇼케이스가 느린 속도 포탈을 탈 때)
        if (have_best && t >= first_done + done_extra) {
            char b[64]; snprintf(b, sizeof b, "OK %zu", best_path.size()); write(b, best_path);
            printf("찾음: %zu 틱 (비용 %.0f, %.1f s)\n", best_path.size(), best_cost, secs());
            return 0;
        }
        if (have_best) {   // 끝난 상태는 더 펼치지 않음
            std::vector<Entry> nl2; std::vector<std::pair<int, bool>>& hb = hist.back(); std::vector<std::pair<int, bool>> hb2;
            for (int i = 0; i < (int)layer.size(); ++i) if (!done(layer[i].st)) { nl2.push_back(std::move(layer[i])); hb2.push_back(hb[i]); }
            hb.swap(hb2); layer.swap(nl2);
            if (layer.empty()) { char b[64]; snprintf(b, sizeof b, "OK %zu", best_path.size()); write(b, best_path);
                printf("찾음: %zu 틱 (비용 %.0f, %.1f s)\n", best_path.size(), best_cost, secs()); return 0; }
        }
    }
    printf("시간 초과\n"); return 1;
}
#endif
