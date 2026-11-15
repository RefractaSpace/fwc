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
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/utils/file.hpp>
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
struct Press { int tick; int window; bool down = true; };   // down=false 면 뗌(릴리즈)

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
                d.presses.push_back({(int)w["tick"].asInt().unwrapOr(0), (int)w["window"].asInt().unwrapOr(1), kind == "press"});
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
static CCNode* makeRing(int window, CCPoint pos) {
    auto col3 = BIN_COLOR[binOf(window)];
    ccColor4F col = {col3.r / 255.f, col3.g / 255.f, col3.b / 255.f, 1.f};
    ccColor4F black = {0.f, 0.f, 0.f, 1.f}, clear = {0.f, 0.f, 0.f, 0.f};
    auto node = CCNode::create();
    auto dn = CCDrawNode::create();
    float rIn = 7.0f, th = 4.3f, edge = 1.3f;
    dn->drawCircle({0.f, 0.f}, rIn - edge, clear, th + 2.f * edge, black, 48);
    dn->drawCircle({0.f, 0.f}, rIn, clear, th, col, 48);
    node->addChild(dn);
    auto lab = CCLabelBMFont::create(std::to_string(window).c_str(), "bigFont.fnt");
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

// ── 매크로 재생. 모드 설정 "매크로 재생"을 켜면 설정 폴더 macro.txt 를 재생 ──
//   macro.txt: 줄마다 "틱 1" (누름) 또는 "틱 0" (뗌). 이클립스 macro.gdr2 를 넣으면 자동으로 macro.txt 로 바뀜
struct Macro {
    bool on = false;
    std::map<int, std::vector<bool>> inputs;
    int lastTick = -1;
    bool heldAt(int t) const { bool h = false; for (auto const& kv : inputs) { if (kv.first > t) break; for (bool dn : kv.second) h = dn; } return h; }
};
static Macro g_mac;
// 이클립스 매크로 바로 읽기: 설정 폴더의 macro.gdr2 가 macro.txt 보다 새것(또는 macro.txt 없음)이면 macro.txt 로 변환한다.
//   기존 macro.txt 는 macro_prev.txt 로 복사해 둔다. 결과는 macro_import.txt 에 적는다.
static void macroImportGdr2(std::filesystem::path const& dir) {
    std::error_code ec;
    auto src = dir / "macro.gdr2", dst = dir / "macro.txt";
    if (!std::filesystem::exists(src, ec)) return;
    if (std::filesystem::exists(dst, ec)) {
        auto ts = std::filesystem::last_write_time(src, ec); if (ec) return;
        auto td = std::filesystem::last_write_time(dst, ec); if (ec) return;
        if (ts <= td) return;                                              // 이미 변환했음
    }
    auto note = [&](std::string const& msg) { std::ofstream o(dir / "macro_import.txt", std::ios::binary | std::ios::trunc); o << msg << "\n"; };
    std::string bytes;
    { std::ifstream in(src, std::ios::binary); bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()); }
    std::vector<std::pair<int, bool>> ev; FwcGdr2Info info; std::string err;
    if (!fwcReadGdr2(bytes, ev, info, err)) { note("FAILED: " + err); return; }
    if (std::fabs(info.tps - 240.0) > 0.01) { note(fmt::format("FAILED: macro is {} TPS, this mod needs 240 TPS", info.tps)); return; }
    if (ev.empty()) { note("FAILED: no player 1 jump inputs in macro.gdr2"); return; }
    if (std::filesystem::exists(dst, ec)) std::filesystem::copy_file(dst, dir / "macro_prev.txt", std::filesystem::copy_options::overwrite_existing, ec);
    {
        std::ofstream o(dst, std::ios::binary | std::ios::trunc);
        for (auto const& e : ev) o << e.first << ' ' << (e.second ? 1 : 0) << "\n";
    }
    note(fmt::format("OK: {} inputs from macro.gdr2 (level '{}', skipped: {} player-2 inputs, {} left/right inputs)", ev.size(), info.levelName, info.p2Inputs, info.otherButtons));
}
static void macroLoad() {
    g_mac = Macro{};
    auto dir = Mod::get()->getConfigDir();
    if (!Mod::get()->getSettingValue<bool>("macro-play")) return;          // 설정: 모드가 매크로 재생
    macroImportGdr2(dir);                                                  // 설정 폴더에 macro.gdr2 (이클립스 매크로)가 새로 들어왔으면 macro.txt 로 바꿔 둔다
    std::ifstream f(dir / "macro.txt");
    int t, d; int n = 0;
    while (f >> t >> d) { g_mac.inputs[t].push_back(d != 0); ++n; }
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
    bool armed = false;                        // 일시정지 메뉴의 "FWC Count" 를 누름 → 지금 시작 지점에서 다시 시작해 세기
    bool dead = false, completed = false, useTrial = false;
    std::vector<std::pair<int, bool>> ev;      // (틱, 누름?) 순서대로
    std::vector<int> pressIdx;                 // ev 안에서 누름의 번호
    size_t cur = 0;
    CheckpointObject* cp = nullptr; int cpTick = -1;
    struct R { int tick, early, late; bool pair, down; };
    std::vector<R> res;
    std::map<int, std::vector<bool>> trial;    // 이번 시도에 쓰는 입력
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

// ── 영상 녹화 ──
//   매크로를 화면 없이 돌릴 때와 같은 방식(틱마다 애니메이션 → 물리)으로 게임을 직접 진행시키면서,
//   영상 한 프레임(60fps = 4틱)마다 화면을 별도 버퍼에 그려 ffmpeg 로 넘긴다. 실제 시간과 무관하게 정확히 60fps 로 찍힌다.
//   ffmpeg 위치는 설정 폴더 ffmpeg_path.txt, 결과는 설정 폴더 showcase.mp4 (소리 없음, 정보는 record_info.txt)
struct Recorder {
    bool want = false, active = false, done = false;
    int w = 1280, h = 720, fps = 60;
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
    std::string cmd = fmt::format("\"{}\" -y -f rawvideo -pix_fmt rgba -s {}x{} -r {} -i - -vf vflip -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"{}\"",
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
    if (g_autorun) return true;
    if (g_rec.want || g_rec.active || g_cnt.busy) return !fwcFocused();
    return false;
}

class $modify(FwcPlayLayer, PlayLayer) {
    struct Fields {
        FwcHud* m_hud = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g_data = loadFor(std::string(level->m_levelName));
        if (!Mod::get()->getSettingValue<bool>("enabled")) g_data.reset();      // 설정에서 끄면 표시·집계 안 함
        macroLoad();
        clickLoad();
        g_showRings = Mod::get()->getSettingValue<bool>("show-rings");
        g_rec = Recorder{};
        g_rec.want = g_mac.on && Mod::get()->getSettingValue<bool>("record-video");
        g_cnt = Counter{};
        g_cnt.on = g_mac.on && Mod::get()->getSettingValue<bool>("count-windows");      // 설정이 켜져 있으면 레벨을 열자마자 셈
        g_cnt.levelName = std::string(level->m_levelName);
        if (g_mac.on) {                                 // 입력 목록 만들기 (나중에 메뉴에서 시작할 수도 있으므로 항상)
            for (auto const& kv : g_mac.inputs) for (bool dn : kv.second) g_cnt.ev.push_back({kv.first, dn});
            for (int i = 0; i < (int)g_cnt.ev.size(); ++i) if (g_cnt.ev[i].second) g_cnt.pressIdx.push_back(i);
            if (g_cnt.pressIdx.empty()) { g_cnt.on = false; g_cnt.ev.clear(); }
        }
        resetCounts();
        if (g_data) {
            auto hud = FwcHud::create();
            hud->setZOrder(1000);
            this->addChild(hud);
            m_fields->m_hud = hud;
            hud->refresh(0.f);
        }
        return true;
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
            std::map<int, std::vector<bool>> sh;
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
        g_cnt.ev.clear(); g_cnt.pressIdx.clear();
        for (auto const& kv : g_mac.inputs) for (bool dn : kv.second) g_cnt.ev.push_back({kv.first, dn});
        for (int i = 0; i < (int)g_cnt.ev.size(); ++i) if (g_cnt.ev[i].second) g_cnt.pressIdx.push_back(i);
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
        node->visit();
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
        add("Vanilla 240 FPS", R, 0.040f, 1.f, sHdr, BIN_COLOR[0], 0.f);
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
    void recIntro() {
        if (!Mod::get()->getSettingValue<bool>("record-intro")) return;
        Ref<CCNode> intro = makeIntro();
        recCapture(intro);
        const int n = g_rec.fps * 3;                         // 3초 (같은 그림을 그대로 반복)
        for (int i = 0; i < n && g_rec.active; ++i) recWrite();
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
                { auto& v = g_cnt.trial[T]; for (auto it = v.begin(); it != v.end(); ++it) if (*it == ev[ci].second) { v.erase(it); break; } if (v.empty()) g_cnt.trial.erase(T); g_cnt.trial[nT].push_back(ev[ci].second); }
                if (R > 0) cntRestore(); else cntFresh();
                g_cnt.useTrial = true; ++g_cnt.trials;
                int reach = cntRunUntil(std::min(goal, D + 720));
                g_cnt.useTrial = false;
                if (reach > bestReach || (reach == bestReach && bestI >= 0 && std::abs(d) < std::abs(bestD))) { bestReach = reach; bestI = ci; bestD = d; }
            }
            if (bestI < 0 || bestReach <= D + 20) { g_cnt.repair += fmt::format("stuck at tick {} (no shift helps); ", D); return false; }
            {   // 적용
                int T = ev[bestI].first, nT = T + bestD; bool dn = ev[bestI].second;
                auto& v = g_mac.inputs[T]; for (auto it = v.begin(); it != v.end(); ++it) if (*it == dn) { v.erase(it); break; } if (v.empty()) g_mac.inputs.erase(T);
                g_mac.inputs[nT].push_back(dn);
                g_cnt.repair += fmt::format("{} {} {}->{} (died {}); ", dn ? "press" : "release", T, T, nT, D);
                ++g_cnt.repairs;
            }
        }
        return false;
    }
    void cntRebuildEv() {
        g_cnt.ev.clear(); g_cnt.pressIdx.clear();
        for (auto const& kv : g_mac.inputs) for (bool dn : kv.second) g_cnt.ev.push_back({kv.first, dn});
        for (int i = 0; i < (int)g_cnt.ev.size(); ++i) if (g_cnt.ev[i].second) g_cnt.pressIdx.push_back(i);
    }
    void cntSaveMacro(const char* backupName) {     // 지금 입력을 macro.txt 로 저장 (기존 파일은 backupName 으로 한 번만 보관)
        auto dir = Mod::get()->getConfigDir(); std::error_code ec;
        if (!std::filesystem::exists(dir / backupName, ec)) std::filesystem::copy_file(dir / "macro.txt", dir / backupName, ec);
        std::ofstream f(dir / "macro.txt", std::ios::binary | std::ios::trunc);
        for (auto const& kv : g_mac.inputs) for (bool dn : kv.second) f << kv.first << " " << (dn ? 1 : 0) << "\n";
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
        std::map<int, std::vector<bool>> partB; double X = 0; int nB = 0;
        { std::ifstream f(dir / "macro_part2.txt"); int t, d; while (f >> t >> d) { partB[t].push_back(d != 0); ++nB; } }
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
            std::map<int, std::vector<bool>> m; bool held = false;
            for (auto const& kv : partA) { if (kv.first >= J) break; m[kv.first] = kv.second; for (bool dn : kv.second) held = dn; }
            if (held) m[J].push_back(false);         // 이음매에서 누르고 있었으면 뗌
            for (auto const& kv : partB) { auto& v = m[J + kv.first]; v.insert(v.end(), kv.second.begin(), kv.second.end()); }
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
        std::map<int, std::vector<bool>> merged = build(bestJ);
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
    void cntStart() {
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
    bool cntTrial(std::vector<Sig> const& ref, bool refCompleted, int pi, bool pair, int d) {
        auto const& ev = g_cnt.ev;
        int T = ev[pi].first;
        bool hasRel = pi + 1 < (int)ev.size() && !ev[pi + 1].second;
        if (pair && !hasRel) return false;
        int nT = T + d, relT = hasRel ? ev[pi + 1].first : -1, nRel = pair ? relT + d : relT;
        int prevT = pi > 0 ? ev[pi - 1].first : 0;
        int afterIdx = pair ? pi + 2 : pi + 1;
        int nextT = afterIdx < (int)ev.size() ? ev[afterIdx].first : INT_MAX;
        if (nT <= prevT || nT <= g_cnt.cpTick + 1) return false;          // 앞 입력을 넘어가면 안 됨
        if (pair ? nRel >= nextT : nT >= nextT) return false;             // 뒤 입력을 넘어가면 안 됨
        g_cnt.trial = g_mac.inputs;                                    // 입력 복사 후 이 누름(과 뗌)만 옮김
        auto move = [&](int from, int to, bool dn) {
            auto& v = g_cnt.trial[from];
            for (auto it = v.begin(); it != v.end(); ++it) if (*it == dn) { v.erase(it); break; }
            if (v.empty()) g_cnt.trial.erase(from);
            g_cnt.trial[to].push_back(dn);
        };
        move(T, nT, ev[pi].second);
        if (pair) move(relT, nRel, false);
        cntRestore();
        g_cnt.useTrial = true; ++g_cnt.trials;
        int lastChanged = std::max({T, nT, pair ? std::max(relT, nRel) : T});
        int refEnd = g_cnt.cpTick + (int)ref.size();
        // 중간 판정 시점: 바뀐 입력 뒤 "다다음 입력 + 30틱", 단 120~360틱 사이
        int cnt2 = 0, t2 = lastChanged + Counter::REJOIN_MIN;
        for (auto const& kv : g_cnt.trial) { if (kv.first <= lastChanged) continue; if (++cnt2 == 2) { t2 = kv.first + 30; break; } }
        t2 = std::min(std::max(t2, lastChanged + Counter::REJOIN_MIN), lastChanged + Counter::REJOIN_MAX);
        int end = std::min(lastChanged + Counter::HORIZON, refEnd);
        bool ok = true;
        while (cntTick() < end) {
            int before = cntTick();
            cntStep();
            if (g_cnt.dead) { ok = false; break; }
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
        std::string js = "{\n  \"source\": \"geode\", \"level_end\": 0, \"n\": " + std::to_string(g_cnt.res.size()) + ",\n  \"windows\": [\n";
        std::string tb = fmt::format("# 게임 안 윈도우 세기: 누름 {}개, {:.1f}초, 시도 {}번, 되돌리기 {}번({:.1f}초), 진행 {}틱({:.1f}초){}\n# tick early late window pair kind\n",
                                     g_cnt.res.size(), sec, g_cnt.trials, g_cnt.resets, g_cnt.msReset / 1000.0, g_cnt.ticksRun, g_cnt.msStep / 1000.0, g_cnt.msg.empty() ? "" : " (" + g_cnt.msg + ")");
        tb += "# calibration: " + g_cnt.calib + "chosen offset " + std::to_string(g_cnt.offset) + "\n";
        if (!g_cnt.merge.empty()) tb += "# merge: " + g_cnt.merge + "\n";
        tb += fmt::format("# restore check: {} ticks compared, position differs {}, rotation-only differs {}\n", g_cnt.chkTicks, g_cnt.chkPosDiff, g_cnt.chkRotDiff);
        if (!g_cnt.repair.empty()) tb += "# repair (" + std::to_string(g_cnt.repairs) + " changes): " + g_cnt.repair + "\n";
        for (size_t i = 0; i < g_cnt.res.size(); ++i) {
            auto const& r = g_cnt.res[i]; int w = r.early + r.late + 1;
            js += fmt::format("    {{\"kind\": \"{}\", \"tick\": {}, \"early\": {}, \"late\": {}, \"window\": {}, \"pair\": {}}}{}\n", r.down ? "press" : "release", r.tick, r.early, r.late, w, r.pair ? "true" : "false", i + 1 < g_cnt.res.size() ? "," : "");
            tb += fmt::format("{} {} {} {} {} {}\n", r.tick, r.early, r.late, w, (int)r.pair, r.down ? "press" : "release");
        }
        js += "  ]\n}\n";
        { std::ofstream f(levelsDir() / (safe + ".json"), std::ios::binary | std::ios::trunc); f << js; }
        { std::ofstream f(Mod::get()->getConfigDir() / "windows_ingame.txt", std::ios::binary | std::ios::trunc); f << tb; }
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
    // 프레임마다 누름 하나씩 처리
    void cntWork() {
        if (g_cnt.cur >= g_cnt.ev.size()) { cntFinish(); return; }
        int pi = (int)g_cnt.cur;
        bool isPress = g_cnt.ev[pi].second;
        if (g_cnt.ev[pi].first < 12) { ++g_cnt.cur; return; }             // 시작 직후의 뗌 같은 것은 건너뜀
        if (pi > 0 && g_cnt.ev[pi - 1].second == isPress) { ++g_cnt.cur; return; }   // 이미 뗀 상태에서 또 뗌(또는 누른 상태에서 또 누름): 아무 효과 없는 입력
        int T = g_cnt.ev[pi].first;
        int R = cntRestoreTick(T);
        if (R < 0) R = 6;
        // 저장 지점을 R 로 옮기기
        if (g_cnt.cp) cntRestore();
        if (!g_cnt.cp || R > g_cnt.cpTick) {
            g_cnt.useTrial = false;
            if (cntTick() > R) { g_cnt.failed = true; g_cnt.msg = fmt::format("press {}: restore tick {} is in the past", T, R); cntFinish(); return; }
            if (!cntStepTo(R)) { g_cnt.failed = true; g_cnt.msg = fmt::format("press {}: macro dies before tick {}", T, R); cntFinish(); return; }
            cntDropCp();
            cntMakeCp();
        }
        // 원래 경로 기록 (저장 지점 다음 틱부터)
        std::vector<Sig> ref; bool refCompleted = false;
        int refTo = T + Counter::MAXS + Counter::HORIZON + 4;
        g_cnt.useTrial = false;
        while (cntTick() < refTo) {
            int before = cntTick();
            cntStep();
            if (g_cnt.dead) break;
            if (g_cnt.completed) { refCompleted = true; break; }
            if (cntTick() != before) {
                ref.push_back(cntSig());
                int t = cntTick();
                if (t > 0 && t < (int)g_cnt.fullRef.size() && g_cnt.fullRef[t].x != 0) {
                    auto const& r = g_cnt.fullRef[t]; auto const& q = ref.back();
                    ++g_cnt.chkTicks;
                    if (q.x != r.x || q.y != r.y || q.v != r.v) ++g_cnt.chkPosDiff;
                    else if (m_player1->getRotation() != r.rot) ++g_cnt.chkRotDiff;
                }
            }
        }
        int modeAtT = (T - g_cnt.cpTick - 1 >= 0 && T - g_cnt.cpTick - 1 < (int)ref.size()) ? ref[T - g_cnt.cpTick - 1].mode : 0;
        bool holdMode = (modeAtT & (1 | 8 | 16 | 64)) != 0;      // 비행기(1)·웨이브(8)·로봇(16)·스윙(64): 누르는 길이가 중요한 모드
        if (g_cnt.dead && g_cnt.cpTick + (int)ref.size() <= T + 1) {      // 매크로 자체가 이 누름 근처에서 죽음
            g_cnt.msg = fmt::format("macro ends (dies) near tick {}", T); cntFinish(); return;
        }
        // 당기기 → 죽으면 멈춤, 클릭으로 돌아와 미루기 → 죽으면 멈춤. 누름만 옮기기 / 누름+뗌 같이 옮기기 중 큰 쪽
        if (!isPress && !holdMode) {                                     // 큐브·UFO·볼·거미에서 뗌은 타이밍이 의미 없어 세지 않음
            ++g_cnt.cur; return;
        }
        int be = 0, bl = 0; bool bp = false;
        for (int m = 0; m < 2; ++m) {
            bool pair = m == 1;
            if (pair && (!isPress || !holdMode || !(pi + 1 < (int)g_cnt.ev.size() && !g_cnt.ev[pi + 1].second))) break;
            int e = 0; while (e < Counter::MAXS && cntTrial(ref, refCompleted, pi, pair, -(e + 1))) ++e;
            int l = 0; while (l < Counter::MAXS && cntTrial(ref, refCompleted, pi, pair, l + 1)) ++l;
            if (m == 0 || e + l > be + bl) { be = e; bl = l; bp = pair; }
        }
        g_cnt.res.push_back({T, be, bl, bp, isPress});
        ++g_cnt.cur;
        cntSetLabel(fmt::format("FWC counting {}/{}", g_cnt.cur, g_cnt.ev.size()));
    }
    void startMusic() { if (g_cnt.busy) return; PlayLayer::startMusic(); }              // 세는 중에는 되돌릴 때마다 음악을 다시 맞추지 않음 (느려지는 원인)
    void prepareMusic(bool dontWait) { if (g_cnt.busy) return; PlayLayer::prepareMusic(dontWait); }
    void pauseGame(bool unfocused) {
        if (fwcBlockPause()) return;
        PlayLayer::pauseGame(unfocused);
    }
    void levelComplete() {
        if (g_cnt.busy) { g_cnt.completed = true; return; }      // 세는 중에는 클리어 화면을 띄우지 않음
        PlayLayer::levelComplete();
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (g_cnt.busy && object != m_anticheatSpike) { g_cnt.dead = true; return; }   // 세는 중: 죽음만 기록하고 연출은 생략
        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        g_mac.lastTick = -1;
        PlayLayer::resetLevel();
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

    void onQuit() {
        clearRings();
        g_data.reset();
        PlayLayer::onQuit();
    }
};

class $modify(FwcGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && !g_cnt.internal && !g_cnt.busy && m_player1) {
            if (g_rec.active) { static_cast<FwcPlayLayer*>(pl)->recWork(); return; }                       // 녹화 중: 게임을 직접 진행
            if (g_rec.want && !g_cnt.armed && (!g_cnt.on || g_cnt.done) && !m_player1->m_isDead && fwcTick(m_gameState.m_currentProgress) >= 8) {
                static_cast<FwcPlayLayer*>(pl)->recStart();
                return;
            }
        }
        if (pl && static_cast<GJBaseGameLayer*>(pl) == this && g_cnt.armed && !g_cnt.busy && !g_cnt.internal && !g_cnt.ev.empty()) {
            // 메뉴에서 시작: 결과를 비우고 지금 시작 지점에서 다시 시작한 뒤 셈
            g_cnt.armed = false; g_cnt.on = true; g_cnt.done = false; g_cnt.failed = false; g_cnt.msg.clear();
            g_cnt.calib.clear(); g_cnt.repair.clear(); g_cnt.repairs = 0;
            g_cnt.res.clear(); g_cnt.cur = 0; g_cnt.trials = 0; g_cnt.resets = 0; g_cnt.ticksRun = 0; g_cnt.msReset = 0; g_cnt.msStep = 0;
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
                if (it != inp.end()) for (bool down : it->second) this->handleButton(down, 1, true);
            }
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
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
                auto ring = makeRing(g_data->presses[i].window, pl1->getPosition());
                m_objectLayer->addChild(ring, 9999);
                g_rings.push_back(ring);
            }
        }
    }
};

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
class $modify(FwcPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (g_cnt.ev.empty() || g_cnt.busy) return;          // 매크로 재생이 꺼져 있거나 이미 세는 중이면 버튼 없음
        auto spr = ButtonSprite::create("FWC Count");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FwcPause::onFwcCount));
        auto menu = CCMenu::create();
        menu->addChild(btn);
        auto ws = CCDirector::get()->getWinSize();
        menu->setPosition({ws.width / 2.f, 22.f});
        this->addChild(menu, 100);
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
        { std::ifstream f(Mod::get()->getConfigDir() / "autorun.txt"); std::getline(f, name); }
        while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) name.pop_back();
        if (name.empty()) return true;
        auto arr = LocalLevelManager::get()->m_localLevels;
        if (!arr) return true;
        for (auto lvl : CCArrayExt<GJGameLevel*>(arr)) {
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
        if (!g_rec.active) { CCScheduler::update(dt); return; }
        for (int k = 0; k < REC_PER_FRAME && g_rec.active; ++k) CCScheduler::update(1.f / (float)g_rec.fps);
    }
};
