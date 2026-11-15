"""gdlevel.py — 레벨 읽기 (내가 짠 코드). 게임 파일은 읽기만 하고 절대 쓰지 않음.

받는 것:
  - 레벨 문자열 텍스트 (.txt: "kS38,...;1,1,2,15,3,15;...")
  - .gmd (GDShare 등으로 내보낸 레벨, plist XML. k2 = 이름, k4 = 레벨 데이터)
  - CCLocalLevels.dat (게임 저장 파일, %LOCALAPPDATA%\\GeometryDash) → 만든 레벨 목록에서 고름
풀기:
  저장 파일: 바이트마다 XOR 11 → URL-safe base64 → gzip → plist XML
  k4 레벨 데이터: URL-safe base64 → gzip (이미 풀린 문자열이면 그대로)
"""
import base64, gzip, zlib, re, os
import xml.etree.ElementTree as ET


def _b64(s):
    s = s.strip().replace("\0", "")
    s += "=" * (-len(s) % 4)
    return base64.urlsafe_b64decode(s)


def _unzip(b):
    try: return gzip.decompress(b)
    except Exception:
        return zlib.decompress(b[10:], -15)          # 헤더 10바이트 빼고 날 deflate


def decode_level_data(k4):
    """k4 → 레벨 문자열"""
    k4 = k4.strip()
    if k4.startswith("kS") or k4.startswith("kA") or ";1," in k4[:5000]:
        return k4
    return _unzip(_b64(k4)).decode("utf-8", "replace")


def _plist_dicts(xml_text):
    """GD plist (d/k/s/i/t/r 짧은 태그)에서 k2·k4가 있는 사전들을 찾음 → [(이름, k4)]"""
    root = ET.fromstring(xml_text)
    out = []
    for d in root.iter("d"):
        kids = list(d); vals = {}
        for i in range(0, len(kids) - 1, 2):
            if kids[i].tag == "k": vals[kids[i].text] = kids[i + 1].text or ""
        if "k4" in vals and vals["k4"]:
            out.append((vals.get("k2", "(이름 없음)"), vals["k4"]))
    # 맨 위가 d가 아니라 dict인 .gmd도 있음
    if not out:
        for d in root.iter("dict"):
            kids = list(d); vals = {}
            for i in range(0, len(kids) - 1, 2):
                if kids[i].tag in ("k", "key"): vals[kids[i].text] = kids[i + 1].text or ""
            if "k4" in vals and vals["k4"]:
                out.append((vals.get("k2", "(이름 없음)"), vals["k4"]))
    return out


def decrypt_save(path):
    b = open(path, "rb").read()
    if b.lstrip().startswith(b"<?xml"):
        return b.decode("utf-8", "replace")          # 맥 등 암호화 안 된 저장 파일
    x = bytes(c ^ 11 for c in b)
    return _unzip(_b64(x.decode("ascii", "ignore"))).decode("utf-8", "replace")


def list_levels(path):
    """파일 → [(이름, 레벨 문자열)]"""
    low = path.lower()
    if low.endswith(".dat"):
        return [(n, decode_level_data(k)) for n, k in _plist_dicts(decrypt_save(path))]
    raw = open(path, "rb").read().decode("utf-8", "replace")
    if low.endswith(".gmd") or raw.lstrip().startswith("<?xml") or raw.lstrip().startswith("<d>") or raw.lstrip().startswith("<plist"):
        txt = raw if raw.lstrip().startswith("<?xml") or raw.lstrip().startswith("<plist") else "<plist>" + raw + "</plist>"
        return [(n, decode_level_data(k)) for n, k in _plist_dicts(txt)]
    name = os.path.splitext(os.path.basename(path))[0]
    return [(name, decode_level_data(raw))]


def parse_objects(level_string):
    """레벨 문자열 → (헤더 문자열, 오브젝트 목록[dict])  (gdwave2.load_level과 같은 규칙)"""
    parts = level_string.split(";")
    objs = []
    for p in parts[1:]:
        f = p.split(","); o = dict(zip(f[0::2], f[1::2]))
        if "1" in o and "2" in o and "3" in o:
            try: int(o["1"]); float(o["2"]); float(o["3"])
            except ValueError: continue
            objs.append(o)
    return parts[0], objs


def header_start(header):
    """레벨 헤더 → 시작 상태 (모드, 속도 번호, 미니, 듀얼, 뒤집힘). 키 뜻은 gddocs 기준 (추정 포함)
    kA2 시작 모드(0 큐브 1 비행기 2 볼 3 UFO 4 웨이브 5 로봇 6 스파이더 7 스윙), kA3 미니, kA4 속도(0=1배 1=0.5배 2=2배 3=3배 4=4배),
    kA8 듀얼, kA11 뒤집힌 중력"""
    f = header.split(","); h = dict(zip(f[0::2], f[1::2]))
    modes = ["cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"]
    def gi(k, d=0):
        try: return int(float(h.get(k, d)))
        except ValueError: return d
    m = gi("kA2"); sp = gi("kA4")
    return {"mode": modes[m] if 0 <= m < len(modes) else "cube", "speed": {0: 1, 1: 0, 2: 2, 3: 3, 4: 4}.get(sp, 1),
            "mini": gi("kA3") == 1, "dual": gi("kA8") == 1, "flip": gi("kA11") == 1}


# ---- 시험용
def encode_level_data(s):
    return base64.urlsafe_b64encode(gzip.compress(s.encode("utf-8"))).decode()

def make_save(levels):
    """[(이름, 레벨 문자열)] → 저장 파일 바이트 (시험용)"""
    inner = "".join("<k>k_%d</k><d><k>kCEK</k><i>4</i><k>k2</k><s>%s</s><k>k4</k><s>%s</s></d>" % (i, n, encode_level_data(s)) for i, (n, s) in enumerate(levels))
    xml = '<?xml version="1.0"?><plist version="1.0" gjver="2.0"><dict><k>LLM_01</k><d><k>_isArr</k><t />%s</d><k>LLM_02</k><i>35</i></dict></plist>' % inner
    return bytes(c ^ 11 for c in base64.urlsafe_b64encode(gzip.compress(xml.encode())))
