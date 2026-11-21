// FWC 윈도우 카운터 — 지오드 모드 (기능 1: 게임 안 실시간 표시 + 누른 자리 고리)
// 하는 일: exe가 계산해 둔 결과(레벨 이름.json)를 읽어서, 플레이 중 누를 때마다 그 누름의 윈도우 칸 개수를 올리고
//          왼쪽 위에 칸별 개수, 왼쪽 아래에 진행률과 σ/s를 오버레이와 같은 모양으로 보여줌.
// 결과 파일 위치: <지메 폴더>/geode/config/fwc.window-counter/levels/<레벨 이름>.json  (exe의 "지오드로 보내기" 버튼이 넣음)
// 게임 파일은 건드리지 않음 (읽기만: 레벨 이름, 진행 틱, 플레이어 x)
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/utils/file.hpp>
#include <tulip/TulipHook.hpp>
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <array>
#include <climits>
#include <cmath>
#include <optional>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <fstream>
#include <sstream>
#include <limits>
#include <filesystem>
#include <iterator>
#include "gdr2.hpp"

using namespace geode::prelude;

namespace fwc {

// 칸 이름·색 (오버레이 overlay.py 와 같음)
static constexpr int NBIN = 9;
static const std::array<const char*, NBIN> BIN_NAME = {"17+", "13+", "9+", "7-8", "5-6", "4", "3", "2", "1"};
static const std::array<ccColor3B, NBIN> BIN_COLOR = {{
    {255, 107, 230}, {197, 140, 255}, {127, 124, 255}, {108, 198, 255}, {107, 227, 107},
    {255, 255, 255}, {255, 227, 77}, {255, 171, 61}, {255, 77, 77}}};

static int binOf(int w) {
    if (w >= 17) return 0;
    if (w >= 13) return 1;
    if (w >= 9) return 2;
    if (w >= 7) return 3;
    if (w >= 5) return 4;
    if (w == 4) return 5;
    if (w == 3) return 6;
    if (w == 2) return 7;
    return 8;
}

static constexpr int MAX_SHOWN_WINDOW = 20;   // 이 값보다 넓은 윈도우는 화면에 표시하지 않음
struct Press { int tick; int window; bool down = true; double wf = 0; };   // down=false 면 뗌(릴리즈). wf: CBF 세기 결과(프레임, 소수), 없으면 0

struct LevelData {
    std::vector<Press> presses;      // 계산된 경로의 누름 (틱 순서)
    std::vector<double> sigmaRun;    // 누름 i까지의 σ/s (precision_run)
    double levelEnd = 0;             // 진행률 100% 기준 x
};

// 지금 레벨에서 쓰는 상태
static std::optional<LevelData> g_data;
static std::array<int, NBIN> g_count{};
static std::vector<bool> g_used;     // 계산 누름 중 이미 센 것
static int g_lastIdx = -1;           // 마지막으로 센 누름 번호 (σ/s 표시용)
static std::vector<Ref<CCNode>> g_rings;   // 누른 자리 고리 (레벨 화면에 붙어 같이 움직임)
static bool g_showRings = true;            // 설정 "show-rings" (레벨 시작 때 한 번 읽음)
static size_t g_ringCull = 0;              // 화면 밖 고리 숨기기를 이어서 할 위치

static std::filesystem::path levelsDir() {
    return Mod::get()->getConfigDir() / "levels";
}

static std::optional<LevelData> loadFor(std::string const& name) {
    std::string safe = name;                                   // exe와 같은 규칙: 파일 이름에 못 쓰는 글자는 _
    for (auto& ch : safe) if (std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos) ch = '_';
    auto path = levelsDir() / (safe + ".json");
    if (!std::filesystem::exists(path)) {
        log::info("결과 파일 없음: {}", path.string());
        return std::nullopt;
    }
    auto res = utils::file::readJson(path);
    if (!res) {
        log::warn("결과 파일 읽기 실패: {}", res.unwrapErr());
        return std::nullopt;
    }
    auto& j = res.unwrap();
    LevelData d;
    if (j.contains("level_end")) d.levelEnd = j["level_end"].asDouble().unwrapOr(0.0);
    if (j.contains("windows")) {
        if (auto arr = j["windows"].asArray()) {
            for (auto const& w : arr.unwrap()) {
                auto kind = w["kind"].asString().unwrapOr("");
                if (kind != "press" && kind != "release") continue;
                d.presses.push_back({(int)w["tick"].asInt().unwrapOr(0), (int)w["window"].asInt().unwrapOr(1), kind == "press",
                                     w.contains("window_f") ? w["window_f"].asDouble().unwrapOr(0.0) : 0.0});
            }
        }
    }
    if (j.contains("precision_run")) {
        if (auto arr = j["precision_run"].asArray()) {
            for (auto const& v : arr.unwrap()) d.sigmaRun.push_back(v.asDouble().unwrapOr(0.0));
        }
    }
    log::info("결과 파일 읽음: {} (누름 {}개)", path.string(), d.presses.size());
    return d;
}

// 누른 자리 고리: 오버레이와 같은 모양 (720p에서 바깥 반지름 26px, 색 두께 10px, 검은 테두리 3px, 왼쪽에 윈도우 숫자)
// 화면 1px = 게임 1/2.31칸 → 반지름 11.3칸, 두께 4.3칸, 테두리 1.3칸.
// drawCircle 의 테두리가 반지름 바깥쪽으로 그려진다고 보고 안쪽 반지름을 7.0칸으로 잡음 (추정, 게임에서 확인 필요)
static CCNode* makeRing(int window, double wf, CCPoint pos) {
    auto col3 = BIN_COLOR[binOf(window)];
    ccColor4F col = {col3.r / 255.f, col3.g / 255.f, col3.b / 255.f, 1.f};
    ccColor4F black = {0.f, 0.f, 0.f, 1.f}, clear = {0.f, 0.f, 0.f, 0.f};
    auto node = CCNode::create();
    auto dn = CCDrawNode::create();
    float rIn = 7.0f, th = 4.3f, edge = 1.3f;
    dn->drawCircle({0.f, 0.f}, rIn - edge, clear, th + 2.f * edge, black, 48);
    dn->drawCircle({0.f, 0.f}, rIn, clear, th, col, 48);
    node->addChild(dn);
    auto lab = CCLabelBMFont::create((wf > 0 ? fmt::format("{:.1f}", wf) : std::to_string(window)).c_str(), "bigFont.fnt");
    lab->setAnchorPoint({1.f, 0.5f}); lab->setScale(0.38f); lab->setColor(col3);
    lab->setPosition({-(rIn + th) - 2.f, 0.f});
    node->addChild(lab);
    node->setPosition(pos);
    return node;
}

static void clearRings() {
    for (auto& r : g_rings) if (r) r->removeFromParent();
    g_rings.clear();
    g_ringCull = 0;
}

static void resetCounts() {
    clearRings();
    g_count.fill(0);
    g_lastIdx = -1;
    if (g_data) g_used.assign(g_data->presses.size(), false);
}

// 실제 누른 틱 → 가장 가까운 아직 안 센 계산 누름 (허용 범위: 그 누름 윈도우와 3틱 중 큰 쪽)
static int matchPress(int tick, bool down = true) {
    if (!g_data) return -1;
    auto const& ps = g_data->presses;
    int best = -1, bestD = 1 << 30;
    // 틱 순서라 이분 탐색으로 근처만 봄
    auto it = std::lower_bound(ps.begin(), ps.end(), tick, [](Press const& p, int t) { return p.tick < t; });
    int c = (int)(it - ps.begin());
    for (int i = std::max(0, c - 8); i < std::min((int)ps.size(), c + 8); ++i) {
        if (g_used[i] || ps[i].down != down) continue;
        int d = std::abs(ps[i].tick - tick);
        int lim = std::max(3, ps[i].window);
        if (d <= lim && d < bestD) { bestD = d; best = i; }
    }
    return best;
}

}  // namespace fwc

using namespace fwc;

// 화면 표시 노드
class FwcHud : public CCNode {
public:
    std::array<CCLabelBMFont*, NBIN> m_nums{};
    CCLabelBMFont* m_pct = nullptr;
    CCLabelBMFont* m_sig = nullptr;

    static FwcHud* create() {
        auto r = new FwcHud();
        if (r->init()) { r->autorelease(); return r; }
        delete r; return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        auto ws = CCDirector::get()->getWinSize();
        float sc = 0.42f, lh = ws.height / 14.5f;     // 글자 크기·줄 간격 (오버레이 720p 배치를 화면 크기에 맞춤)
        for (int i = 0; i < NBIN; ++i) {
            float y = ws.height - 12.f - lh * (i + 0.5f);
            auto name = CCLabelBMFont::create((std::string(BIN_NAME[i]) + ":").c_str(), "bigFont.fnt");
            name->setAnchorPoint({0.f, 0.5f}); name->setScale(sc); name->setPosition({8.f, y}); name->setColor(BIN_COLOR[i]);
            this->addChild(name);
            auto num = CCLabelBMFont::create("0", "bigFont.fnt");
            num->setAnchorPoint({0.f, 0.5f}); num->setScale(sc); num->setPosition({8.f + ws.width * 0.075f, y}); num->setColor(BIN_COLOR[i]);
            this->addChild(num);
            m_nums[i] = num;
        }
        m_pct = CCLabelBMFont::create("0.00%", "bigFont.fnt");
        m_pct->setAnchorPoint({0.f, 0.5f}); m_pct->setScale(sc); m_pct->setPosition({8.f, 12.f + lh * 1.2f});
        this->addChild(m_pct);
        m_sig = CCLabelBMFont::create("0.00", "bigFont.fnt");
        m_sig->setAnchorPoint({0.f, 0.5f}); m_sig->setScale(sc); m_sig->setPosition({8.f, 12.f + lh * 0.2f});
        this->addChild(m_sig);
        return true;
    }

    // 글자는 값이 바뀔 때만 다시 만든다 (CCLabelBMFont::setString 은 글자 스프라이트를 새로 만들어서, 매 프레임 부르면 렉의 원인)
    std::array<int, NBIN> m_lastCount{};
    int m_lastPct = -1, m_lastSig = -1;           // 0.01 단위 정수로 비교 (화면에 보이는 자릿수와 같음)
    bool m_first = true;
    void refresh(float playerX) {
        for (int i = 0; i < NBIN; ++i) {
            if (!m_first && g_count[i] == m_lastCount[i]) continue;
            m_lastCount[i] = g_count[i];
            m_nums[i]->setString(std::to_string(g_count[i]).c_str());
        }
        if (g_data && g_data->levelEnd > 0) {
            double p = std::clamp(playerX / g_data->levelEnd * 100.0, 0.0, 100.0);
            int pi = (int)std::lround(p * 100.0);
            if (m_first || pi != m_lastPct) { m_lastPct = pi; m_pct->setString(fmt::format("{:.2f}%", p).c_str()); }
        }
        double s = 0.0;
        if (g_data && g_lastIdx >= 0 && g_lastIdx < (int)g_data->sigmaRun.size()) s = g_data->sigmaRun[g_lastIdx];
        int si = (int)std::lround(s * 100.0);
        if (m_first || si != m_lastSig) { m_lastSig = si; m_sig->setString(fmt::format("{:.2f}", s).c_str()); }
        m_first = false;
    }
};

// 2.2081 실측: m_currentProgress 는 물리 1틱(1/240초)에 2씩 오른다 (첫 틱에 2). 매크로·계산 결과의 틱 번호 = progress / 2
static inline int fwcTick(unsigned progress) { return (int)(progress / 2); }

// ── 틱 사이 입력 (CBF 방식) ──
//   입력 시각 = 틱 + 위치(0 이상 1 미만). 위치 0 = 지금까지와 같은 "틱 시작" 입력.
//   위치는 1/8000초(= 0.03틱) 단위로 맞춤. 1틱 = 33.33칸이라 딱 나누어떨어지지 않으므로, 칸은 틱마다 0부터 다시 셈:
//   위치 = k × 0.03틱 (k = 0..33). 시각 번호 u = 틱 × 34 + k (순서 비교용 정수)
static constexpr int SUB_HZ = 8000;
static constexpr double SUB_TICK = 240.0 / SUB_HZ;                 // 1/8000초를 틱으로 = 0.03틱
static constexpr int SUB_K = 34;                                   // 한 틱 안 칸 번호 0..33
struct In {
    bool down = false; float frac = 0.f;
    In(bool d = false, float f = 0.f) : down(d), frac(f) {}
    operator bool() const { return down; }                       // 예전 코드(누름만 보던 곳)는 그대로 동작
};
using Inputs = std::map<int, std::vector<In>>;
static long long subUnits(double t) {                                                     // 시각(틱) → 시각 번호
    double T = std::floor(t + 1e-9); long long k = std::llround((t - T) / SUB_TICK);
    if (k >= SUB_K) { T += 1; k = 0; }                                                     // 틱 끝에 붙으면 다음 틱 시작
    return (long long)T * SUB_K + k;
}
static void subSplit(long long u, int& tick, float& frac) {                              // 시각 번호 → 틱 + 위치
    tick = (int)(u / SUB_K); frac = (float)((u % SUB_K) * SUB_TICK);
}
static void inputsAdd(Inputs& m, double t, bool down) {                                  // 시각 t(틱) 에 입력 하나 (1/8000초에 맞춤)
    int tk; float fr; subSplit(subUnits(t), tk, fr);
    auto& v = m[tk]; In x(down, fr);
    auto it = v.begin(); while (it != v.end() && it->frac <= fr) ++it;                    // 같은 틱 안에서는 위치 순서
    v.insert(it, x);
}
static bool inputsRemove(Inputs& m, int tick, bool down, float frac) {                   // 그 틱의 같은 입력 하나 빼기
    auto f = m.find(tick); if (f == m.end()) return false;
    auto& v = f->second;
    for (auto it = v.begin(); it != v.end(); ++it) if (it->down == down && std::fabs(it->frac - frac) < 1e-6f) { v.erase(it); if (v.empty()) m.erase(f); return true; }
    return false;
}

// ── 매크로 재생. 모드 설정 "매크로 재생"을 켜면 설정 폴더 macro.txt 를 재생 ──
//   macro.txt: 줄마다 "틱 1" (누름) 또는 "틱 0" (뗌), 세 번째 칸이 있으면 틱 안 위치(0~1, CBF 방식 입력).
//   이클립스 macro.gdr2 를 넣으면 자동으로 macro.txt 로 바뀜
struct Macro {
    bool on = false;
    Inputs inputs;
    int lastTick = -1;
    bool heldAt(int t) const { bool h = false; for (auto const& kv : inputs) { if (kv.first > t) break; for (bool dn : kv.second) h = dn; } return h; }
};
static Macro g_mac;
// 매크로 파일 읽기: 줄마다 "틱 누름 [틱 안 위치]". 위치가 없거나 범위 밖이면 0 (틱 시작). 읽은 입력 수를 돌려줌
static int macroRead(std::filesystem::path const& path, Inputs& out) {
    std::ifstream f(path);
    std::string line; int n = 0;
    while (std::getline(f, line)) {
        std::istringstream ls(line); int t, d; double fr = 0.0;
        if (!(ls >> t >> d)) continue;
        if (!(ls >> fr) || fr < 0.0 || fr >= 1.0) fr = 0.0;
        inputsAdd(out, t + fr, d != 0); ++n;
    }
    return n;
}
// 이클립스 .gdr2 → macro.txt 변환. 성공하면 기존 macro.txt 는 macro_prev.txt 로 복사해 둠. 결과 문장을 돌려줌 ("OK: ..." / "FAILED: ...")
static std::string macroConvertGdr2(std::filesystem::path const& src, std::filesystem::path const& dir) {
    std::error_code ec;
    auto dst = dir / "macro.txt";
    std::string bytes;
    { std::ifstream in(src, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
    std::vector<std::pair<int, bool>> ev; FwcGdr2Info info; std::string err;
    if (!fwcReadGdr2(bytes, ev, info, err)) return "FAILED: " + err;
    if (std::fabs(info.tps - 240.0) > 0.01) return fmt::format("FAILED: macro is {} TPS, this mod needs 240 TPS", info.tps);
    if (ev.empty()) return "FAILED: no player 1 jump inputs";
    if (std::filesystem::exists(dst, ec)) std::filesystem::copy_file(dst, dir / "macro_prev.txt", std::filesystem::copy_options::overwrite_existing, ec);
    {
        std::ofstream o(dst, std::ios::binary | std::ios::trunc);
        for (auto const& e : ev) o << e.first << ' ' << (e.second ? 1 : 0) << "\n";
    }
    return fmt::format("OK: {} inputs (level '{}', skipped: {} player-2 inputs, {} left/right inputs)", ev.size(), info.levelName, info.p2Inputs, info.otherButtons);
}
static void macroNote(std::filesystem::path const& dir, std::string const& msg) {
    std::ofstream o(dir / "macro_import.txt", std::ios::binary | std::ios::trunc); o << msg << "\n";
}
// 설정 폴더에 macro.gdr2 를 직접 넣은 경우: macro.txt 보다 새것(또는 macro.txt 없음)이면 변환
static void macroImportGdr2(std::filesystem::path const& dir) {
    std::error_code ec;
    auto src = dir / "macro.gdr2", dst = dir / "macro.txt";
    if (!std::filesystem::exists(src, ec)) return;
    if (std::filesystem::exists(dst, ec)) {
        auto ts = std::filesystem::last_write_time(src, ec); if (ec) return;
        auto td = std::filesystem::last_write_time(dst, ec); if (ec) return;
        if (ts <= td) return;                                              // 이미 변환했음
    }
    macroNote(dir, "macro.gdr2 -> " + macroConvertGdr2(src, dir));
}
// 게임 안 설정 "Macro file" 에서 고른 파일: 처음 고르거나 파일이 바뀌었을 때만 macro.txt 로 가져옴
//   (.gdr2 는 변환, .txt 는 그대로 복사 — FWC 로 녹화한 틱 안 위치도 유지). 가져온 뒤에는 macro.txt 를 쓰므로 자동 고침 결과도 남음.
//   어떤 파일을 언제 가져왔는지는 macro_source.txt 에 적어 둠
static void macroImportPicked(std::filesystem::path const& dir) {
    std::filesystem::path src;
    try { src = Mod::get()->getSettingValue<std::filesystem::path>("macro-file"); } catch (...) { return; }
    std::error_code ec;
    if (src.empty() || !std::filesystem::is_regular_file(src, ec)) return;
    auto mt = std::filesystem::last_write_time(src, ec); if (ec) return;
    std::string stamp = fmt::format("{}|{}", src.string(), (long long)mt.time_since_epoch().count());
    {   std::ifstream f(dir / "macro_source.txt"); std::string old; std::getline(f, old);
        if (old == stamp) return; }                                       // 이미 가져온 파일
    std::string ext = src.extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    std::string msg;
    if (ext == ".txt") {
        Inputs tmp;
        int n = macroRead(src, tmp);
        if (n <= 0) msg = "FAILED: no inputs in the text file";
        else {
            if (std::filesystem::exists(dir / "macro.txt", ec)) std::filesystem::copy_file(dir / "macro.txt", dir / "macro_prev.txt", std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::copy_file(src, dir / "macro.txt", std::filesystem::copy_options::overwrite_existing, ec);
            msg = ec ? "FAILED: copy error " + ec.message() : fmt::format("OK: {} inputs (text)", n);
        }
    }
    else msg = macroConvertGdr2(src, dir);                                // .gdr2 (이클립스)
    macroNote(dir, src.filename().string() + " -> " + msg);
    if (msg.rfind("OK", 0) == 0) { std::ofstream f(dir / "macro_source.txt", std::ios::binary | std::ios::trunc); f << stamp << "\n"; }
}
static void macroLoad() {
    g_mac = Macro{};
    auto dir = Mod::get()->getConfigDir();
    if (!Mod::get()->getSettingValue<bool>("macro-play")) return;          // 설정: 모드가 매크로 재생
    macroImportGdr2(dir);                                                  // 설정 폴더에 macro.gdr2 (이클립스 매크로)가 새로 들어왔으면 macro.txt 로 바꿔 둔다
    macroImportPicked(dir);                                                // 게임 설정에서 고른 매크로 파일 (새로 고른 경우만)
    int n = macroRead(dir / "macro.txt", g_mac.inputs);
    g_mac.on = n > 0;
}

// ── 게임 안에서 윈도우 세기 ──
//   매크로(macro.txt)를 게임이 직접 돌리면서, 누름 하나씩 "당기다 죽으면 기록 → 클릭으로 돌아와 미루기"를 한다. 엔진 계산 없음.
//   되돌리기는 버튼을 뗀 틱에서만 한다 (시험에서 그 조건이면 틱마다 완전히 같게 재현됨을 확인).
struct Counter {
    static constexpr int MAXS = 20;      // 한쪽으로 최대 몇 틱까지 옮겨 보나 (기존 계산기와 같음. 20+20+1=41 → 17+ 칸까지 구분 가능)
    static constexpr int HORIZON = 720;  // 옮긴 뒤 이만큼(3초) 더 살아 있으면 무조건 통과
    static constexpr int REJOIN_MIN = 120, REJOIN_MAX = 360, NEAR_Y = 15;   // 그 전이라도 "다다음 입력+30틱"(120~360틱 사이)에 살아서 원래 경로 근처(15 이내)면 통과 — 기존 계산기 판정과 같은 기준
    bool on = false, busy = false, internal = false, done = false, failed = false;
    bool forced = false;                       // 메뉴에서 직접 시작: 이미 센 결과가 있어도 다시 셈
    std::string sig;                            // 이번 세기의 조건 지문 (레벨·매크로·시작 지점·CBF·판정 방식)
    bool armed = false;                        // 일시정지 메뉴의 "FWC Count" 를 누름 → 지금 시작 지점에서 다시 시작해 세기
    bool dead = false, completed = false, useTrial = false;
    std::vector<std::pair<int, bool>> ev;      // (틱, 누름?) 순서대로
    std::vector<float> evFrac;                 // ev 와 같은 순서의 틱 안 위치 (CBF 입력, 보통 0)
    std::vector<int> pressIdx;                 // ev 안에서 누름의 번호
    size_t cur = 0;
    CheckpointObject* cp = nullptr; int cpTick = -1;
    struct R { int tick, early, late; bool pair, down; double earlyF = 0, lateF = 0; int dieE = -1, dieL = -1; };   // dieE/dieL: 처음 실패한 시도가 입력 뒤 몇 틱에 죽었나 (-1: 죽음이 아니라 규칙으로 실패)   // earlyF/lateF: CBF 세기에서 틱 안까지 잰 값(틱), 아니면 early/late 와 같음
    bool cbf = false;                          // 설정 "1/8000초 단위로 세기"
    std::vector<R> res;
    std::map<int, std::pair<int, int>> validShift;   // 잰 입력의 될 수 있는 위치: (앞으로 몇 틱, 뒤로 몇 틱)
    std::vector<int> unmeasured;               // 되돌린 상태가 원래와 달라서 잴 수 없었던 입력 (틱)
    bool refDied = false;                      // 이번 입력의 원래 경로(되돌린 상태에서 그대로 재생)가 중간에 죽나 = 매크로 끝
    static constexpr int REC_MAX = 12;         // 다음 입력을 다시 맞출 때 최대 몇 틱까지 옮겨 보나 (다다음 입력·옮긴 입력을 넘지는 않음)
    int lastDie = -1;                          // 마지막 시도가 죽은 틱 (-1: 안 죽음)
    int recTrials = 0, recovered = 0;
    int freshCps = 0;                          // 처음부터 다시 돌려 저장 지점을 만든 횟수          // 다음 클릭으로 되살리기: 해 본 횟수, 통과한 횟수
    Inputs trial;    // 이번 시도에 쓰는 입력
    std::chrono::steady_clock::time_point t0;
    long long ticksRun = 0; int trials = 0, resets = 0;
    double msReset = 0, msStep = 0;            // 되돌리기 / 진행에 쓴 시간
    std::string levelName, msg, calib;         // calib: 틱 맞춤 자동 찾기 기록
    int offset = 0;                            // 찾은 틱 맞춤 (매크로 틱 + offset = 게임 틱)
    double goalX = 0;                          // 0 이 아니면: 캐릭터 x 가 여기에 닿으면 "끝까지 감"으로 침 (매크로 합치기에서 앞부분만 볼 때)
    std::string merge;                         // 합치기 기록
    bool calibOk = false;                      // 그대로 재생해서 끝까지 갔나
    std::string repair;                        // 자동 고침 기록
    struct P3 { float x, y; double v; float rot; };
    std::vector<P3> fullRef;                   // 화면 없이 돌린 전체 경로 (틱 번호로 찾음). 실제 재생과 비교용
    long long chkTicks = 0, chkPosDiff = 0, chkRotDiff = 0;   // 되돌린 뒤 다시 돌린 경로 vs 처음부터 돌린 경로: 비교한 틱 / 위치 다른 틱 / 회전만 다른 틱
    int repairs = 0;
    CCLabelBMFont* label = nullptr;
};
static Counter g_cnt;
// 틱 나누기 상태
struct SubIn { float frac; bool down; };
static std::vector<SubIn> g_sub;                 // 이번 틱 안에서 넣을 입력 (위치 순)
static bool g_subMid = false, g_subP2Done = false, g_subRot1 = false, g_subRot2 = false;
static float g_subRot = 0.f;
static bool g_subStep = false;                   // 이번 틱을 나눠 돌렸나 (비행기 회전 보정용)
static float g_shipRotDelta = 0.f;               // 0 이 아니면 회전 보간을 이 시간에 비례하게 바꿈
static CCPoint g_subP1Pos, g_subP2Pos;
static Inputs g_live;                            // FWC 방식 녹화: 받은 실제 입력을 넣을 틱
static float g_stepX0 = 0.f, g_stepDx = 0.f;     // CBF 모드 켠 채 녹화: 틱 시작 x, 직전 틱 이동량
// ── 매크로 녹화 (이 모드에서) ──
//   방식(설정 record-cbf): off = 틱 단위(지금까지와 같음) / fwc = 이 모드가 틱 사이 입력을 직접 처리(CBF 모드는 잠시 끔) /
//                          cbf-mod = CBF 모드를 켠 채 녹화하고, 입력 순간 캐릭터가 그 틱에서 얼마나 움직였는지로 위치를 추정
//   연습 모드에서 죽고 이어서 찍으면, 되돌아간 틱 뒤의 입력은 지우고 이어 붙임 (이클립스와 같은 방식)
//   레벨을 깨면 macro.txt 로 저장 (기존 것은 macro_prev.txt), 나갈 때는 macro_rec_last.txt 로 저장
struct MacroRec {
    bool on = false; int mode = 0;                 // 0 off, 1 fwc, 2 cbf-mod
    std::vector<std::pair<long long, bool>> ev;    // (1/8000초 번호, 누름?)
    bool held = false;
};
static MacroRec g_mrec;
static double g_framePrev = 0.0, g_frameNow = 0.0;    // 이번 프레임 시작·직전 프레임 시작 (초, QueryPerformanceCounter 기준)
static std::vector<PlayerButtonCommand> g_carry;      // 아직 시각이 안 된 입력 (다음 프레임으로)
static void mrecAdd(double t, bool down) {
    if (!g_mrec.on) return;
    if (down == g_mrec.held) return;                            // 상태가 안 바뀌는 입력은 버림 (처음 뗌 포함)
    g_mrec.ev.push_back({subUnits(t), down}); g_mrec.held = down;
}
static std::string mrecText() {
    std::string o;
    for (auto const& e : g_mrec.ev) {
        int tk; float fr; subSplit(e.first, tk, fr);
        o += fr > 0.f ? fmt::format("{} {} {:.6f}\n", tk, e.second ? 1 : 0, fr) : fmt::format("{} {}\n", tk, e.second ? 1 : 0);
    }
    return o;
}
// CBF 모드(syzzi) 잠시 끄기: 이 모드가 매크로를 재생하거나 FWC 방식으로 녹화할 때 둘이 같이 틱을 나누면 안 됨 (이클립스도 봇 쓸 때 끔)
static int g_cbfRestore = -1;                          // -1 건드리지 않음, 0/1 = 나갈 때 되돌릴 원래 값
static void cbfModPause(bool pause) {
    auto cbf = Loader::get()->getLoadedMod("syzzi.click_between_frames");
    if (!cbf) return;
    if (pause) { if (g_cbfRestore < 0) g_cbfRestore = cbf->getSettingValue<bool>("soft-toggle") ? 1 : 0; cbf->setSettingValue<bool>("soft-toggle", true); }
    else if (g_cbfRestore >= 0) { cbf->setSettingValue<bool>("soft-toggle", g_cbfRestore == 1); g_cbfRestore = -1; }
}
static void cntBuildEv() {
    g_cnt.ev.clear(); g_cnt.evFrac.clear(); g_cnt.pressIdx.clear();
    for (auto const& kv : g_mac.inputs) for (In const& x : kv.second) { g_cnt.ev.push_back({kv.first, x.down}); g_cnt.evFrac.push_back(x.frac); }
    for (int i = 0; i < (int)g_cnt.ev.size(); ++i) if (g_cnt.ev[i].second) g_cnt.pressIdx.push_back(i);
}

// ── 영상 녹화 ──
//   매크로를 화면 없이 돌릴 때와 같은 방식(틱마다 애니메이션 → 물리)으로 게임을 직접 진행시키면서,
//   영상 한 프레임(60fps = 4틱)마다 화면을 별도 버퍼에 그려 ffmpeg 로 넘긴다. 실제 시간과 무관하게 정확히 60fps 로 찍힌다.
//   ffmpeg 위치는 설정 폴더 ffmpeg_path.txt, 결과는 설정 폴더 showcase.mp4 (끝나고 레벨 노래를 입힘, 정보는 record_info.txt)
struct Recorder {
    bool want = false, active = false, done = false;
    int w = 1920, h = 1080, fps = 60;          // 1080p 60fps
    double introSec = 0, songStart = 0;        // 노래 입히기: 소개 화면 길이, 녹화 시작 때 노래 위치(초)
    std::string ffmpeg, song;
    GLuint fbo = 0, tex = 0;
    std::vector<unsigned char> buf;
    HANDLE hWrite = nullptr; HANDLE hProc = nullptr;
    int frames = 0, endTick = 0;
    std::string info;
    std::chrono::steady_clock::time_point t0;
};
static Recorder g_rec;
// 게임을 직접 진행시키는 동안에는 화면 애니메이션(액션)을 틱마다 직접 돌리므로, 원래 프레임마다 도는 것은 잠시 멈춘다 (두 번 돌면 큐브 회전이 빨라져 판정이 달라짐)
static void fwcActionsAuto(bool on) {
    auto d = CCDirector::get();
    if (on) d->getScheduler()->resumeTarget(d->getActionManager());
    else d->getScheduler()->pauseTarget(d->getActionManager());
}
static bool g_autorun = false;
static int g_autorunStartPos = 0;
static StartPosObject* g_pendingStartPos = nullptr;   // 자동 실행: 첫 프레임에 바꿀 스타트포스           // autorun.txt 둘째 줄: 시작할 스타트포스 번호 (1 = x 가 가장 작은 것, 0 = 레벨 처음)
// 지금 이 게임 창이 맨 앞(입력 받는 창)인가
static bool fwcFocused() {
    HWND fg = GetForegroundWindow(); DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}
// 일시정지를 막아야 하나: 자동 실행 중이면 항상, 세는 중·녹화 중이면 창이 뒤로 갔을 때 (직접 ESC 로 멈추는 것은 허용)
static bool fwcBlockPause();        // autorun.txt 로 자동 실행 중 (자리에 없을 때): 창이 뒤로 가도 일시정지하지 않음
static bool recOpen(std::string const& ffmpeg, std::string const& out) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 1 << 24)) return false;
    SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 0);
    std::string cmd = fmt::format("\"{}\" -y -f rawvideo -pix_fmt rgba -s {}x{} -r {} -i - -vf vflip -c:v libx264 -preset veryfast -crf 15 -pix_fmt yuv420p \"{}\"",
                                  ffmpeg, g_rec.w, g_rec.h, g_rec.fps, out);
    STARTUPINFOA si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = hRead;
    si.hStdOutput = nullptr; si.hStdError = nullptr;
    PROCESS_INFORMATION pi{};
    std::vector<char> cl(cmd.begin(), cmd.end()); cl.push_back(0);
    BOOL ok = CreateProcessA(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hRead);
    if (!ok) { CloseHandle(hWrite); return false; }
    CloseHandle(pi.hThread);
    g_rec.hWrite = hWrite; g_rec.hProc = pi.hProcess;
    return true;
}
static void recClose() {
    if (g_rec.hWrite) { CloseHandle(g_rec.hWrite); g_rec.hWrite = nullptr; }
    if (g_rec.hProc) { WaitForSingleObject(g_rec.hProc, 60000); CloseHandle(g_rec.hProc); g_rec.hProc = nullptr; }
    if (g_rec.fbo) { glDeleteFramebuffers(1, &g_rec.fbo); g_rec.fbo = 0; }
    if (g_rec.tex) { glDeleteTextures(1, &g_rec.tex); g_rec.tex = 0; }
}

// ── 클릭 소리 ──
static bool g_clickOn = false;
static std::string g_clickPress, g_clickRelease;
static void clickLoad() {
    g_clickOn = Mod::get()->getSettingValue<bool>("click-sounds");
    if (!g_clickOn) return;
    std::error_code ec;
    auto user = Mod::get()->getConfigDir() / "clicks";            // 내 소리를 넣으면 그걸 씀
    auto res = Mod::get()->getResourcesDir();
    auto pick = [&](const char* userName, const char* resName) {
        auto u = user / userName;
        return std::filesystem::exists(u, ec) ? u.string() : (res / resName).string();
    };
    g_clickPress = pick("press.wav", "fwc_press.wav");
    g_clickRelease = pick("release.wav", "fwc_release.wav");
    auto fm = FMODAudioEngine::sharedEngine();          // 첫 클릭에 파일을 읽느라 끊기지 않게 미리 불러 둠
    fm->preloadEffect(g_clickPress);
    fm->preloadEffect(g_clickRelease);
}


static bool fwcBlockPause() {
    if (g_autorun || g_rec.want || g_rec.active || g_cnt.busy) return !fwcFocused();   // 창이 뒤로 가서 생기는 자동 일시정지만 막음 (앞에서 ESC 는 됨)
    return false;
}

class $modify(FwcPlayLayer, PlayLayer) {
    struct Fields {
        FwcHud* m_hud = nullptr;
        std::vector<StartPosObject*> m_startPos;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        fwcSetup();
        return true;
    }
    // FWC 준비: 레벨을 열 때, 그리고 일시정지 메뉴에서 FWC 를 켜고 끌 때
    //   enabled 가 꺼져 있으면 FWC 전체(표시·매크로 재생·세기·녹화)를 쓰지 않음
    void fwcSetup() {
        bool en = Mod::get()->getSettingValue<bool>("enabled");
        if (m_fields->m_hud) { m_fields->m_hud->removeFromParent(); m_fields->m_hud = nullptr; }
        clearRings();
        g_data = en ? loadFor(std::string(m_level->m_levelName)) : std::nullopt;
        macroLoad();
        if (!en) g_mac.on = false;
        clickLoad();
        g_showRings = Mod::get()->getSettingValue<bool>("show-rings");
        g_rec = Recorder{};
        g_rec.want = g_mac.on && Mod::get()->getSettingValue<bool>("record-video");
        g_cnt = Counter{};
        g_cnt.on = g_mac.on && Mod::get()->getSettingValue<bool>("count-windows");      // 설정이 켜져 있으면 레벨을 열자마자 셈
        g_cnt.levelName = std::string(m_level->m_levelName);
        g_cnt.cbf = Mod::get()->getSettingValue<bool>("count-cbf");
        g_mrec = MacroRec{}; g_live.clear(); g_sub.clear(); g_carry.clear();
        g_mrec.on = en && !g_mac.on && Mod::get()->getSettingValue<bool>("macro-record");
        {   auto m = Mod::get()->getSettingValue<std::string>("record-cbf");
            g_mrec.mode = m == "fwc" ? 1 : (m == "cbf-mod" ? 2 : 0); }
        if (g_mac.on || (g_mrec.on && g_mrec.mode == 1)) { cbfModPause(true); m_clickBetweenSteps = false; m_clickOnSteps = false; }
        else cbfModPause(false);
        if (g_mac.on) {                                 // 입력 목록 만들기 (나중에 메뉴에서 시작할 수도 있으므로 항상)
            cntBuildEv();
            if (g_cnt.pressIdx.empty()) { g_cnt.on = false; g_cnt.ev.clear(); g_cnt.evFrac.clear(); }
        }
        resetCounts();
        if (g_data) {
            auto hud = FwcHud::create();
            hud->setZOrder(1000);
            this->addChild(hud);
            m_fields->m_hud = hud;
            hud->refresh(0.f);
        }
    }

    // ───────── 게임 안 윈도우 세기 ─────────
    struct Sig { float x, y; double v; bool up, dual; int mode; float y2; double v2;
        bool operator==(Sig const& o) const { return x == o.x && y == o.y && v == o.v && up == o.up && dual == o.dual && mode == o.mode && y2 == o.y2 && v2 == o.v2; } };
    Sig cntSig() {
        auto p = m_player1;
        int mode = (int)p->m_isShip | ((int)p->m_isBird << 1) | ((int)p->m_isBall << 2) | ((int)p->m_isDart << 3) | ((int)p->m_isRobot << 4) | ((int)p->m_isSpider << 5) | ((int)p->m_isSwing << 6) | ((p->m_vehicleSize < 1.0f ? 1 : 0) << 7);
        bool dual = m_gameState.m_isDualMode && m_player2;
        return {p->getPositionX(), p->getPositionY(), p->m_yVelocity, (bool)p->m_isUpsideDown, dual, mode, dual ? m_player2->getPositionY() : 0.f, dual ? m_player2->m_yVelocity : 0.0};
    }
    int cntTick() { return fwcTick(m_gameState.m_currentProgress); }
    void cntStep() {                       // 화면 없이 1틱
        auto t0 = std::chrono::steady_clock::now();
        g_cnt.internal = true;
        // 실제 게임은 프레임마다 화면 애니메이션(큐브 회전 등)을 먼저 진행한다. 이걸 빼면 기울어진 가시와의 충돌 판정이 실제와 달라진다 (시험으로 확인: 넣으면 실제와 같은 틱에서 죽음)
        static_cast<CCObject*>(CCDirector::get()->getActionManager())->update(1.f / 240.f);
        GJBaseGameLayer::update(1.f / 240.f);
        g_cnt.internal = false;
        ++g_cnt.ticksRun;
        g_cnt.msStep += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    bool cntStepTo(int target) {           // target 틱까지 진행. 죽거나 끝나면 false
        const int limit = (target - cntTick()) + 64;       // 헛도는 틱 여유 포함, 한 번만 계산
        for (int g = 0; cntTick() < target && g < limit; ++g) {
            cntStep();
            if (g_cnt.dead || g_cnt.completed) return false;
        }
        return cntTick() == target;
    }
    void cntRestore() {                    // 저장 지점으로 되돌리기 (연습 모드의 다시 시작을 그대로 씀)
        auto t0 = std::chrono::steady_clock::now();
        m_isPracticeMode = true;
        this->resetLevel();
        g_cnt.msReset += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        g_cnt.dead = false; g_cnt.completed = false;
        g_mac.lastTick = cntTick();      // 이 틱까지의 입력은 저장 지점에 이미 반영돼 있음
        ++g_cnt.resets;
    }
    void cntMakeCp() {
        g_cnt.cp = this->createCheckpoint();
        g_cnt.cp->retain();
        this->storeCheckpoint(g_cnt.cp);
        g_cnt.cpTick = cntTick();
    }
    void cntDropCp() {
        if (!g_cnt.cp) return;
        this->removeCheckpoint(false);
        g_cnt.cp->release(); g_cnt.cp = nullptr;
    }
    // 되돌릴 틱 고르기: T - MAXS - 2 이하에서, 앞뒤 2틱 동안 버튼이 계속 떼어져 있는 가장 늦은 틱
    int cntRestoreTick(int T) {
        for (int t = T - Counter::MAXS - 2; t >= 6; --t) {
            bool ok = true;
            for (int k = -2; k <= 2 && ok; ++k) ok = !g_mac.heldAt(t + k);
            if (ok) return t;
        }
        return -1;
    }
    void cntSetLabel(std::string const& txt) {
        if (!g_cnt.label) {
            g_cnt.label = CCLabelBMFont::create("", "bigFont.fnt");
            g_cnt.label->setScale(0.5f);
            auto ws = CCDirector::get()->getWinSize();
            g_cnt.label->setPosition({ws.width / 2.f, ws.height - 40.f});
            g_cnt.label->setZOrder(1001);
            this->addChild(g_cnt.label);
        }
        g_cnt.label->setString(txt.c_str());
    }
    // 틱 맞춤 자동 찾기: 매크로 전체를 0, -1, +1, -2, +2 틱 밀어서 화면 없이 끝까지 돌려 보고, 가장 멀리 가는 값을 씀.
    //   (이클립스가 시작 지점에 따라 틱 번호를 다르게 셀 수 있어서, 직접 돌려 보고 정함)
    void cntCalibrate() {
        static const int offs[5] = {0, -1, 1, -2, 2};
        const auto base = g_mac.inputs;
        if (base.empty()) return;
        int lastEv = base.rbegin()->first;
        auto shifted = [&](int off) {
            Inputs sh;
            for (auto const& kv : base) { auto& v = sh[std::max(1, kv.first + off)]; v.insert(v.end(), kv.second.begin(), kv.second.end()); }
            return sh;
        };
        int best = 0, bestTick = -1;
        for (int off : offs) {
            g_mac.inputs = shifted(off);
            g_cnt.useTrial = false;
            m_isPracticeMode = false;
            this->resetLevel();
            g_mac.lastTick = -1; g_cnt.dead = false; g_cnt.completed = false;
            int reached = cntRunUntil(lastEv + 240);
            g_cnt.calib += fmt::format("offset {}: {}; ", off, g_cnt.completed ? std::string("level complete") : (g_cnt.dead ? fmt::format("dies at tick {}", cntTick()) : fmt::format("alive at tick {}", cntTick())));
            if (reached > bestTick) { bestTick = reached; best = off; }
        }
        g_cnt.offset = best;
        g_cnt.calibOk = bestTick >= INT_MAX - 1;
        g_mac.inputs = shifted(best);
        cntBuildEv();
        m_isPracticeMode = false;
        this->resetLevel();
        g_mac.lastTick = -1; g_cnt.dead = false; g_cnt.completed = false;
        log::info("틱 맞춤 찾기: {} → {} 선택", g_cnt.calib, best);
    }
    // ───────── 영상 녹화 ─────────
    void recStart() {
        auto dir = Mod::get()->getConfigDir();
        std::string ffmpeg;
        { std::ifstream f(dir / "ffmpeg_path.txt"); std::getline(f, ffmpeg); }
        while (!ffmpeg.empty() && (ffmpeg.back() == '\r' || ffmpeg.back() == '\n' || ffmpeg.back() == ' ')) ffmpeg.pop_back();
        g_rec.want = false;
        auto fail = [&](std::string const& why) { g_rec.info = "record failed: " + why + "\n"; std::ofstream f(dir / "record_info.txt", std::ios::binary | std::ios::trunc); f << g_rec.info; g_rec.done = true; };
        if (ffmpeg.empty()) { fail("ffmpeg_path.txt missing"); return; }
        // 그릴 버퍼
        GLint oldFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
        glGenTextures(1, &g_rec.tex);
        glBindTexture(GL_TEXTURE_2D, g_rec.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_rec.w, g_rec.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &g_rec.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_rec.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_rec.tex, 0);
        bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
        ccGLBindTexture2D(0);
        if (!complete) { recClose(); fail("framebuffer incomplete"); return; }
        g_rec.buf.assign((size_t)g_rec.w * g_rec.h * 4, 0);
        auto out = (dir / "showcase.mp4").string();
        if (!recOpen(ffmpeg, out)) { recClose(); fail("cannot start ffmpeg: " + ffmpeg); return; }
        // 처음부터 다시 시작해서 찍음
        g_cnt.useTrial = false;
        m_isPracticeMode = false;
        this->resetLevel();
        g_mac.lastTick = -1;
        g_rec.endTick = (g_mac.inputs.empty() ? 0 : g_mac.inputs.rbegin()->first) + 360;
        g_rec.frames = 0; g_rec.active = true; g_rec.t0 = std::chrono::steady_clock::now();
        fwcActionsAuto(false);
        float songOffset = m_levelSettings ? m_levelSettings->m_songOffset : 0.f;
        g_rec.info.clear();
        g_rec.ffmpeg = ffmpeg;
        g_rec.songStart = m_gameState.m_levelTime + songOffset;
        g_rec.song.clear();
        if (m_level) {                       // 지금 레벨 노래 파일 (NONG 를 쓰면 바뀐 파일)
            std::string sp = m_level->getAudioFileName();
            if (!sp.empty()) {
                std::string full = CCFileUtils::sharedFileUtils()->fullPathForFilename(sp.c_str(), false);
                std::error_code ec2;
                if (std::filesystem::exists(full, ec2)) g_rec.song = full; else if (std::filesystem::exists(sp, ec2)) g_rec.song = sp;
            }
        }
        recIntro();                          // 소개 화면 3초 (설정에서 끌 수 있음). 노래를 입힐 때 이만큼 늦춰야 함 → record_info.txt 에 적음
        g_rec.info += fmt::format("output: {}\nsize: {}x{} fps {}\nsong id: {}\nsong offset: {}\nstart tick: {}\nstart leveltime: {:.6f}\n", out, g_rec.w, g_rec.h, g_rec.fps,
                                 m_level ? (int)m_level->m_songID : 0, songOffset, cntTick(), m_gameState.m_levelTime);
        log::info("영상 녹화 시작");
    }
    void recCapture(CCNode* node) {         // node 를 녹화 버퍼에 그려서 g_rec.buf 로 읽어 옴
        GLint oldFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, g_rec.fbo);
        glViewport(0, 0, g_rec.w, g_rec.h);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        // 영상에는 게임 화면 + FWC 표시만: 버튼층(일시정지 버튼, 다른 모드의 글자·스타트포스 바꾸기 등)과 FWC 상태 글자는 숨김
        bool uiVis = m_uiLayer && m_uiLayer->isVisible(), lbVis = g_cnt.label && g_cnt.label->isVisible();
        if (m_uiLayer) m_uiLayer->setVisible(false);
        if (g_cnt.label) g_cnt.label->setVisible(false);
        node->visit();
        if (m_uiLayer) m_uiLayer->setVisible(uiVis);
        if (g_cnt.label) g_cnt.label->setVisible(lbVis);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, g_rec.w, g_rec.h, GL_RGBA, GL_UNSIGNED_BYTE, g_rec.buf.data());
        glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
        CCDirector::get()->setViewport();
    }
    bool recWrite() {                       // g_rec.buf 한 장을 ffmpeg 로 보냄
        DWORD wr = 0; size_t off = 0, n = g_rec.buf.size();
        while (off < n) { if (!WriteFile(g_rec.hWrite, g_rec.buf.data() + off, (DWORD)std::min<size_t>(n - off, 1 << 22), &wr, nullptr) || wr == 0) { g_rec.active = false; g_rec.info += "write to ffmpeg failed\n"; return false; } off += wr; }
        ++g_rec.frames;
        return true;
    }
    void recFrame() {                       // 4틱 진행 후 한 프레임 캡처
        for (int k = 0; k < 240 / g_rec.fps; ++k) {
            g_cnt.internal = true;
            static_cast<CCObject*>(CCDirector::get()->getActionManager())->update(1.f / 240.f);
            GJBaseGameLayer::update(1.f / 240.f);
            g_cnt.internal = false;
        }
        recCapture(this);
        recWrite();
    }
    // ── 영상 앞 소개 화면 (3초) ──
    //   참고 영상의 소개 화면(1920x1080)을 픽셀로 재서 같은 비율로 배치. 글자 크기는 "기준 줄의 폭"으로 맞춤:
    //     범례 묶음: "9-12 Frames (~20Hz FP)" 폭 = 화면 폭 41.4% (아래 설명 2줄도 같은 크기)
    //     제목 묶음: "Frame Window" 폭 = 36.0% (레벨 이름·부제도 같은 크기, 왼쪽 칸 47%를 넘으면 그 줄만 줄임)
    //     머리줄: "Vanilla 240 FPS" 폭 = 29.4%
    //   범례 숫자는 계산: 1프레임 = 1000/240 ms, Hz = 240 / 그 칸의 가장 넓은 윈도우
    CCNode* makeIntro() {
        auto ws = CCDirector::get()->getWinSize();
        auto root = CCLayerColor::create({0, 0, 0, 255}, ws.width, ws.height);
        auto widthAt1 = [](const char* txt) {
            auto l = CCLabelBMFont::create(txt, "bigFont.fnt");
            return l ? std::max(1.f, l->getContentSize().width) : 1.f;
        };
        const float sLeg = 0.414f * ws.width / widthAt1("9-12 Frames (~20Hz FP)");
        const float sTit = 0.360f * ws.width / widthAt1("Frame Window");
        const float sHdr = 0.294f * ws.width / widthAt1("Vanilla 240 FPS");
        // xf, yf: 화면 비율 (yf 는 위에서부터), ax: 0 왼쪽 맞춤 / 1 오른쪽 맞춤
        auto add = [&](std::string const& txt, float xf, float yf, float ax, float scale, ccColor3B col, float maxWf) {
            auto l = CCLabelBMFont::create(txt.c_str(), "bigFont.fnt");
            if (!l) return;
            float w = l->getContentSize().width * scale;
            if (maxWf > 0 && w > maxWf * ws.width) scale *= maxWf * ws.width / w;
            l->setAnchorPoint({ax, 0.5f}); l->setScale(scale); l->setColor(col);
            l->setPosition({xf * ws.width, (1.f - yf) * ws.height});
            root->addChild(l);
        };
        const ccColor3B white = {255, 255, 255}, gray = {140, 140, 140};
        std::string name = g_cnt.levelName;
        for (auto& ch : name) ch = (char)std::toupper((unsigned char)ch);
        std::string sub = Mod::get()->getSettingValue<std::string>("intro-subtitle");
        const float L = 0.043f, LW = 0.47f;
        add(name, L, 0.255f, 0.f, sTit, white, LW);
        if (!sub.empty()) add(sub, L, 0.353f, 0.f, sTit, white, LW);
        add("Frame Window", L, 0.549f, 0.f, sTit, white, LW);
        add(fmt::format("Counter ({}F)", MAX_SHOWN_WINDOW), L, 0.645f, 0.f, sTit, white, LW);
        add("(Bottom Left Number)", 0.010f, 0.889f, 0.f, sLeg, gray, 0.52f);
        add("Raw Precision (24h) in sigma/s", 0.010f, 0.955f, 0.f, sLeg, white, 0.52f);
        const float R = 0.958f;
        add(g_cnt.cbf ? "CBF 8000 FPS" : "Vanilla 240 FPS", R, 0.040f, 1.f, sHdr, BIN_COLOR[0], 0.f);
        // 범례: 9-12 부터 1 까지 7칸 (17+, 13+ 는 넓은 칸이라 소개 화면에서는 생략), 칸마다 2줄
        const int lo[7] = {9, 7, 5, 4, 3, 2, 1}, hi[7] = {12, 8, 6, 4, 3, 2, 1}, bin[7] = {2, 3, 4, 5, 6, 7, 8};
        auto ms = [](int f) { return f * 1000.0 / 240.0; };
        for (int k = 0; k < 7; ++k) {
            int a = lo[k], b = hi[k];
            std::string l1 = (a != b ? fmt::format("{}-{} Frames", a, b) : fmt::format("{} Frame{}", a, a > 1 ? "s" : "")) + fmt::format(" (~{}Hz FP)", (int)std::lround(240.0 / b));
            std::string l2 = a != b ? fmt::format("({:.1f}ms, {:.1f}ms)", ms(a), ms(b)) : fmt::format("({:.1f}ms)", ms(a));
            add(l1, R, 0.104f + 0.1333f * k, 1.f, sLeg, BIN_COLOR[bin[k]], 0.f);
            add(l2, R, 0.104f + 0.1333f * k + 0.0667f, 1.f, sLeg, BIN_COLOR[bin[k]], 0.f);
        }
        return root;
    }
    // 노래 입히기: 화면만 있는 영상에 레벨 노래를 (녹화 시작 때 노래 위치부터, 소개 화면 길이만큼 늦춰) 붙임
    void recAddSong() {
        auto dir = Mod::get()->getConfigDir();
        if (g_rec.song.empty()) { g_rec.info += "song: not found (video has no sound)\n"; return; }
        std::error_code ec;
        auto vid = dir / "showcase.mp4", tmp = dir / "showcase_video_only.mp4";
        std::filesystem::rename(vid, tmp, ec);
        if (ec) { g_rec.info += "song: cannot rename video\n"; return; }
        int delayMs = (int)std::lround(g_rec.introSec * 1000.0);
        std::string cmd = fmt::format("\"{}\" -y -i \"{}\" -ss {:.3f} -i \"{}\" -map 0:v -map 1:a -c:v copy -c:a aac -b:a 192k -af adelay={}:all=1 -shortest \"{}\"",
                                      g_rec.ffmpeg, tmp.string(), std::max(0.0, g_rec.songStart), g_rec.song, delayMs, vid.string());
        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<char> cl(cmd.begin(), cmd.end()); cl.push_back(0);
        bool ok = CreateProcessA(nullptr, cl.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        DWORD code = 1;
        if (ok) { WaitForSingleObject(pi.hProcess, 120000); GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
        if (ok && code == 0 && std::filesystem::exists(vid, ec)) {
            std::filesystem::remove(tmp, ec);
            g_rec.info += fmt::format("song: {} from {:.3f}s, delayed {} ms\n", g_rec.song, g_rec.songStart, delayMs);
        } else {
            std::filesystem::remove(vid, ec); std::filesystem::rename(tmp, vid, ec);          // 실패하면 소리 없는 영상 그대로
            g_rec.info += fmt::format("song: adding failed (ffmpeg exit {}), video has no sound\n", (int)code);
        }
    }
    // 녹화마다 따로 남기기: 설정 폴더 recordings/<레벨 이름>_<날짜_시각>.mp4 (showcase.mp4 는 항상 마지막 것)
    void recKeepCopy() {
        auto dir = Mod::get()->getConfigDir();
        std::error_code ec;
        std::filesystem::create_directories(dir / "recordings", ec);
        std::string safe = g_cnt.levelName;
        for (auto& ch : safe) if (std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos) ch = '_';
        auto now = std::time(nullptr); std::tm tm{}; localtime_s(&tm, &now);
        char ts[32]; std::strftime(ts, sizeof ts, "%Y%m%d_%H%M%S", &tm);
        auto dst = dir / "recordings" / (safe + "_" + ts + ".mp4");
        std::filesystem::copy_file(dir / "showcase.mp4", dst, std::filesystem::copy_options::overwrite_existing, ec);
        g_rec.info += ec ? std::string("copy: failed\n") : fmt::format("copy: {}\n", dst.string());
    }
    void recIntro() {
        if (!Mod::get()->getSettingValue<bool>("record-intro")) return;
        Ref<CCNode> intro = makeIntro();
        recCapture(intro);
        const int n = g_rec.fps * 3;                         // 3초 (같은 그림을 그대로 반복)
        for (int i = 0; i < n && g_rec.active; ++i) recWrite();
        g_rec.introSec = n / (double)g_rec.fps;
        g_rec.info += fmt::format("intro seconds: {:.3f} (frames {})\n", n / (double)g_rec.fps, n);
    }
    void recWork() {                        // 실제 한 프레임마다 영상 프레임 몇 개씩
        for (int i = 0; i < 1 && g_rec.active; ++i) {     // 실제 한 프레임 = 영상 한 프레임 (파티클·트레일이 영상 시간에 맞게 움직이도록)
            recFrame();
            bool dead = m_player1 && m_player1->m_isDead;
            if (dead || m_hasCompletedLevel || cntTick() >= g_rec.endTick) {
                g_rec.info += fmt::format("frames: {}\nend tick: {}\ndead: {}\ncompleted: {}\n", g_rec.frames, cntTick(), (int)dead, (int)m_hasCompletedLevel);
                if (!dead) for (int j = 0; j < 30 && g_rec.active; ++j) recFrame();      // 끝에 0.5초 여유
                g_rec.active = false;
            }
        }
        if (!g_rec.active) {
            fwcActionsAuto(true);
            recClose();
            recAddSong();
            recKeepCopy();
            g_rec.info += fmt::format("seconds taken: {:.1f}\nfinal frames: {}\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - g_rec.t0).count(), g_rec.frames);
            std::ofstream f(Mod::get()->getConfigDir() / "record_info.txt", std::ios::binary | std::ios::trunc); f << g_rec.info;
            g_rec.done = true;
            log::info("영상 녹화 끝: {} 프레임", g_rec.frames);
        }
    }

    void cntFresh() {                      // 지금 시작 지점에서 새로 시작 (연습 모드 아님)
        m_isPracticeMode = false;
        this->resetLevel();
        g_mac.lastTick = -1; g_cnt.dead = false; g_cnt.completed = false;
    }
    int cntRunUntil(int limit) {           // 죽으면 그 틱, 클리어면 INT_MAX, limit 까지 살면 INT_MAX-1
        for (int g = 0, cap = (limit - cntTick()) + 400; g < cap && cntTick() < limit; ++g) {
            cntStep();
            if (g_cnt.completed) return INT_MAX;
            if (g_cnt.dead) return cntTick();
            if (g_cnt.goalX > 0 && m_player1->getPositionX() >= g_cnt.goalX) return INT_MAX - 1;
        }
        return INT_MAX - 1;
    }
    // 자동 고침: 연습 모드에서 이어 찍은 매크로는 되살아난 지점에서 미세하게 어긋나 그대로는 재생되지 않는다.
    //   죽는 지점 직전의 입력 몇 개를 1~8틱씩 옮겨 보고, 가장 멀리 가는 것을 골라 적용하기를 끝까지 반복한다.
    bool cntRepair() {
        if (g_mac.inputs.empty()) return false;
        const int goal = g_mac.inputs.rbegin()->first + 240;
        static const int shifts[] = {-1, 1, -2, 2, -3, 3, -4, 4, -5, 5, -6, 6, -8, 8, -10, 10, -12, 12};
        for (int iter = 0; iter < 120; ++iter) {
            cntDropCp();
            g_cnt.useTrial = false;
            cntFresh();
            int D = cntRunUntil(goal);
            if (D >= INT_MAX - 1) return true;                       // 끝까지 감
            // 상태를 바꾸는 입력만 순서대로
            std::vector<std::pair<int, bool>> ev; bool st = false;
            for (auto const& kv : g_mac.inputs) for (bool dn : kv.second) if (dn != st) { ev.push_back({kv.first, dn}); st = dn; }
            std::vector<int> cand;
            for (int i = (int)ev.size() - 1; i >= 0 && (int)cand.size() < 6; --i) if (ev[i].first < D && ev[i].first > D - 720 && ev[i].first >= 12) cand.push_back(i);
            if (cand.empty()) { g_cnt.repair += fmt::format("stuck at tick {} (no input to adjust); ", D); return false; }
            int earliest = ev[cand.back()].first;
            int R = -1;
            for (int t = earliest - 12; t >= 6; --t) { bool ok = true; for (int k = -2; k <= 2 && ok; ++k) ok = !g_mac.heldAt(t + k); if (ok) { R = t; break; } }
            cntFresh();
            if (R > 0) { if (!cntStepTo(R)) { g_cnt.repair += fmt::format("stuck at tick {} (cannot reach {}); ", D, R); return false; } cntMakeCp(); }
            int bestReach = D, bestI = -1, bestD = 0;
            for (int ci : cand) for (int d : shifts) {
                int T = ev[ci].first, nT = T + d;
                int prevT = ci > 0 ? ev[ci - 1].first : 0, nextT = ci + 1 < (int)ev.size() ? ev[ci + 1].first : INT_MAX;
                if (nT <= prevT || nT >= nextT || nT <= (R > 0 ? R + 1 : 6)) continue;
                g_cnt.trial = g_mac.inputs;
                { auto& v = g_cnt.trial[T]; In mv(ev[ci].second); for (auto it = v.begin(); it != v.end(); ++it) if (it->down == ev[ci].second) { mv = *it; v.erase(it); break; } if (v.empty()) g_cnt.trial.erase(T); inputsAdd(g_cnt.trial, nT + mv.frac, mv.down); }
                if (R > 0) cntRestore(); else cntFresh();
                g_cnt.useTrial = true; ++g_cnt.trials;
                int reach = cntRunUntil(std::min(goal, D + 720));
                g_cnt.useTrial = false;
                if (reach > bestReach || (reach == bestReach && bestI >= 0 && std::abs(d) < std::abs(bestD))) { bestReach = reach; bestI = ci; bestD = d; }
            }
            if (bestI < 0 || bestReach <= D + 20) { g_cnt.repair += fmt::format("stuck at tick {} (no shift helps); ", D); return false; }
            {   // 적용
                int T = ev[bestI].first, nT = T + bestD; bool dn = ev[bestI].second;
                auto& v = g_mac.inputs[T]; In mv(dn); for (auto it = v.begin(); it != v.end(); ++it) if (it->down == dn) { mv = *it; v.erase(it); break; } if (v.empty()) g_mac.inputs.erase(T);
                inputsAdd(g_mac.inputs, nT + mv.frac, dn);
                g_cnt.repair += fmt::format("{} {} {}->{} (died {}); ", dn ? "press" : "release", T, T, nT, D);
                ++g_cnt.repairs;
            }
        }
        return false;
    }
    void cntRebuildEv() { cntBuildEv(); }
    void cntSaveMacro(const char* backupName) {     // 지금 입력을 macro.txt 로 저장 (기존 파일은 backupName 으로 한 번만 보관)
        auto dir = Mod::get()->getConfigDir(); std::error_code ec;
        if (!std::filesystem::exists(dir / backupName, ec)) std::filesystem::copy_file(dir / "macro.txt", dir / backupName, ec);
        std::ofstream f(dir / "macro.txt", std::ios::binary | std::ios::trunc);
        for (auto const& kv : g_mac.inputs) for (In const& x : kv.second) {
            f << kv.first << " " << (x.down ? 1 : 0);
            if (x.frac > 0.f) f << " " << fmt::format("{:.6f}", x.frac);
            f << "\n";
        }
    }
    void cntRepairPass() {                           // 그대로는 끝까지 못 가면 자동 고침
        int before = g_cnt.repairs;
        bool ok = cntRepair();
        cntDropCp();
        g_cnt.repair += ok ? "result: reaches the end; " : "result: still dies; ";
        if (g_cnt.repairs > before) cntSaveMacro("macro_before_fix.txt");
        cntRebuildEv();
        g_cnt.useTrial = false;
        cntFresh();
    }
    // 매크로 합치기: 설정 폴더에 macro_part2.txt(스타트포스에서 찍은 뒷부분)와 merge_x.txt(그 스타트포스의 x)가 있으면,
    //   앞부분(macro.txt)이 그 x 에 닿는 틱을 찾아 거기서부터 뒷부분을 이어 붙인다. 이음매가 안 맞는 곳은 자동 고침이 맞춘다.
    void cntMerge() {
        auto dir = Mod::get()->getConfigDir();
        Inputs partB; double X = 0; int nB = 0;
        nB = macroRead(dir / "macro_part2.txt", partB);                     // 틱 안 위치(세 번째 칸)도 그대로
        { std::ifstream f(dir / "merge_x.txt"); f >> X; }
        if (nB == 0 || X <= 0) return;
        g_cnt.goalX = X;
        cntCalibrate();                                  // 앞부분의 틱 맞춤 (x 에 닿으면 통과)
        if (!g_cnt.calibOk) cntRepairPass();             // 앞부분이 x 전에 죽으면 고침
        g_cnt.useTrial = false;
        cntFresh();
        int T0 = -1;
        for (int g = 0; g < 200000; ++g) {
            cntStep();
            if (g_cnt.dead || g_cnt.completed) break;
            if (m_player1->getPositionX() >= X) { T0 = cntTick(); break; }
        }
        g_cnt.goalX = 0;
        if (T0 < 0) { g_cnt.merge = fmt::format("merge failed: part 1 does not reach x {:.1f} (stops at tick {}); ", X, cntTick()); cntFresh(); return; }
        // 이음매 틱을 T0-2 ~ T0+2 로 바꿔 가며 붙여 보고, 가장 멀리 가는 것을 고름 (닿는 틱이 1틱쯤 어긋날 수 있어서)
        const auto partA = g_mac.inputs;
        auto build = [&](int J) {
            Inputs m; bool held = false;
            for (auto const& kv : partA) { if (kv.first >= J) break; m[kv.first] = kv.second; for (bool dn : kv.second) held = dn; }
            if (held) m[J].push_back(false);         // 이음매에서 누르고 있었으면 뗌
            for (auto const& kv : partB) for (In const& x : kv.second) inputsAdd(m, J + kv.first + (double)x.frac, x.down);   // 위치 유지, 같은 틱 안에서는 위치 순
            return m;
        };
        static const int joff[5] = {0, 1, -1, 2, -2};
        int bestJ = T0, bestReach = -1;
        for (int jo : joff) {
            g_mac.inputs = build(T0 + jo);
            g_cnt.useTrial = false;
            cntFresh();
            int r = cntRunUntil(g_mac.inputs.rbegin()->first + 240);
            g_cnt.merge += fmt::format("join {}{}: {}; ", jo >= 0 ? "+" : "", jo, r >= INT_MAX - 1 ? std::string("reaches the end") : fmt::format("dies at tick {}", r));
            if (r > bestReach) { bestReach = r; bestJ = T0 + jo; }
        }
        Inputs merged = build(bestJ);
        g_cnt.merge += fmt::format("merged at tick {} (part 1 reaches x {:.1f} at tick {}): part2 {} inputs shifted by {}; ", bestJ, X, T0, nB, bestJ);
        g_mac.inputs = merged;
        cntSaveMacro("macro_part1.txt");
        { std::ofstream f(dir / "macro_part2.txt", std::ios::binary | std::ios::trunc); f << ""; }   // 다시 합치지 않도록 비움 (원본은 앱 폴더에 있음)
        cntRebuildEv();
        g_cnt.calibOk = false;
        g_cnt.useTrial = false;
        cntFresh();
        int r = cntRunUntil(g_mac.inputs.rbegin()->first + 240);
        g_cnt.merge += r >= INT_MAX - 1 ? "merged macro reaches the end as is; " : fmt::format("merged macro dies at tick {} -> repairing; ", r);
        if (r < INT_MAX - 1) cntRepairPass();
        g_cnt.calibOk = true;
        cntFresh();
    }
    // 이번 세기 조건의 지문: 레벨 데이터, 매크로 입력(틱 안 위치 포함), 시작 지점, CBF 세기, 판정 방식 버전
    std::string cntSignature() {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](void const* p, size_t n) { auto b = static_cast<unsigned char const*>(p); for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } };
        std::string ls = m_level ? std::string(m_level->m_levelString) : std::string();
        mix(ls.data(), ls.size());
        for (auto const& kv : g_mac.inputs) for (In const& x : kv.second) { int t = kv.first; float f = x.frac; mix(&t, sizeof t); mix(&x.down, sizeof x.down); mix(&f, sizeof f); }
        float spx = m_startPosObject ? m_startPosObject->getPositionX() : -1.f; mix(&spx, sizeof spx);
        bool cbf = g_cnt.cbf; mix(&cbf, sizeof cbf);
        const int RULE = 7;  mix(&RULE, sizeof RULE);           // 판정 방식이 바뀌면 이 숫자를 올림 → 다시 셈
        return fmt::format("{:016x}", h);
    }
    std::filesystem::path cntSigPath() {
        std::string safe = g_cnt.levelName;
        for (auto& ch : safe) if (std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos) ch = '_';
        return levelsDir() / (safe + ".sig");
    }
    void cntStart() {
        g_cnt.sig = cntSignature();
        if (!g_cnt.forced) {                 // 같은 조건으로 이미 셌으면 다시 세지 않고 결과만 보여 줌
            std::string old; { std::ifstream f(cntSigPath()); std::getline(f, old); }
            if (!old.empty() && old == g_cnt.sig && loadFor(g_cnt.levelName)) {
                g_cnt.done = true;
                log::info("윈도우 세기: 같은 조건으로 이미 셈 ({}) → 건너뜀", g_cnt.sig);
                cntSetLabel("FWC: already counted");
                return;
            }
        }
        g_cnt.forced = false;
        g_cnt.busy = true; g_cnt.t0 = std::chrono::steady_clock::now();
        fwcActionsAuto(false);
        cntMerge();
        if (g_cnt.merge.empty()) {
            cntCalibrate();
            if (!g_cnt.calibOk) cntRepairPass();
        }
        {   // 전체 경로 기록: 지금 입력 그대로 화면 없이 처음부터 끝까지
            g_cnt.useTrial = false;
            cntFresh();
            int goal = g_mac.inputs.empty() ? 0 : g_mac.inputs.rbegin()->first + 240;
            g_cnt.fullRef.assign(goal + 2, Counter::P3{0, 0, 0, 0});
            for (int g = 0, cap = goal + 400; g < cap && cntTick() < goal && !g_cnt.dead && !g_cnt.completed; ++g) {
                cntStep();
                int t = cntTick();
                if (t >= 0 && t < (int)g_cnt.fullRef.size()) g_cnt.fullRef[t] = {m_player1->getPositionX(), m_player1->getPositionY(), m_player1->m_yVelocity, m_player1->getRotation()};
            }
            cntFresh();
        }
        log::info("윈도우 세기 시작: 누름 {}개", g_cnt.pressIdx.size());
        cntSetLabel(fmt::format("FWC counting 0/{}", g_cnt.ev.size()));
    }
    // 옮긴 입력으로 한 번 돌려 보기. ref: 저장 지점 다음 틱부터의 원래 경로 상태
    //   d: 옮기는 양(틱). 정수면 지금까지와 같고, 소수면 틱 사이로 옮김 (CBF 세기, 1/8000초 단위로 맞춤)
    bool cntTrial(std::vector<Sig> const& ref, bool refCompleted, int pi, bool pair, double d) {
        auto const& ev = g_cnt.ev;
        auto tOf = [&](int i) { return ev[i].first + (double)g_cnt.evFrac[i]; };     // 입력 시각 (틱 + 위치)
        int T = ev[pi].first;
        bool hasRel = pi + 1 < (int)ev.size() && ev[pi + 1].second != ev[pi].second;   // 짝: 바로 다음의 반대 입력 (누름→뗌, 뗌→다음 누름)
        if (pair && !hasRel) return false;
        double t0 = tOf(pi), nt = t0 + d;
        double relt = hasRel ? tOf(pi + 1) : -1.0, nrel = pair ? relt + d : relt;
        double prevt = pi > 0 ? tOf(pi - 1) : 0.0;
        int afterIdx = pair ? pi + 2 : pi + 1;
        double nextt = afterIdx < (int)ev.size() ? tOf(afterIdx) : 1e18;
        if (nt <= prevt || nt < g_cnt.cpTick + 1.0 + 1e-9) return false;   // 앞 입력을 넘어가면 안 됨 (되돌린 지점 바로 다음 틱부터)
        if (pair ? nrel >= nextt : nt >= nextt) return false;             // 뒤 입력을 넘어가면 안 됨
        g_cnt.trial = g_mac.inputs;                                       // 입력 복사 후 이 누름(과 뗌)만 옮김
        inputsRemove(g_cnt.trial, T, ev[pi].second, g_cnt.evFrac[pi]);
        inputsAdd(g_cnt.trial, nt, ev[pi].second);
        if (pair) { inputsRemove(g_cnt.trial, ev[pi + 1].first, ev[pi + 1].second, g_cnt.evFrac[pi + 1]); inputsAdd(g_cnt.trial, nrel, ev[pi + 1].second); }
        int nT = (int)std::floor(nt), relT = hasRel ? ev[pi + 1].first : -1, nRel = pair ? (int)std::floor(nrel) : relT;
        cntRestore();
        g_cnt.useTrial = true; ++g_cnt.trials; g_cnt.lastDie = -1;
        int lastChanged = std::max({T, nT, pair ? std::max(relT, nRel) : T});
        int refEnd = g_cnt.cpTick + (int)ref.size();
        // 중간 판정 시점: 바뀐 입력 뒤 "다다음 입력 + 30틱", 단 120~360틱 사이
        int cnt2 = 0, t2 = lastChanged + Counter::REJOIN_MIN;
        for (auto const& kv : g_cnt.trial) { if (kv.first <= lastChanged) continue; if (++cnt2 == 2) { t2 = kv.first + 30; break; } }
        t2 = std::min(std::max(t2, lastChanged + Counter::REJOIN_MIN), lastChanged + Counter::REJOIN_MAX);
        int end = std::min(lastChanged + Counter::HORIZON, refEnd);
        if (g_cnt.refDied) end = std::min(end, refEnd - 1);              // 매크로 자체가 여기서 죽음: 원래처럼 죽기 직전까지 살면 통과
        bool ok = true;
        while (cntTick() < end) {
            int before = cntTick();
            cntStep();
            if (g_cnt.dead) { ok = false; g_cnt.lastDie = cntTick(); break; }
            if (g_cnt.completed) break;
            int t = cntTick();
            if (t == before) continue;                                    // 되돌린 직후 헛도는 틱
            int k = t - g_cnt.cpTick - 1;
            if (k < 0 || k >= (int)ref.size()) continue;
            if (t > lastChanged + 1 && cntSig() == ref[k]) break;         // 원래 경로와 완전히 같아짐
            if (t == t2) {                                                // 살아서 원래 경로 근처면 통과
                Sig a = cntSig(); Sig const& r = ref[k];
                if (a.mode == r.mode && a.up == r.up && a.dual == r.dual && std::fabs(a.y - r.y) <= Counter::NEAR_Y && (!a.dual || std::fabs(a.y2 - r.y2) <= Counter::NEAR_Y)) break;
            }
        }
        // 다음 클릭으로 되살리기: 다음 입력까지는 살았는데 그 뒤에 죽었으면, 다음 입력 하나만 ±1~REC_MAX(12)틱 다시 맞춰 봄.
        //   그다음 입력까지 살거나 원래 경로로 돌아오면 통과 (그 입력 탓이 아닌, 뒤 입력이 고정돼서 생긴 죽음을 빼기)
        if (!ok && g_cnt.lastDie >= 0 && afterIdx < (int)ev.size() && g_cnt.lastDie > ev[afterIdx].first) {
            int dieKeep = g_cnt.lastDie;
            const auto base = g_cnt.trial;
            double na = tOf(afterIdx);
            double lo = pair ? nrel : nt;                                  // 다음 입력은 옮긴 입력보다 뒤
            double hi = afterIdx + 1 < (int)ev.size() ? tOf(afterIdx + 1) : 1e18;
            int goal2 = afterIdx + 1 < (int)ev.size() ? ev[afterIdx + 1].first + 1 : end;
            goal2 = std::min(goal2, end);
            for (int m = 1; m <= 2 * Counter::REC_MAX; ++m) {           // -1, +1, -2, +2, ... 가까운 것부터
                int r = (m % 2 ? -1 : 1) * ((m + 1) / 2);
                double nn = na + r;
                if (nn <= lo || nn >= hi || nn < g_cnt.cpTick + 1.0) continue;
                g_cnt.trial = base;
                inputsRemove(g_cnt.trial, ev[afterIdx].first, ev[afterIdx].second, g_cnt.evFrac[afterIdx]);
                inputsAdd(g_cnt.trial, nn, ev[afterIdx].second);
                cntRestore();
                g_cnt.useTrial = true; ++g_cnt.trials; ++g_cnt.recTrials;
                int lc2 = std::max(lastChanged, (int)std::floor(nn));
                bool ok2 = true;
                while (cntTick() < goal2) {
                    int before = cntTick();
                    cntStep();
                    if (g_cnt.dead) { ok2 = false; break; }
                    if (g_cnt.completed) break;
                    int t = cntTick();
                    if (t == before) continue;
                    int k = t - g_cnt.cpTick - 1;
                    if (k >= 0 && k < (int)ref.size() && t > lc2 + 1 && cntSig() == ref[k]) break;
                }
                if (ok2) { ok = true; ++g_cnt.recovered; break; }
            }
            g_cnt.trial = base;
            if (!ok) g_cnt.lastDie = dieKeep; else g_cnt.lastDie = -1;
        }
        g_cnt.useTrial = false;
        return ok;
    }
    void cntFinish() {
        cntRestore();
        cntDropCp();
        m_isPracticeMode = false;
        g_cnt.busy = false; g_cnt.done = true; g_cnt.useTrial = false;
        fwcActionsAuto(true);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_cnt.t0).count();
        // 결과 저장: 앱·모드가 읽는 형식 (levels/<레벨 이름>.json) + 사람이 읽는 표 (windows_ingame.txt)
        std::string safe = g_cnt.levelName;
        for (auto& ch : safe) if (std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos) ch = '_';
        std::error_code ec; std::filesystem::create_directories(levelsDir(), ec);
        // σ/s (NaNDL 식, 앱 analyze.precision 과 같음): 입력 k 개까지의 윈도우로 24시간 안에 깰 수 있는 정밀도
        std::vector<double> run;
        {
            std::vector<double> wsec, tsec;
            for (auto const& r : g_cnt.res) {
                double wf = g_cnt.cbf ? r.earlyF + r.lateF + SUB_TICK : (double)(r.early + r.late + 1);
                wsec.push_back(wf / 240.0); tsec.push_back(r.tick / 240.0);
                size_t n = wsec.size();
                auto et = [&](double L) -> double {
                    double prodP = 1.0, sumTerm = 0.0, r0 = 1.0;
                    for (size_t i = 0; i < n; ++i) {
                        double pp = std::clamp(std::erf(0.5 * wsec[i] * L / std::sqrt(2.0)), 1e-300, 1.0);
                        sumTerm += tsec[i] * r0 * (1.0 - pp);
                        r0 *= pp; prodP *= pp;
                    }
                    if (prodP <= 0) return INFINITY;
                    double v = (tsec[n - 1] * prodP + sumTerm + 1.0) / prodP;
                    return std::isfinite(v) ? v : INFINITY;
                };
                double lo = 1e-3, hi = 1e5, target = 86400.0;
                if (et(lo) <= target) { run.push_back(lo); continue; }
                for (int it = 0; it < 200; ++it) { double mid = std::sqrt(lo * hi); if (et(mid) > target) lo = mid; else hi = mid; if (hi / lo < 1 + 1e-10) break; }
                run.push_back(std::sqrt(lo * hi));
            }
        }
        std::string js = "{\n  \"source\": \"geode\", \"level_end\": " + fmt::format("{:.3f}", (double)m_levelLength) + ", \"cbf\": " + std::string(g_cnt.cbf ? "true" : "false") + ", \"n\": " + std::to_string(g_cnt.res.size()) + ",\n  \"precision_run\": [";
        for (size_t i = 0; i < run.size(); ++i) js += (i ? ", " : "") + fmt::format("{:.4f}", run[i]);
        js += "],\n  \"windows\": [\n";
        std::string tb = fmt::format("# 게임 안 윈도우 세기: 누름 {}개, {:.1f}초, 시도 {}번, 되돌리기 {}번({:.1f}초), 진행 {}틱({:.1f}초){}\n# tick early late window pair kind dieE dieL (dieE/dieL: 처음 실패한 시도가 입력 뒤 몇 틱에 죽었나, -1 = 죽음 아님/끝까지 됨)\n",
                                     g_cnt.res.size(), sec, g_cnt.trials, g_cnt.resets, g_cnt.msReset / 1000.0, g_cnt.ticksRun, g_cnt.msStep / 1000.0, g_cnt.msg.empty() ? "" : " (" + g_cnt.msg + ")");
        tb += "# calibration: " + g_cnt.calib + "chosen offset " + std::to_string(g_cnt.offset) + "\n";
        if (!g_cnt.merge.empty()) tb += "# merge: " + g_cnt.merge + "\n";
        tb += fmt::format("# restore check: {} ticks compared, position differs {}, rotation-only differs {}\n", g_cnt.chkTicks, g_cnt.chkPosDiff, g_cnt.chkRotDiff);
        tb += fmt::format("# fresh checkpoints (restore drift): {}\n", g_cnt.freshCps);
        tb += "# rule: shift this click; it passes if, for at least one of the previous click's possible positions, the player is alive at the next click\n";
        if (!g_cnt.unmeasured.empty()) { tb += fmt::format("# unmeasured ({}, restored state differs from a fresh run):", g_cnt.unmeasured.size()); for (int t : g_cnt.unmeasured) tb += " " + std::to_string(t); tb += "\n"; }
        if (!g_cnt.repair.empty()) tb += "# repair (" + std::to_string(g_cnt.repairs) + " changes): " + g_cnt.repair + "\n";
        for (size_t i = 0; i < g_cnt.res.size(); ++i) {
            auto const& r = g_cnt.res[i]; int w = r.early + r.late + 1;
            // CBF 세기: 되는 시각 구간의 길이 + 1/8000초 (정수 세기의 "칸 수"와 같은 뜻: 정수면 e+l+1)
            double wf = g_cnt.cbf ? r.earlyF + r.lateF + SUB_TICK : (double)w;
            std::string extra = g_cnt.cbf ? fmt::format(", \"window_f\": {:.3f}, \"window_ms\": {:.3f}", wf, wf * 1000.0 / 240.0) : std::string();
            js += fmt::format("    {{\"kind\": \"{}\", \"tick\": {}, \"early\": {}, \"late\": {}, \"window\": {}, \"pair\": {}{}}}{}\n", r.down ? "press" : "release", r.tick, r.early, r.late, g_cnt.cbf ? (int)std::floor(wf) : w, r.pair ? "true" : "false", extra, i + 1 < g_cnt.res.size() ? "," : "");
            tb += fmt::format("{} {} {} {} {} {} {} {}", r.tick, r.early, r.late, w, (int)r.pair, r.down ? "press" : "release", r.dieE, r.dieL);
            if (g_cnt.cbf) tb += fmt::format(" cbf {:.3f}f {:.3f}ms", wf, wf * 1000.0 / 240.0);
            tb += "\n";
        }
        js += "  ]\n}\n";
        { std::ofstream f(levelsDir() / (safe + ".json"), std::ios::binary | std::ios::trunc); f << js; }
        { std::ofstream f(Mod::get()->getConfigDir() / "windows_ingame.txt", std::ios::binary | std::ios::trunc); f << tb; }
        if (!g_cnt.failed && !g_cnt.sig.empty()) { std::ofstream f(cntSigPath(), std::ios::binary | std::ios::trunc); f << g_cnt.sig << "\n"; }   // 다 셌을 때만: 다음에 같은 조건이면 건너뜀
        log::info("윈도우 세기 끝: 누름 {}개, {:.1f}초, 시도 {}번", g_cnt.res.size(), sec, g_cnt.trials);
        cntSetLabel(g_cnt.failed ? "FWC counting stopped (error)" : fmt::format("FWC done: {} inputs, {:.0f}s", g_cnt.res.size(), sec));
        // 결과를 바로 화면에 쓰기
        if (Mod::get()->getSettingValue<bool>("enabled")) {
            g_data = loadFor(g_cnt.levelName);
            if (g_data && !m_fields->m_hud) {
                auto hud = FwcHud::create(); hud->setZOrder(1000); this->addChild(hud); m_fields->m_hud = hud;
            }
        }
        this->resetLevel();                 // 지금 시작 지점(스타트포스)에서 다시 시작 → 매크로 재생 + 숫자·고리 표시
        g_mac.lastTick = -1;
    }
    // 한 번 해 보기: 이전 클릭(pj)을 sp 틱, 이 클릭(pi)을 d 만큼 옮기고 나머지는 그대로 → 다음 클릭 시각까지 살면 통과
    bool cntTry(int pi, int pj, int sp, double d, int* modeOut) {
        auto const& ev = g_cnt.ev;
        auto tOf = [&](int i) { return ev[i].first + (double)g_cnt.evFrac[i]; };
        int n = (int)ev.size();
        double nt = tOf(pi) + d;
        double lowCur = g_cnt.cpTick + 1.0 - 1e-9;
        if (pj >= 0 && sp != 0) {
            double np = tOf(pj) + sp;
            double lo = pj > 0 ? tOf(pj - 1) : 0.0, hi = pj + 1 < n ? tOf(pj + 1) : 1e18;
            if (pj + 1 == pi) hi = nt;
            if (np <= lo || np >= hi || np < g_cnt.cpTick + 1.0) return false;
            if (pj + 1 == pi) lowCur = std::max(lowCur, np);
        }
        double prevt = pi > 0 ? tOf(pi - 1) : 0.0;
        if (!(pj + 1 == pi && sp != 0)) lowCur = std::max(lowCur, prevt);
        double nextt = pi + 1 < n ? tOf(pi + 1) : 1e18;
        if (nt <= lowCur || nt >= nextt) return false;
        g_cnt.trial = g_mac.inputs;
        if (pj >= 0 && sp != 0) { inputsRemove(g_cnt.trial, ev[pj].first, ev[pj].second, g_cnt.evFrac[pj]); inputsAdd(g_cnt.trial, tOf(pj) + sp, ev[pj].second); }
        if (d != 0.0) { inputsRemove(g_cnt.trial, ev[pi].first, ev[pi].second, g_cnt.evFrac[pi]); inputsAdd(g_cnt.trial, nt, ev[pi].second); }
        int endT = pi + 1 < n ? ev[pi + 1].first : (int)std::floor(nt) + 240;
        endT = std::max(endT, (int)std::floor(nt) + 1);
        cntRestore();
        g_cnt.useTrial = true; ++g_cnt.trials; g_cnt.lastDie = -1;
        bool ok = true;
        for (int g = 0, cap = endT - cntTick() + 64; g < cap && cntTick() < endT; ++g) {
            cntStep();
            if (g_cnt.dead) { ok = false; g_cnt.lastDie = cntTick(); break; }
            if (g_cnt.completed) break;
            if (modeOut && cntTick() <= ev[pi].first) *modeOut = cntSig().mode;
        }
        g_cnt.useTrial = false;
        return ok;
    }
    // 프레임마다 누름 하나씩 처리
    void cntWork() {
        if (g_cnt.cur >= g_cnt.ev.size()) { cntFinish(); return; }
        int pi = (int)g_cnt.cur;
        bool isPress = g_cnt.ev[pi].second;
        if (g_cnt.ev[pi].first < 12) { ++g_cnt.cur; return; }             // 시작 직후의 뗌 같은 것은 건너뜀
        if (pi > 0 && g_cnt.ev[pi - 1].second == isPress) { ++g_cnt.cur; return; }   // 이미 뗀 상태에서 또 뗌(또는 누른 상태에서 또 누름): 아무 효과 없는 입력
        int T = g_cnt.ev[pi].first;
        // 이전 클릭: 앞에서 이미 잰 가장 가까운 입력. 그 입력이 될 수 있었던 위치들(경우의 수)을 저장해 두고 씀
        int pj = -1;
        for (int j = pi - 1; j >= 0; --j) if (g_cnt.validShift.count(j)) { pj = j; break; }
        if (pj >= 0 && g_cnt.ev[pj].first < T - 240) pj = -1;        // 너무 먼 입력은 이전 클릭으로 안 씀
        // 저장 지점을 R 로 옮기기 (앞으로만)
        // fresh: 되돌리기 오차가 쌓였을 때 처음부터 다시 돌려 정확한 저장 지점을 만듦
        auto moveCp = [&](int R, bool fresh = false) -> bool {
            if (R < 0) R = 6;
            if (!fresh) {
                if (g_cnt.cp) cntRestore();
                if (g_cnt.cp && R <= g_cnt.cpTick) return true;
            }
            g_cnt.useTrial = false;
            bool ok = !fresh && cntTick() <= R && cntStepTo(R);
            if (!ok) {                                   // 되돌린 상태에서 가다 죽으면 처음부터 다시
                cntDropCp();
                cntFresh();
                if (!cntStepTo(R)) { g_cnt.failed = true; g_cnt.msg = fmt::format("press {}: macro dies before tick {}", T, R); cntFinish(); return false; }
                ++g_cnt.freshCps;
            }
            cntDropCp();
            cntMakeCp();
            return true;
        };
        if (!moveCp(cntRestoreTick(pj >= 0 ? std::min(T, g_cnt.ev[pj].first) : T))) return;
        // 이전 클릭의 경우의 수: 0, +1, -1, +2, -2 ... (가까운 것부터)
        std::vector<int> sList{0};
        if (pj >= 0) {
            auto [pe, pl] = g_cnt.validShift[pj];
            for (int k = 1; k <= std::max(pe, pl); ++k) { if (k <= pl) sList.push_back(k); if (k <= pe) sList.push_back(-k); }
        }
        // 기준: 아무것도 안 옮기고 다음 클릭까지 → 못 버티면 되돌리기 문제라 잴 수 없음
        int mode = 0;
        if (!cntTry(pi, pj, 0, 0.0, &mode)) {
            // 되돌린 상태가 원래와 달라 기준도 못 버팀 → 이전 클릭 없이, 이 클릭 바로 앞의 새 저장 지점에서 다시
            pj = -1; sList.assign(1, 0);
            if (!moveCp(cntRestoreTick(T))) return;
            if (!cntTry(pi, pj, 0, 0.0, &mode)) {
                if (!moveCp(cntRestoreTick(T), true)) return;     // 처음부터 돌려 만든 정확한 저장 지점에서 한 번 더
                if (!cntTry(pi, pj, 0, 0.0, &mode)) { g_cnt.unmeasured.push_back(T); ++g_cnt.cur; return; }
            }
        }
        bool holdMode = (mode & (1 | 8 | 16 | 64)) != 0;      // 비행기(1)·웨이브(8)·로봇(16)·스윙(64): 누르는 길이가 중요한 모드
        if (!isPress && !holdMode) { ++g_cnt.cur; return; }     // 큐브·UFO·볼·거미에서 뗌은 타이밍이 의미 없어 세지 않음
        // 이 클릭을 d 만큼 옮겨서, 이전 클릭의 경우의 수 중 하나라도 다음 클릭까지 살면 통과
        auto pass = [&](double d) { for (int sp : sList) if (cntTry(pi, pj, sp, d, nullptr)) return true; return false; };
        int be = 0; while (be < Counter::MAXS && pass(-(be + 1))) ++be;
        int dE = g_cnt.lastDie >= 0 ? g_cnt.lastDie - T : -1;
        int bl = 0; while (bl < Counter::MAXS && pass(bl + 1)) ++bl;
        int dL = g_cnt.lastDie >= 0 ? g_cnt.lastDie - T : -1;
        if (be >= Counter::MAXS) dE = -1;
        if (bl >= Counter::MAXS) dL = -1;
        double bef = be, blf = bl;
        if (g_cnt.cbf) {                       // 경계 틱 안을 반씩 나눠 1/8000초까지
            const int K = (int)std::floor(1.0 / SUB_TICK);
            auto refine = [&](int whole, int sign) {
                if (whole >= Counter::MAXS) return (double)whole;
                int lo = 0, hi = K + 1;
                while (hi - lo > 1) { int mid = (lo + hi) / 2; if (pass(sign * (whole + mid * SUB_TICK))) lo = mid; else hi = mid; }
                return whole + lo * SUB_TICK;
            };
            bef = refine(be, -1); blf = refine(bl, +1);
        }
        g_cnt.validShift[pi] = {be, bl};
        bool bp = false;
        g_cnt.res.push_back({T, be, bl, bp, isPress, bef, blf, dE, dL});
        ++g_cnt.cur;
        cntSetLabel(fmt::format("FWC counting {}/{}", g_cnt.cur, g_cnt.ev.size()));
    }
    void startMusic() { if (g_cnt.busy) return; PlayLayer::startMusic(); }              // 세는 중에는 되돌릴 때마다 음악을 다시 맞추지 않음 (느려지는 원인)
    void prepareMusic(bool dontWait) { if (g_cnt.busy) return; PlayLayer::prepareMusic(dontWait); }
    void pauseGame(bool unfocused) {
        if (fwcBlockPause()) return;
        PlayLayer::pauseGame(unfocused);
    }
    // 세는 중에 끝에 닿으면 끝 연출(플레이어·카메라를 끌고 가는 액션)을 시작하지 않고 '클리어'로만 기록.
    //   세기는 틱마다 액션도 같이 돌리므로, 연출이 시작되면 다시 시작한 뒤에도 남아서 플레이어를 끌고 가 엉뚱한 곳에서 죽음
    void playEndAnimationToPos(CCPoint pos) {
        if (g_cnt.busy) { g_cnt.completed = true; return; }
        PlayLayer::playEndAnimationToPos(pos);
    }
    void playPlatformerEndAnimationToPos(CCPoint pos, bool instant) {
        if (g_cnt.busy) { g_cnt.completed = true; return; }
        PlayLayer::playPlatformerEndAnimationToPos(pos, instant);
    }
    void levelComplete() {
        if (g_cnt.busy) { g_cnt.completed = true; return; }      // 세는 중에는 클리어 화면을 띄우지 않음
        if (g_mrec.on && !g_mrec.ev.empty()) {                   // 녹화한 매크로 저장
            auto dir = Mod::get()->getConfigDir(); std::error_code ec;
            if (std::filesystem::exists(dir / "macro.txt", ec)) std::filesystem::copy_file(dir / "macro.txt", dir / "macro_prev.txt", std::filesystem::copy_options::overwrite_existing, ec);
            { std::ofstream f(dir / "macro.txt", std::ios::binary | std::ios::trunc); f << mrecText(); }
            static const char* modeName[3] = {"off (ticks)", "fwc (between steps)", "cbf-mod (between steps, estimated)"};
            { std::ofstream f(dir / "macro_rec_info.txt", std::ios::binary | std::ios::trunc);
              f << fmt::format("level: {}\ninputs: {}\nmode: {}\nsaved to macro.txt (old one copied to macro_prev.txt)\n", g_cnt.levelName, g_mrec.ev.size(), modeName[g_mrec.mode]); }
            cntSetLabel(fmt::format("FWC macro saved: {} inputs", g_mrec.ev.size()));
        }
        PlayLayer::levelComplete();
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g_cnt.busy && object != m_anticheatSpike) { g_cnt.dead = true; return; }   // 세는 중: 죽음만 기록하고 연출은 생략
        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        g_mac.lastTick = -1;
        g_sub.clear(); g_live.clear(); g_carry.clear(); g_subP2Done = false; g_subRot1 = g_subRot2 = false;
        PlayLayer::resetLevel();
        g_stepX0 = m_player1 ? m_player1->getPositionX() : 0.f; g_stepDx = 0.f;
        if (g_mrec.on && !g_cnt.busy) {
            if (m_isPracticeMode) {                 // 이어 찍기: 되돌아간 틱부터 뒤는 지우고, 누른 채였으면 그 틱에 뗌 (게임이 되돌릴 때 누름을 풂)
                long long u0 = subUnits(cntTick());
                while (!g_mrec.ev.empty() && g_mrec.ev.back().first >= u0) g_mrec.ev.pop_back();
                g_mrec.held = !g_mrec.ev.empty() && g_mrec.ev.back().second;
                if (g_mrec.held) { g_mrec.ev.push_back({u0, false}); g_mrec.held = false; }
            } else { g_mrec.ev.clear(); g_mrec.held = false; }
        }
        resetCounts();                       // 다시 시작하면 0부터 (연습 모드 체크포인트는 고려 안 함)
        if (m_fields->m_hud) { m_fields->m_hud->m_first = true; m_fields->m_hud->refresh(0.f); }
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (m_fields->m_hud && m_player1) m_fields->m_hud->refresh(m_player1->getPositionX());
        ringsCull();
    }
    // 고리가 많아지면(수백 개) 화면 밖 것까지 매 프레임 그리는 게 렉의 원인 → 화면에 보이는 범위(+여유) 밖은 숨김.
    //   한 프레임에 최대 64개씩 돌아가며 검사 (고리가 아무리 많아도 프레임당 일이 일정)
    void ringsCull() {
        if (g_rings.empty() || !m_objectLayer) return;
        auto ws = CCDirector::get()->getWinSize();
        CCPoint c[4] = {m_objectLayer->convertToNodeSpace({0.f, 0.f}), m_objectLayer->convertToNodeSpace({ws.width, 0.f}),
                        m_objectLayer->convertToNodeSpace({0.f, ws.height}), m_objectLayer->convertToNodeSpace({ws.width, ws.height})};
        float x0 = c[0].x, x1 = c[0].x, y0 = c[0].y, y1 = c[0].y;
        for (auto const& q : c) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
        const float m = 60.f;                                // 고리 반지름 + 숫자 폭보다 넉넉하게
        x0 -= m; x1 += m; y0 -= m; y1 += m;
        size_t n = g_rings.size(), todo = std::min<size_t>(n, 64);
        for (size_t k = 0; k < todo; ++k) {
            if (g_ringCull >= n) g_ringCull = 0;
            auto& r = g_rings[g_ringCull++];
            if (!r) continue;
            auto p = r->getPosition();
            r->setVisible(p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1);
        }
    }

    // 자동 실행에서 스타트포스 고르기 (이클립스 스타트포스 바꾸기와 같은 방법: x 순서로 정렬해 n 번째)
    void addObject(GameObject* object) {
        PlayLayer::addObject(object);
        if (g_autorun && g_autorunStartPos > 0 && object && object->m_objectID == 31) m_fields->m_startPos.push_back(static_cast<StartPosObject*>(object));
    }
    void createObjectsFromSetupFinished() {
        PlayLayer::createObjectsFromSetupFinished();
        auto& v = m_fields->m_startPos;
        if (!g_autorun || g_autorunStartPos <= 0 || v.empty()) return;
        std::sort(v.begin(), v.end(), [](GameObject* a, GameObject* b) { return a->getPositionX() < b->getPositionX(); });
        int i = std::min(g_autorunStartPos, (int)v.size()) - 1;
        // 레벨을 만드는 도중에 다시 시작하면 멈추므로, 첫 프레임에서 바꿈
        log::info("자동 실행: 스타트포스 {}/{} (x {:.1f}) 로 바꿀 예정", i + 1, v.size(), v[i]->getPositionX());
        g_pendingStartPos = v[i];
    }
    void fwcApplyPendingStartPos() {
        auto sp = g_pendingStartPos; g_pendingStartPos = nullptr;
        if (!sp) return;
        m_currentCheckpoint = nullptr;
        this->setStartPosObject(sp);
        m_isTestMode = true;
        if (m_isPracticeMode) this->resetLevelFromStart();
        this->resetLevel();
        this->startMusic();
        g_mac.lastTick = -1;
        log::info("자동 실행: 스타트포스 적용 (x {:.1f})", sp->getPositionX());
    }

    void onQuit() {
        g_pendingStartPos = nullptr;
        if (g_mrec.on && !g_mrec.ev.empty()) { std::ofstream f(Mod::get()->getConfigDir() / "macro_rec_last.txt", std::ios::binary | std::ios::trunc); f << mrecText(); }
        g_mrec = MacroRec{};
        cbfModPause(false);
        clearRings();
        g_data.reset();
        PlayLayer::onQuit();
    }
};

class $modify(FwcGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && g_pendingStartPos && !g_cnt.internal) { static_cast<FwcPlayLayer*>(pl)->fwcApplyPendingStartPos(); return; }
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && !g_cnt.internal && !g_cnt.busy && m_player1) {
            if (g_rec.active) { static_cast<FwcPlayLayer*>(pl)->recWork(); return; }                       // 녹화 중: 게임을 직접 진행
            if (g_rec.want && !g_cnt.armed && (!g_cnt.on || g_cnt.done) && !m_player1->m_isDead && fwcTick(m_gameState.m_currentProgress) >= 8) {
                static_cast<FwcPlayLayer*>(pl)->recStart();
                return;
            }
        }
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && g_cnt.armed && !g_cnt.busy && !g_cnt.internal && !g_cnt.ev.empty()) {
            // 메뉴에서 시작: 결과를 비우고 지금 시작 지점에서 다시 시작한 뒤 셈
            g_cnt.armed = false; g_cnt.forced = true; g_cnt.on = true; g_cnt.done = false; g_cnt.failed = false; g_cnt.msg.clear();
            g_cnt.calib.clear(); g_cnt.repair.clear(); g_cnt.repairs = 0;
            g_cnt.res.clear(); g_cnt.unmeasured.clear(); g_cnt.cur = 0; g_cnt.trials = 0; g_cnt.resets = 0; g_cnt.ticksRun = 0; g_cnt.msReset = 0; g_cnt.msStep = 0;
            static_cast<FwcPlayLayer*>(pl)->resetLevel();
            g_mac.lastTick = -1;
            return;
        }
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && g_cnt.on && !g_cnt.internal && m_player1) {
            if (g_cnt.busy) { static_cast<FwcPlayLayer*>(pl)->cntWork(); return; }              // 세는 동안은 게임을 직접 돌림 (평소 진행은 멈춤)
            if (!g_cnt.done && !m_player1->m_isDead && fwcTick(m_gameState.m_currentProgress) >= 8) { static_cast<FwcPlayLayer*>(pl)->cntStart(); return; }
        }
        GJBaseGameLayer::update(dt);
    }

    // FWC 방식(틱 사이) 녹화: 이번 프레임에 몇 틱을 돌지 정해지는 곳. 받은 점프 입력의 시각을
    //   [직전 프레임 시작, 이번 프레임 시작] 구간에서의 비율 × 틱 수 → "몇 번째 틱의 어디쯤" 으로 바꿔 g_live 에 넣고 게임 큐에서는 뺌
    double getModifiedDelta(float dt) {
        double md = GJBaseGameLayer::getModifiedDelta(dt);
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || !g_mrec.on || g_mrec.mode != 1 || g_cnt.busy || g_rec.active) return md;
        int steps = (int)std::lround(md * 240.0);
        double span = g_frameNow - g_framePrev;
        int T0 = fwcTick(m_gameState.m_currentProgress);
        std::vector<PlayerButtonCommand> keep, mine;
        for (auto const& c : g_carry) mine.push_back(c);
        g_carry.clear();
        for (auto const& c : m_queuedButtons) {
            if (c.m_button == PlayerButton::Jump && !c.m_isPlayer2) mine.push_back(c); else keep.push_back(c);
        }
        m_queuedButtons.clear();
        for (auto const& c : keep) m_queuedButtons.push_back(c);
        if (steps <= 0) { g_carry = mine; return md; }
        for (auto const& c : mine) {
            double rel = (span > 1e-6 && c.m_timestamp > 0.0) ? (c.m_timestamp - g_framePrev) / span : 0.0;
            if (rel >= 1.0) { g_carry.push_back(c); continue; }             // 아직 이번 프레임 시각이 아님
            rel = std::clamp(rel, 0.0, 0.999999);
            double t = T0 + rel * steps;                                     // 틱 + 위치
            inputsAdd(g_live, t, c.m_isPush);
            mrecAdd(t, c.m_isPush);
        }
        return md;
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || !m_player1) return;
        if (g_mac.on) {
            int tick = fwcTick(m_gameState.m_currentProgress);
            if (tick != g_mac.lastTick) {  // 같은 틱에 두 번 불려도 입력은 한 번만 넣음
                g_mac.lastTick = tick;
                auto const& inp = g_cnt.useTrial ? g_cnt.trial : g_mac.inputs;
                auto it = inp.find(tick);
                if (it != inp.end()) for (In const& x : it->second) {
                    if (x.frac > 0.f) g_sub.push_back({x.frac, x.down});        // 틱 사이 입력: 캐릭터 물리를 나눠 돌릴 때 넣음
                    else this->handleButton(x.down, 1, true);
                }
            }
        }
        // FWC 방식 녹화(틱 사이): 이번 프레임에 받은 실제 입력을 이 틱에 넣음
        if (!g_mac.on && !g_live.empty()) {
            while (!g_live.empty() && g_live.begin()->first < fwcTick(m_gameState.m_currentProgress)) {      // 놓친 틱(지연)은 지금 바로
                for (In const& x : g_live.begin()->second) this->handleButton(x.down, 1, true);
                g_live.erase(g_live.begin());
            }
        }
        if (!g_mac.on && g_live.count(fwcTick(m_gameState.m_currentProgress))) {
            int tick = fwcTick(m_gameState.m_currentProgress);
            for (In const& x : g_live[tick]) {
                if (x.frac > 0.f) g_sub.push_back({x.frac, x.down});
                else this->handleButton(x.down, 1, true);
            }
            g_live.erase(tick);
        }
        if (g_sub.size() > 1) std::stable_sort(g_sub.begin(), g_sub.end(), [](SubIn const& a, SubIn const& b) { return a.frac < b.frac; });
        // CBF 모드 켠 채 녹화(추정): 이 틱 시작의 x, 직전 한 틱 동안 움직인 x
        g_stepDx = m_player1->getPositionX() - g_stepX0; g_stepX0 = m_player1->getPositionX();
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (g_mrec.on && g_mrec.mode != 1 && button == 1 && isPlayer1 && !g_cnt.busy) {
            auto plr = PlayLayer::get();
            if (plr && static_cast<GJBaseGameLayer*>(plr) == this && m_player1) {
                double fr = 0.0;
                if (g_mrec.mode == 2 && g_stepDx > 0.01f)          // CBF 가 틱 중간에 넣은 입력: 이 틱에서 움직인 비율로 위치 추정
                    fr = std::clamp((double)(m_player1->getPositionX() - g_stepX0) / g_stepDx, 0.0, 0.9999);
                mrecAdd(fwcTick(m_gameState.m_currentProgress) + fr, down);
            }
        }
        if (g_clickOn && button == 1 && !g_cnt.busy) {
            auto pl0 = PlayLayer::get();
            if (pl0 && static_cast<GJBaseGameLayer*>(pl0) == this) FMODAudioEngine::sharedEngine()->playEffect(down ? g_clickPress : g_clickRelease);
        }
        if (button != 1 || !g_data || g_cnt.busy) return;                    // 점프 버튼만 (세는 중에는 집계 안 함). 누름·뗌 둘 다 셈
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this) return;
        int tick = fwcTick(m_gameState.m_currentProgress);
        int i = matchPress(tick, down);
        if (i < 0) return;                                     // 계산 경로에 없는 누름은 안 셈
        g_used[i] = true;
        if (g_data->presses[i].window > MAX_SHOWN_WINDOW) return;   // 너무 넓은 윈도우는 표시하지 않음 (판에도 안 세고 고리도 안 그림)
        g_count[binOf(g_data->presses[i].window)] += 1;
        g_lastIdx = std::max(g_lastIdx, i);
        if (g_showRings && m_objectLayer) {
            auto pl1 = isPlayer1 ? m_player1 : m_player2;
            if (pl1) {
                auto ring = makeRing(g_data->presses[i].window, g_data->presses[i].wf, pl1->getPosition());
                m_objectLayer->addChild(ring, 9999);
                g_rings.push_back(ring);
            }
        }
    }
};

// ── 틱 나누기 (CBF 원리: 클릭한 시점까지만 캐릭터를 움직이고 → 입력 → 나머지만큼 움직임) ──
//   syzzi 의 Click Between Frames(MIT) 가 쓰는 방식을 참고해 이 모드 안에서 새로 작성.
//   - 땅·링·대시 위가 아니고 누르는 길이가 중요한 모드도 아니면(공중에서 미리 누르는 "버퍼"), 나누지 않고 틱 시작에 넣음 (CBF 와 같음)
//   - 나눈 조각마다 충돌 검사(피해 무시)·회전을 하고 충돌 기록을 비움 (웨이브용). 죽음은 틱 끝 정상 검사에서 판정
//   - 2번 플레이어(듀얼)는 1번의 update 안에서 같이 나누고, 원래 2번 update 는 건너뜀
class $modify(FwcPlayer, PlayerObject) {
    static void resetLog(PlayerObject* p) {
        p->m_collisionLogTop->removeAllObjects(); p->m_collisionLogBottom->removeAllObjects();
        p->m_collisionLogLeft->removeAllObjects(); p->m_collisionLogRight->removeAllObjects();
        p->m_lastCollisionLeft = -1; p->m_lastCollisionRight = -1; p->m_lastCollisionBottom = -1; p->m_lastCollisionTop = -1;
    }
    static bool canSplit(PlayerObject* p) {
        return p->m_isOnGround || (p->m_touchingRings && p->m_touchingRings->count()) || p->m_isDashing
            || p->m_isDart || p->m_isBird || p->m_isShip || p->m_isSwing;
    }
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!pl || g_subMid) { PlayerObject::update(dt); return; }
        if (this == pl->m_player2 && g_subP2Done) { g_subP2Done = false; return; }          // 1번 update 안에서 이미 나눠 돌림
        if (this == pl->m_player1) g_subStep = false;                                       // 새 틱
        if (this != pl->m_player1 || g_sub.empty()) { PlayerObject::update(dt); return; }
        auto p2 = pl->m_player2;
        bool dual = pl->m_gameState.m_isDualMode && p2;
        std::vector<SubIn> ins; ins.swap(g_sub);
        bool split1 = canSplit(this), split2 = dual && canSplit(p2);
        if (!split1 && !split2) {                                   // 버퍼: 틱 시작에 넣고 보통대로
            for (auto const& x : ins) pl->handleButton(x.down, 1, true);
            PlayerObject::update(dt);
            return;
        }
        g_subStep = true;
        g_subP1Pos = this->getPosition(); if (dual) g_subP2Pos = p2->getPosition();
        bool g1 = this->m_isOnGround, g2 = dual ? p2->m_isOnGround : false;
        const float tiny = std::numeric_limits<float>::min();
        float done = 0.f; bool first = true;
        g_subMid = true;
        for (auto const& x : ins) {
            float part = std::max(tiny, x.frac - done), sdt = dt * part;
            if (split1) {
                PlayerObject::update(sdt);
                if (first && ((this->m_yVelocity < 0) ^ this->m_isUpsideDown)) this->m_isOnGround = g1;
                pl->checkCollisions(this, (this->m_isOnSlope && !this->m_isDart) ? dt : 0.f, true);
                PlayerObject::updateRotation(sdt);
                resetLog(this);
            }
            if (split2) {
                p2->update(sdt);
                if (first && ((p2->m_yVelocity < 0) ^ p2->m_isUpsideDown)) p2->m_isOnGround = g2;
                pl->checkCollisions(p2, (p2->m_isOnSlope && !p2->m_isDart) ? dt : 0.f, true);
                p2->updateRotation(sdt);
                resetLog(p2);
            }
            pl->handleButton(x.down, 1, true);
            done = x.frac; first = false;
        }
        float rest = dt * std::max(tiny, 1.f - done);
        if (split1) PlayerObject::update(rest); else PlayerObject::update(dt);
        if (dual) { if (split2) p2->update(rest); else p2->update(dt); g_subP2Done = true; }
        g_subMid = false;
        g_subRot = rest; g_subRot1 = split1; g_subRot2 = split2;
    }
#ifndef FWC_GD2209
    // 비행기·스윙 회전: 게임의 회전 보간(Slerp2D)은 시간에 비례하지 않아서, 틱을 조각내면 조각마다 너무 많이 돈다.
    //   나눈 틱에서는 아주 작은 시간(1/1024)으로 불러 "단위 회전량"을 얻고, 그걸 실제 조각 시간에 비례하게 늘림 (CBF 와 같은 원리)
    void updateShipRotation(float t) {
        auto pl = PlayLayer::get();
        if (pl && g_subStep && (this == pl->m_player1 || this == pl->m_player2)) {
            g_shipRotDelta = t;
            PlayerObject::updateShipRotation(1.f / 1024.f);
            g_shipRotDelta = 0.f;
        }
        else PlayerObject::updateShipRotation(t);
    }
#endif
    // 게임이 틱 끝에 부르는 회전: 나눠 돌린 경우 남은 조각만큼만 돌리고, 직전 위치를 틱 시작 위치로 (움직이는 트리거·거미가 헷갈리지 않게)
    void updateRotation(float t) {
        auto pl = PlayLayer::get();
        if (pl && !g_subMid && this == pl->m_player1 && g_subRot1) { g_subRot1 = false; PlayerObject::updateRotation(g_subRot); this->m_lastPosition = g_subP1Pos; return; }
        if (pl && !g_subMid && this == pl->m_player2 && g_subRot2) { g_subRot2 = false; PlayerObject::updateRotation(g_subRot); this->m_lastPosition = g_subP2Pos; return; }
        PlayerObject::updateRotation(t);
    }
};

#ifndef FWC_GD2209
static constexpr uintptr_t SLERP2D_2081 = 0x71ef0;          // 2.2081 윈도우의 Slerp2D(float, float, float) 위치
static float fwcSlerp2D(float a, float b, float f) {
    auto orig = reinterpret_cast<float (*)(float, float, float)>(geode::base::get() + SLERP2D_2081);
    if (g_shipRotDelta != 0.f) return orig(a, b, g_shipRotDelta * f * 1024.f);     // f 는 (1/1024 × 상수) → 상수 × 실제 시간
    return orig(a, b, f);
}
$on_mod(Loaded) {
    auto r = Mod::get()->hook(reinterpret_cast<void*>(geode::base::get() + SLERP2D_2081), &fwcSlerp2D, "Slerp2D", tulip::hook::TulipConvention::Default);
    if (!r) log::warn("Slerp2D 가로채기 실패: {}", r.unwrapErr());
}
#endif

// 세는 중에는 음악 위치 옮기기·다시 틀기를 건너뜀 (되돌리기 한 번마다 걸리는 시간을 줄임)
class $modify(FwcAudio, FMODAudioEngine) {
    void setMusicTimeMS(unsigned int time, bool dontWait, int musicID) {
        if (g_cnt.busy) return;
        FMODAudioEngine::setMusicTimeMS(time, dontWait, musicID);
    }
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        if (g_cnt.busy) return;
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
    }
};

// 일시정지 메뉴에 "FWC Count" 버튼: 지금 고른 시작 지점(스타트포스)에서 윈도우 세기를 시작
static bool s_settingsOpened = false;
class $modify(FwcPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (g_cnt.busy) return;
        auto ws = CCDirector::get()->getWinSize();
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 100);
        // FWC 켜기/끄기 (설정의 enabled 와 같음). 누르면 바로 적용되고 지금 시작 지점에서 다시 시작
        bool en = Mod::get()->getSettingValue<bool>("enabled");
        auto tog = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(FwcPause::onFwcToggle), 0.7f);
        tog->toggle(en);
        tog->setPosition({ws.width - 30.f, 22.f});
        menu->addChild(tog);
        auto lbl = CCLabelBMFont::create("FWC", "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setPosition({ws.width - 62.f, 22.f});
        this->addChild(lbl, 100);
        // FWC 설정 창 (지오드 메뉴까지 가지 않고). 바꾼 설정은 FWC 토글을 다시 누르거나 레벨을 다시 열면 적용
        {
            auto gs = ButtonSprite::create("FWC Settings");
            gs->setScale(0.55f);
            auto gb = CCMenuItemSpriteExtra::create(gs, this, menu_selector(FwcPause::onFwcSettings));
            gb->setPosition({ws.width - 62.f, 50.f});
            menu->addChild(gb);
        }
        if (en && !g_cnt.ev.empty()) {                       // 매크로를 재생 중일 때만: 지금 시작 지점에서 윈도우 세기
            auto spr = ButtonSprite::create("FWC Count");
            spr->setScale(0.7f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FwcPause::onFwcCount));
            btn->setPosition({ws.width / 2.f, 22.f});
            menu->addChild(btn);
        }
    }
    void onFwcSettings(CCObject*) { s_settingsOpened = true; geode::openSettingsPopup(Mod::get(), false); }
    // 설정 창을 열었었으면, 계속하기를 누를 때 바뀐 설정을 적용하고 지금 시작 지점에서 다시 시작
    void onResume(CCObject* sender) {
        bool apply = s_settingsOpened; s_settingsOpened = false;
        PauseLayer::onResume(sender);
        if (apply) if (auto pl = static_cast<FwcPlayLayer*>(PlayLayer::get())) { pl->fwcSetup(); pl->resetLevel(); g_mac.lastTick = -1; }
    }
    void onFwcToggle(CCObject* sender) {
        bool en = !Mod::get()->getSettingValue<bool>("enabled");          // 토글러는 누른 뒤에 상태가 바뀌므로 설정 값을 기준으로 뒤집음
        Mod::get()->setSettingValue<bool>("enabled", en);
        if (auto pl = static_cast<FwcPlayLayer*>(PlayLayer::get())) {
            pl->fwcSetup();
            pl->resetLevel();
            g_mac.lastTick = -1;
        }
        this->onResume(sender);
    }
    void onFwcCount(CCObject* sender) {
        g_cnt.armed = true;
        this->onResume(sender);
    }
};

// 자동 실행: 설정 폴더에 autorun.txt (레벨 이름 한 줄)가 있으면, 게임이 메인 메뉴에 도착했을 때 그 내 레벨을 바로 연다.
//   자리에 없을 때 세기를 돌리기 위한 용도. 게임을 켤 때 한 번만 동작.
class $modify(FwcMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        static bool s_done = false;
        if (s_done) return true;
        s_done = true;
        std::string name;
        {   std::ifstream f(Mod::get()->getConfigDir() / "autorun.txt"); std::getline(f, name);
            std::string sp; if (std::getline(f, sp)) { try { g_autorunStartPos = std::max(0, std::stoi(sp)); } catch (...) { g_autorunStartPos = 0; } } }
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) name.pop_back();
        if (name.empty()) return true;
        std::vector<GJGameLevel*> cands;            // 내 레벨 먼저, 없으면 저장된 온라인 레벨
        if (auto arr = LocalLevelManager::get()->m_localLevels) for (auto lvl : CCArrayExt<GJGameLevel*>(arr)) cands.push_back(lvl);
        if (auto dict = GameLevelManager::get()->m_onlineLevels) for (auto [key, lvl] : CCDictionaryExt<gd::string, GJGameLevel*>{dict}) cands.push_back(lvl);
        {   // 같은 이름이 여러 개면 가장 긴 것(레벨 데이터가 가장 큰 것) 하나만
            // 길이 = 오브젝트 x 의 최댓값 (레벨 데이터를 풀어서 셈)
            auto lenOf = [](GJGameLevel* lvl) {
                std::string d = cocos2d::ZipUtils::decompressString(lvl->m_levelString, false, 0);
                double mx = 0; size_t pos = 0;
                while ((pos = d.find(";1,", pos)) != std::string::npos) {
                    size_t end = d.find(';', pos + 1); if (end == std::string::npos) end = d.size();
                    size_t k = d.find(",2,", pos + 1);
                    if (k != std::string::npos && k < end) mx = std::max(mx, std::strtod(d.c_str() + k + 3, nullptr));
                    pos = end;
                }
                return mx;
            };
            GJGameLevel* best = nullptr; double bestLen = -1;
            for (auto lvl : cands) if (lvl && std::string(lvl->m_levelName) == name) { double L = lenOf(lvl); log::info("자동 실행: '{}' 후보 길이 x {:.1f}", name, L); if (L > bestLen) { best = lvl; bestLen = L; } }
            cands.clear(); if (best) { cands.push_back(best); log::info("자동 실행: 같은 이름 중 가장 긴 레벨 (x {:.1f})", bestLen); }
        }
        for (auto lvl : cands) {
            if (lvl && std::string(lvl->m_levelName) == name) {
                log::info("자동 실행: 레벨 '{}' 열기", name);
                g_autorun = true;
                lvl->retain();
                Loader::get()->queueInMainThread([lvl]() {
                    CCDirector::get()->replaceScene(PlayLayer::scene(lvl, false, false));
                    lvl->release();
                });
                break;
            }
        }
        return true;
    }
};

// 자동 실행·녹화·세기 중에는 창이 뒤로 가도(다른 창이 앞에 와도) 게임이 멈추지 않게 함. 평소에는 원래대로 동작.
class $modify(FwcApp, AppDelegate) {
    void pauseGame() {
        if (fwcBlockPause()) return;
        AppDelegate::pauseGame();
    }
    void applicationWillResignActive() {
        if (g_autorun || g_rec.want || g_rec.active || g_cnt.busy) return;
        AppDelegate::applicationWillResignActive();
    }
    void applicationDidEnterBackground() {
        if (g_autorun || g_rec.want || g_rec.active || g_cnt.busy) return;
        AppDelegate::applicationDidEnterBackground();
    }
};

// 녹화 중에는 게임 전체의 "프레임 시간"을 영상 한 프레임(1/60초)으로 고정한다.
//   파티클·트레일 같은 화면 효과는 이 시간으로 움직이므로, 고정하지 않으면 영상에서 너무 느리거나 끊겨 보인다.
//   실제 한 프레임 동안 이 갱신을 REC_PER_FRAME 번 연달아 돌린다 (한 번 = 영상 한 프레임 = 게임 4틱 + 파티클·트레일 1/60초).
//   전에는 실제 한 프레임에 영상 한 프레임이라 녹화 속도가 게임 fps 에 묶여 있었음. 화면에는 마지막 상태만 보임 (빨리 감기처럼 보임)
static constexpr int REC_PER_FRAME = 4;
class $modify(FwcSched, CCScheduler) {
    void update(float dt) {
        {   static LARGE_INTEGER freq{}; if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            g_framePrev = g_frameNow; g_frameNow = (double)now.QuadPart / (double)freq.QuadPart; }
        if (!g_rec.active) { CCScheduler::update(dt); return; }
        for (int k = 0; k < REC_PER_FRAME && g_rec.active; ++k) CCScheduler::update(1.f / (float)g_rec.fps);
    }
};
