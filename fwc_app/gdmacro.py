"""gdmacro.py — 지메 매크로 파일 읽기 (내가 짠 코드, 형식은 maxnut/GDReplayFormat 문서·헤더를 보고 맞춤)

지원:
  - GDR1 (xdBot .gdr / .gdr.json): JSON 또는 msgpack. inputs = [{frame, btn, 2p, down}], framerate(없으면 240)
  - GDR2 (이클립스 .gdr2 등): 바이너리. 맨 앞 3바이트 "GDR"
      정수·bool = LEB128 varint, float·double = 빅엔디언 원래 바이트, 문자열 = varint 길이 + 바이트
      순서: 버전, 입력태그, 작성자, 설명, duration(float), 게임버전, framerate(double), seed, coins, ldm, platformer,
            봇이름, 봇버전, 레벨ID, 레벨이름, 확장 크기+데이터, 죽음 수+죽음 프레임(차이로 저장), 입력 수, P1 입력 수, 입력들
      입력 1개 = varint packed (+ 입력태그가 있으면 확장 크기+데이터)
        플랫포머 아님: delta = packed >> 1, down = packed & 1, 버튼 = 1(점프)
        플랫포머:     delta = packed >> 3, 버튼 = (packed >> 1) & 3, down = packed & 1
      P1 입력이 먼저 다 나오고 그다음 P2 (P2는 프레임 차이가 0부터 다시)
결과: Macro(tps, inputs=[(frame, down, player2, button)], ...)
"""
import json, struct


class Macro:
    def __init__(self):
        self.fmt = ""; self.tps = 240.0; self.inputs = []; self.bot = ""; self.level_name = ""; self.level_id = 0
        self.author = ""; self.platformer = False; self.game_version = 0; self.deaths = []

    def held_by_tick(self, n=None, player2=False, offset=0, merge=True):
        """틱마다 점프 버튼을 누르고 있나 (True/False 목록). offset = 프레임을 몇 틱 밀지 (맞춤용)
        merge = 1인용 레벨: 플레이어 1·2 버튼이 같은 플레이어를 움직이므로 두 쪽 입력을 시간 순서대로 합침 (나중 입력이 이김, 추정)"""
        # 같은 틱 안의 입력은 매크로에 적힌 순서 그대로 (틱 번호로만 정렬. 예전엔 누름·뗌 순서가 뒤바뀌어 "누른 채"로 잘못 읽힘)
        ev = sorted(((f + offset, d) for f, d, p2, b in self.inputs if (merge or p2 == player2) and b == 1), key=lambda e: e[0])
        last = ev[-1][0] if ev else 0
        n = n if n is not None else last + 1
        out = []; held = False; j = 0
        for t in range(n):
            tapped = False
            while j < len(ev) and ev[j][0] <= t:
                if ev[j][1] and not held: tapped = True
                held = ev[j][1]; j += 1
            # 한 틱 안에 눌렀다 뗀 것(톡 치기)은 그 틱 동안만 누른 걸로 (안 그러면 누름 자체가 사라짐)
            out.append(held or tapped)
        return out


class _Reader:
    def __init__(self, b): self.b = b; self.i = 0
    def empty(self): return self.i >= len(self.b)
    def varint(self):
        r = 0; s = 0
        while True:
            if self.i >= len(self.b): raise ValueError("파일이 중간에 끝남 (varint)")
            c = self.b[self.i]; self.i += 1
            r |= (c & 0x7F) << s; s += 7
            if not c & 0x80: return r
    def string(self):
        n = self.varint()
        if self.i + n > len(self.b): raise ValueError("파일이 중간에 끝남 (문자열)")
        s = self.b[self.i:self.i + n].decode("utf-8", "replace"); self.i += n; return s
    def f32(self):
        v = struct.unpack(">f", self.b[self.i:self.i + 4])[0]; self.i += 4; return v
    def f64(self):
        v = struct.unpack(">d", self.b[self.i:self.i + 8])[0]; self.i += 8; return v
    def skip(self, n):
        if self.i + n > len(self.b): raise ValueError("파일이 중간에 끝남 (확장)")
        self.i += n


def _gdr2(b):
    r = _Reader(b); r.skip(3)
    m = Macro(); m.fmt = "GDR2"
    version = r.varint(); tag = r.string()
    m.author = r.string(); r.string()                  # 설명
    r.f32()                                            # duration
    m.game_version = r.varint(); m.tps = r.f64()
    r.varint(); r.varint()                             # seed, coins
    r.varint(); m.platformer = bool(r.varint())        # ldm, platformer
    m.bot = r.string(); r.varint(); m.level_id = r.varint(); m.level_name = r.string()
    r.skip(r.varint())                                 # 확장
    p = 0
    for _ in range(r.varint()):
        p += r.varint(); m.deaths.append(p)
    r.varint()                                         # 입력 수
    p1 = r.varint(); p = 0
    while not r.empty():
        packed = r.varint()
        if m.platformer: delta, btn, down = packed >> 3, (packed >> 1) & 3, bool(packed & 1)
        else: delta, btn, down = packed >> 1, 1, bool(packed & 1)
        if tag: r.skip(r.varint())
        f = p + delta
        m.inputs.append((f, down, p1 == 0, btn)); p = f
        if p1 > 0:
            p1 -= 1
            if p1 == 0: p = 0
    m.inputs.sort(key=lambda a: a[0])
    return m


def _gdr1(d):
    m = Macro(); m.fmt = "GDR1"
    m.tps = float(d.get("framerate", 240.0) or 240.0)
    m.bot = str((d.get("bot") or {}).get("name", "")); m.author = str(d.get("author", ""))
    lv = d.get("level") or {}; m.level_name = str(lv.get("name", "")); m.level_id = int(lv.get("id", 0) or 0)
    for x in d.get("inputs", []):
        m.inputs.append((int(x["frame"]), bool(x["down"]), bool(x.get("2p", False)), int(x.get("btn", 1))))
    m.inputs.sort(key=lambda a: a[0])
    return m


def load(path):
    b = open(path, "rb").read()
    if b[:3] == b"GDR": return _gdr2(b)
    try:
        return _gdr1(json.loads(b.decode("utf-8")))
    except (UnicodeDecodeError, json.JSONDecodeError):
        pass
    try:
        import msgpack
    except ImportError:
        raise ValueError("msgpack 형식 GDR1 파일인데 msgpack 모듈이 없음")
    return _gdr1(msgpack.unpackb(b, raw=False, strict_map_key=False))


# ---- 시험용: GDR2 쓰기 (문서의 형식 그대로)
def _vi(v):
    out = bytearray()
    while True:
        c = v & 0x7F; v >>= 7
        if v: out.append(c | 0x80)
        else: out.append(c); return bytes(out)

def _st(s):
    b = s.encode("utf-8"); return _vi(len(b)) + b

def write_gdr2(path, events, tps=240.0, level_name="", bot="fwc-test"):
    """events = [(frame, down)] (P1 점프만)"""
    b = bytearray(b"GDR") + _vi(2) + _st("") + _st("") + _st("") + struct.pack(">f", 0.0) + _vi(22081) + struct.pack(">d", tps)
    b += _vi(0) + _vi(0) + _vi(0) + _vi(0) + _st(bot) + _vi(1) + _vi(0) + _st(level_name) + _vi(0) + _vi(0)
    b += _vi(len(events)) + _vi(len(events))
    p = 0
    for f, d in events:
        b += _vi(((f - p) << 1) | (1 if d else 0)); p = f
    open(path, "wb").write(bytes(b))


if __name__ == "__main__":
    import sys
    m = load(sys.argv[1])
    print(m.fmt, "tps", m.tps, "봇", m.bot, "레벨", m.level_name, "입력", len(m.inputs), "플랫포머", m.platformer)
