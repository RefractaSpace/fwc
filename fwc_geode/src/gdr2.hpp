// 이클립스 .gdr2 매크로 읽기 (GDR2 형식). 게임·지오드와 무관한 순수 C++ 이라 따로 시험할 수 있다.
//   결과: (틱, 누름?) 목록 — 1번 플레이어의 점프 버튼만. fwc_app/gdmacro.py 의 _gdr2 와 같은 순서로 읽는다.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>

struct FwcGdr2Info {
    double tps = 240.0;        // 1초당 틱 수 (240 이 아니면 이 모드에서 못 씀)
    bool platformer = false;
    int p2Inputs = 0;          // 버린 2번 플레이어 입력 수
    int otherButtons = 0;      // 버린 좌우 버튼 입력 수 (플랫포머)
    std::string levelName;
};

inline bool fwcReadGdr2(const std::string& b, std::vector<std::pair<int, bool>>& out, FwcGdr2Info& info, std::string& err) {
    size_t i = 0; bool bad = false;
    auto need = [&](size_t n) { if (i + n > b.size()) { bad = true; return false; } return true; };
    auto varint = [&]() -> uint64_t {
        uint64_t v = 0; int shift = 0;
        while (true) {
            if (!need(1) || shift > 63) { bad = true; return 0; }
            uint8_t c = (uint8_t)b[i++];
            v |= (uint64_t)(c & 0x7F) << shift;
            if (!(c & 0x80)) return v;
            shift += 7;
        }
    };
    auto skip = [&](uint64_t n) { if (n > b.size() || !need((size_t)n)) { bad = true; return; } i += (size_t)n; };
    auto str = [&]() -> std::string { uint64_t n = varint(); if (bad || n > b.size() || !need((size_t)n)) { bad = true; return {}; } std::string s = b.substr(i, (size_t)n); i += (size_t)n; return s; };
    auto f64 = [&]() -> double {            // 큰 끝(big-endian) 8바이트 실수
        if (!need(8)) return 0.0;
        uint64_t u = 0; for (int k = 0; k < 8; ++k) u = (u << 8) | (uint8_t)b[i + k];
        i += 8; double d; std::memcpy(&d, &u, 8); return d;
    };

    out.clear(); info = FwcGdr2Info{};
    if (b.size() < 3 || b.compare(0, 3, "GDR") != 0) { err = "not a GDR2 file"; return false; }
    i = 3;
    varint();                                // 형식 버전
    std::string tag = str();                 // 입력 확장 이름 (있으면 입력마다 확장 바이트가 붙음)
    str(); str();                            // 만든 사람, 설명
    skip(4);                                 // 길이(초, float)
    varint();                                // 게임 버전
    info.tps = f64();
    varint(); varint();                      // 시드, 코인
    varint();                                // LDM
    info.platformer = varint() != 0;
    str();                                   // 봇 이름
    varint(); varint();                      // 봇 버전, 레벨 ID
    info.levelName = str();
    skip(varint());                          // 머리 확장
    for (uint64_t n = varint(), k = 0; k < n && !bad; ++k) varint();   // 죽은 틱들
    varint();                                // 전체 입력 수
    uint64_t p1 = varint();                  // 1번 플레이어 입력 수 (그 뒤는 2번 플레이어, 틱은 0부터 다시)
    if (bad) { err = "file ends early (header)"; return false; }
    uint64_t p = 0;
    while (i < b.size()) {
        uint64_t packed = varint();
        uint64_t delta; int btn; bool down = (packed & 1) != 0;
        if (info.platformer) { delta = packed >> 3; btn = (int)((packed >> 1) & 3); }
        else { delta = packed >> 1; btn = 1; }
        if (!tag.empty()) skip(varint());
        if (bad) { err = "file ends early (inputs)"; return false; }
        uint64_t f = p + delta;
        bool isP2 = (p1 == 0);
        if (isP2) ++info.p2Inputs;
        else if (btn != 1) ++info.otherButtons;
        else out.emplace_back((int)f, down);
        p = f;
        if (p1 > 0) { --p1; if (p1 == 0) p = 0; }
    }
    std::stable_sort(out.begin(), out.end(), [](auto const& a, auto const& c) { return a.first < c.first; });
    return true;
}
