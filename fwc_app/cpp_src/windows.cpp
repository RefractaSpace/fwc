// windows.cpp — partrun.py 의 프레임 윈도우 계산(finish3 + windows4)을 C++로 옮긴 것 (같은 순서·같은 판정)
// 사용: ./windows 레벨.txt 기본규칙.txt 입력.txt 출력.txt [HORIZON] [REJOIN_CAP]
//   입력.txt  = 0/1 목록 (탐색 결과 둘째 줄과 같은 형식)
//   출력.txt  = "EV n" + 이벤트 틱 목록, 그다음 줄마다 "W 틱 press/release 앞 뒤 윈도우 모드 방법"
// 파이썬 기본값: MAXS 20, ALIVE_HORIZON(HORIZON) 720, REJOIN 120~360, REJOIN_CAP 150, NEAR_REJOIN 켬
#define NO_SEARCH_MAIN
#include "search.cpp"
#include <map>
#include <thread>
#include <atomic>

static const Level* WL;
static int MAXS = 20, HORIZON = 720, REJOIN_MIN = 120, REJOIN_MAX = 360, REJOIN_CAP = 150;
static State START;
static bool PRESS_ONLY = false;
static int WR0 = -1, WR1 = -1;                                       // 시험용 (환경변수 WRANGE="a b")
static const char* WMODE_NAME[8] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};
static bool impulse(int m) { return m == CUBE || m == UFO || m == BALL || m == SPIDER; }

static std::vector<char> inputs_of(const std::vector<int>& ev_in, int n) {
    std::vector<int> ev = ev_in; std::sort(ev.begin(), ev.end());
    std::vector<char> out(n); bool s = false; size_t j = 0;
    for (int t = 0; t < n; ++t) { while (j < ev.size() && ev[j] == t) { s = !s; ++j; } out[t] = s; }
    return out;
}
static std::vector<int> events_of(const std::vector<char>& in) {
    std::vector<int> ev; bool prev = false;
    for (int t = 0; t < (int)in.size(); ++t) { if ((bool)in[t] != prev) ev.push_back(t); prev = in[t]; }
    return ev;
}
// simulate: (done 여부, 틱마다 상태)
static bool simulate(const std::vector<char>& in, std::vector<State>& st) {
    State p = START; st.clear(); st.reserve(in.size());
    for (char h : in) { step(p, *WL, h); st.push_back(p); if (p.p.dead) return false; }
    return done(p);
}
static bool forced_has(int t) { return RU.forced.count(t) > 0; }

static bool alive_from(const std::vector<State>& ref, const std::vector<char>& in, int start_t, int end_t) {
    State p = start_t == 0 ? START : ref[start_t - 1];
    for (int t = start_t; t < (int)in.size(); ++t) {
        step(p, *WL, in[t]);
        if (p.p.dead || out(p)) return false;
        if (t > end_t && t < (int)ref.size() && key_eq(p, ref[t])) return true;
        if (done(p)) return true;
        if (HORIZON && t > end_t + HORIZON) return true;
    }
    bool last = in.empty() ? false : in.back();
    while (!done(p) && p.p.tick < (long long)in.size() + 600) { step(p, *WL, last); if (p.p.dead) return false; }
    return done(p);
}

// phys_key 같음
static bool phys_eq(const State& a, const State& b) {
    const Player &p = a.p, &q = b.p;
    if (!(p.x == q.x && p.y == q.y && p.y_vel == q.y_vel && p.mode == q.mode && p.upside_down == q.upside_down && p.mini == q.mini &&
          p.speed == q.speed && p.used == q.used && p.is_dashing == q.is_dashing && p.on_ground == q.on_ground)) return false;
    if (a.dual != b.dual) return false;
    if (a.trig.sig != b.trig.sig) return false;
    if (!a.dual) return true;
    const Player &s = a.tw, &t = b.tw;
    return s.y == t.y && s.y_vel == t.y_vel && s.mode == t.mode && s.upside_down == t.upside_down && s.mini == t.mini &&
           s.used == t.used && s.is_dashing == t.is_dashing && s.on_ground == t.on_ground;
}

static std::vector<int> extend_holds(std::vector<int> ev, int n) {
    std::vector<State> ref; bool ok = simulate(inputs_of(ev, n), ref);
    if (!ok) { fprintf(stderr, "extend_holds: 기준 경로 죽음\n"); exit(3); }
    for (int i = 0; i + 1 < (int)ev.size(); ++i) {
        if (!inputs_of(ev, n)[ev[i]]) continue;
        int p_t = ev[i], r_t = ev[i + 1];
        if (!impulse(ref[p_t].p.mode)) continue;
        int nxt_p = i + 2 < (int)ev.size() ? ev[i + 2] : n;
        int best = r_t;
        for (int r2 = nxt_p - 1; r2 > r_t; --r2) {
            if (forced_has(r2)) continue;
            std::vector<int> ev2(ev.begin(), ev.begin() + i + 1); ev2.push_back(r2); ev2.insert(ev2.end(), ev.begin() + i + 2, ev.end());
            auto inp2 = inputs_of(ev2, n);
            State q = p_t > 0 ? ref[p_t - 1] : START; bool same = true;
            for (int t = p_t; t < std::min(nxt_p, n); ++t) {
                step(q, *WL, inp2[t]);
                if (q.p.dead || !phys_eq(q, ref[t])) { same = false; break; }
            }
            if (same) { best = r2; break; }
        }
        ev[i + 1] = best;
    }
    return ev;
}

// 버퍼 밀기 공통: ev2 만들기 (실패면 false)
static bool push_shift(const std::vector<int>& ev, int n, const std::vector<int>& idx, int d, std::vector<int>& ev2) {
    ev2 = ev;
    for (int k : idx) ev2[k] += d;
    int lo = *std::min_element(idx.begin(), idx.end()), hi = *std::max_element(idx.begin(), idx.end());
    int k = lo - 1;
    while (k >= 0 && ev2[k] >= ev2[k + 1]) { ev2[k] = ev2[k + 1] - 1; --k; }
    k = hi + 1;
    while (k < (int)ev2.size() && ev2[k] <= ev2[k - 1]) { ev2[k] = ev2[k - 1] + 1; ++k; }
    if (ev2[0] < 0 || ev2.back() >= n) return false;
    if (!RU.forced.empty()) {                                  // 강제 입력이 없으면 확인할 것 없음 (빠르게)
        std::set<int> old(ev.begin(), ev.end());
        for (int x : ev2) if (!old.count(x) && forced_has(x)) return false;
    }
    return true;
}

static bool shift_ok3(const std::vector<State>& ref, const std::vector<int>& ev, int n, const std::vector<int>& idx, int d) {
    std::vector<int> ev2; if (!push_shift(ev, n, idx, d, ev2)) return false;
    int mn = INT32_MAX, mx = INT32_MIN;
    for (size_t k = 0; k < ev.size(); ++k) if (ev2[k] != ev[k]) { mn = std::min({mn, ev[k], ev2[k]}); mx = std::max({mx, ev[k], ev2[k]}); }
    auto inp = inputs_of(ev2, n);
    return alive_from(ref, inp, std::max(0, mn - 1), mx + 1);
}

static bool near_one(const Player& a, const Player& b) {
    return a.mode == b.mode && a.upside_down == b.upside_down && a.on_ground == b.on_ground && a.mini == b.mini &&
           std::fabs(a.y - b.y) <= 1.0 && std::fabs(a.y_vel - b.y_vel) <= 1.0 && a.used.size() == b.used.size();
}
static bool near_state(const State& q, const State& r) {
    if (!near_one(q.p, r.p)) return false;
    if (q.dual != r.dual) return false;
    if (q.trig.sig != r.trig.sig) return false;
    return q.dual ? near_one(q.tw, r.tw) : true;
}

static bool recover(const std::vector<State>& ref, const std::vector<char>& in, int start_t, int end_t, const std::vector<int>& ev_after) {
    State p = start_t == 0 ? START : ref[start_t - 1];
    for (int t = start_t; t <= end_t; ++t) {
        step(p, *WL, in[t]);
        if (p.p.dead || out(p)) return false;
        if (done(p)) return true;
    }
    std::vector<int> nx; for (int e : ev_after) if (e > end_t) nx.push_back(e);
    int T = nx.size() > 1 ? nx[1] + 30 : end_t + REJOIN_MIN;
    T = std::min({std::max(T, end_t + REJOIN_MIN), end_t + REJOIN_MAX, (int)ref.size() - 1});
    // 파이썬 dict 순서 그대로: 버킷마다 처음 넣은 상태만, 넣은 순서대로
    std::vector<State> layer; layer.push_back(p);
    for (int t = end_t + 1; t <= T; ++t) {
        std::vector<State> nxt; nxt.reserve(layer.size() * 2); std::unordered_map<BKey, int, BHash> seen; seen.reserve(layer.size() * 2);
        for (const State& q0 : layer) {
            for (int h = 0; h < 2; ++h) {
                State q = q0; step(q, *WL, h);
                if (q.p.dead || out(q)) continue;
                if (done(q)) return true;
                if (key_eq(q, ref[t])) return true;
                if (near_state(q, ref[t])) return true;
                BKey b = bucket(q);
                if (!seen.count(b)) { seen[b] = (int)nxt.size(); nxt.push_back(std::move(q)); }
            }
        }
        if (nxt.empty()) return false;
        if ((int)nxt.size() > REJOIN_CAP) {
            const State& r = ref[t];
            std::vector<int> ord(nxt.size()); for (size_t i = 0; i < ord.size(); ++i) ord[i] = (int)i;
            auto sc = [&](int i) { return std::fabs(nxt[i].p.y - r.p.y) + (nxt[i].p.mode == r.p.mode ? 0.0 : 1e6); };
            std::stable_sort(ord.begin(), ord.end(), [&](int a, int b) { return sc(a) < sc(b); });
            std::vector<State> keep; keep.reserve(REJOIN_CAP);
            for (int i = 0; i < REJOIN_CAP; ++i) keep.push_back(std::move(nxt[ord[i]]));
            nxt.swap(keep);
        }
        layer.swap(nxt);
    }
    const State& r = ref[T];
    for (auto& q : layer) if (q.p.mode == r.p.mode && std::fabs(q.p.y - r.p.y) <= 15 && q.p.upside_down == r.p.upside_down) return true;
    return false;
}

static bool shift_ok4(const std::vector<State>& ref, const std::vector<int>& ev, int n, const std::vector<int>& idx, int d) {
    std::vector<int> ev2; if (!push_shift(ev, n, idx, d, ev2)) return false;
    int a = INT32_MAX, b = INT32_MIN;
    for (size_t k = 0; k < ev.size(); ++k) if (ev2[k] != ev[k]) { a = std::min({a, ev[k], ev2[k]}); b = std::max({b, ev[k], ev2[k]}); }
    auto inp = inputs_of(ev2, n);
    if (alive_from(ref, inp, std::max(0, a - 1), b + 1)) return true;
    return recover(ref, inp, std::max(0, a - 1), b, ev2);
}

struct WRes { int tick; bool press; int early, late, window, mode; bool pair; };

template <class F>
static std::vector<WRes> windows_gen(const std::vector<int>& ev, int n, F shift_ok, const char* tag, double t0) {
    auto inputs = inputs_of(ev, n);
    std::vector<State> ref; if (!simulate(inputs, ref)) { fprintf(stderr, "기준 경로 죽음\n"); exit(3); }
    std::vector<WRes> res(ev.size());
    // 입력마다 따로 계산 (서로 영향 없음) → 여러 스레드로 나눔. 결과는 순번 자리에 넣어서 1스레드와 같음
    std::atomic<int> next(0), finished(0);
    auto work = [&]() {
        while (true) {
            int i = next.fetch_add(1);
            if (i >= (int)ev.size()) break;
            int t = ev[i]; int mode = ref[t].p.mode;
            if (WR0 >= 0 && (t < WR0 || t > WR1)) { res[i] = {t, (bool)inputs[t], 0, 0, 0, mode, false}; ++finished; continue; }   // 시험용: 이 틱 범위만
            if (PRESS_ONLY && !inputs[t]) {                         // 떼기는 안 잼 (exe: 누름만 셈 → 시간 반)
                res[i] = {t, false, 0, 0, 0, mode, false}; ++finished; continue;
            }
            std::vector<std::vector<int>> methods = {{i}};
            if (!impulse(mode) && i + 1 < (int)ev.size()) methods.push_back({i, i + 1});
            int bm = -1, be = 0, bl = 0;
            for (int mi = 0; mi < (int)methods.size(); ++mi) {
                int e = 0; while (e < MAXS && shift_ok(ref, ev, n, methods[mi], -(e + 1))) ++e;
                int l = 0; while (l < MAXS && shift_ok(ref, ev, n, methods[mi], l + 1)) ++l;
                if (bm < 0 || e + l > be + bl) { bm = mi; be = e; bl = l; }
            }
            res[i] = {t, (bool)inputs[t], be, bl, be + bl + 1, mode, bm == 1};
            int f = ++finished;
            if (f % 10 == 0) fprintf(stderr, "  [%s] 입력 %d/%zu (%.1f s)\n", tag, f, ev.size(),
                                     std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - t0);
        }
    };
    int nt = (int)std::thread::hardware_concurrency(); if (nt < 1) nt = 1;
    if (getenv("WTHREADS")) nt = std::max(1, atoi(getenv("WTHREADS")));
    std::vector<std::thread> th;
    for (int k = 1; k < nt; ++k) th.emplace_back(work);
    work();
    for (auto& x : th) x.join();
    return res;
}

int main(int argc, char** argv) {
    if (argc < 5) { fprintf(stderr, "사용: windows 레벨.txt 기본규칙.txt 입력.txt 출력.txt [HORIZON] [REJOIN_CAP]\n"); return 2; }
    static Level L;
    if (!load_level(argv[1], L, START)) { fprintf(stderr, "레벨 못 읽음\n"); return 1; }
    if (!load_rules(argv[2], RU)) { fprintf(stderr, "규칙 못 읽음\n"); return 1; }
    WL = &L; LV = &L; build_yc_idx();
    PRESS_ONLY = getenv("PRESS_ONLY") && atoi(getenv("PRESS_ONLY"));
    if (getenv("WRANGE")) sscanf(getenv("WRANGE"), "%d %d", &WR0, &WR1);
    if (argc > 5) HORIZON = atoi(argv[5]);
    if (argc > 6) REJOIN_CAP = atoi(argv[6]);
    std::vector<char> raw; { std::ifstream in(argv[3]); std::string t; while (in >> t) raw.push_back(t == "1"); }
    int n = (int)raw.size();
    double t0 = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::vector<State> st;
    if (getenv("PROBE") && atoi(getenv("PROBE"))) {          // 재생만: 몇 틱 살아남나 (매크로 틱 맞춤 고르기용)
        bool track = getenv("TRACK") && atoi(getenv("TRACK"));  // 틱마다 x y 모드도 씀 (exe가 파이썬 재생 대신 씀)
        bool track2 = getenv("TRACK2") && atoi(getenv("TRACK2"));
        int trackg = getenv("TRACKG") ? atoi(getenv("TRACKG")) : 0;   // 시험용: 이 그룹 첫 물체 위치도 씀 (상태 트리거)
        State p = START; int t = 0;
        FILE* f = fopen(argv[4], "w");
        for (; t < n; ++t) {
            step(p, *WL, raw[t]);
            if (track) {                                                // 세계 좌표 (방향 반대·게임 회전이면 로컬과 다름) + 로컬 x (진행 거리)
                double wx = p.p.x, wy = p.p.y; if (L.strig && !p.trig.fr.id()) p.trig.fr.to_world(p.p.x, p.p.y, wx, wy);
                fprintf(f, "T %.17g %.17g %d %.17g\n", wx, wy, p.p.mode, p.p.x);
            }
            if (track2)                                                 // 시험용: 듀얼 두 플레이어 상태 (틱, 듀얼, P1 y·속도·중력·바닥·천장·땅·누름·누름다씀, P2 같은 값·모드)
                fprintf(f, "D %d %d %.3f %.3f %d %.1f %.1f %d %d %d %.3f %.3f %d %.1f %.1f %d %d %d %d\n", t, (int)p.dual,
                        p.p.y, p.p.y_vel, (int)p.p.upside_down, p.p.floor, p.p.has_ceiling ? p.p.ceiling : -1.0, (int)p.p.on_ground, (int)p.p.holding_jump, (int)p.p.spider_hold,
                        p.tw.y, p.tw.y_vel, (int)p.tw.upside_down, p.tw.floor, p.tw.has_ceiling ? p.tw.ceiling : -1.0, (int)p.tw.on_ground, (int)p.tw.holding_jump, (int)p.tw.spider_hold, p.tw.mode);
            if (trackg > 0 && L.strig) { double gx, gy; if (anc_pos(L, p.trig, trackg, gx, gy)) fprintf(f, "G %d %.17g %.17g %d\n", trackg, gx, gy, (int)group_on(L, p.trig, trackg)); }
            if (p.p.dead) break;
        }
        fprintf(f, "ALIVE %d %d %.6f\n", t, p.p.dead ? 1 : 0, p.p.x);
        if (p.p.dead) fprintf(f, "CAUSE %s\n", p.p.cause.c_str());
        fclose(f);
        return 0;
    }
    if (!simulate(raw, st)) { fprintf(stderr, "기준 경로가 끝까지 못 감\n"); return 3; }
    // finish3 (MEASURE_ONLY=1이면 건너뜀: 사용자가 녹화한 매크로는 고치지 않고 윈도우만 잼)
    bool measure_only = getenv("MEASURE_ONLY") && atoi(getenv("MEASURE_ONLY"));
    auto ev = measure_only ? events_of(raw) : extend_holds(events_of(raw), n);
    for (int rep = 0; rep < (measure_only ? 0 : 2); ++rep) {
        auto res = windows_gen(ev, n, shift_ok3, "w3", t0);
        for (int k = 0; k < (int)res.size(); ++k) {
            int sh = res[k].late - res[k].early;
            sh = (int)std::floor(sh / 2.0);        // 파이썬 // (음수는 내림)
            if (sh == 0) continue;
            std::vector<int> ev2 = ev;
            ev2[k] += sh; if (res[k].pair) ev2[k + 1] += sh;
            std::vector<int> srt(ev2); std::sort(srt.begin(), srt.end()); srt.erase(std::unique(srt.begin(), srt.end()), srt.end());
            std::vector<State> tmp;
            if (ev2 == srt && simulate(inputs_of(ev2, n), tmp)) ev = ev2;
        }
        ev = extend_holds(ev, n);
    }
    auto res = windows_gen(ev, n, shift_ok4, "w4", t0);
    FILE* f = fopen(argv[4], "w");
    fprintf(f, "EV %zu", ev.size()); for (int e : ev) fprintf(f, " %d", e); fprintf(f, "\n");
    for (auto& r : res) fprintf(f, "W %d %s %d %d %d %s %s\n", r.tick, r.press ? "press" : "release", r.early, r.late, r.window, WMODE_NAME[r.mode], r.pair ? "pair" : "single");
    fclose(f);
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - t0;
    fprintf(stderr, "끝: 입력 %zu개, %.1f s\n", ev.size(), el);
    return 0;
}
