"""verify_main.py — 메인 레벨 매크로로 엔진 검증 (읽기만 함. 게임 파일은 절대 안 바꿈)

쓰는 법:
  python verify_main.py <LevelData.plist 경로> <매크로 폴더> [결과.csv]

하는 일: 매크로마다 1번씩 재생해서
  - 엔진에서 끝까지 가는지 / 어디서 죽는지
  - 윈도우 개수, σ/s
  - 매크로에 실제 게임 위치 기록(frameFixes)이 있으면, 엔진 위치와 틱마다 비교 (y 차이)
"""
import sys, os, glob, json, csv
import xml.etree.ElementTree as ET
import gdlevel, gdmacro, analyze


def load_main_levels(path):
    """LevelData.plist → {레벨 번호: 레벨 문자열}. 키 = 레벨 번호, 값 = 압축된 레벨 데이터 (추정: k4와 같은 방식).
    긴 태그(dict/key/string)와 짧은 태그(d/k/s) 둘 다 받음. 레벨로 안 풀리는 값은 건너뜀."""
    raw = open(path, "rb").read().decode("utf-8", "replace")
    root = ET.fromstring(raw)
    out = {}
    for d in list(root.iter("dict")) + list(root.iter("d")):
        kids = list(d)
        for i in range(0, len(kids) - 1, 2):
            if kids[i].tag not in ("k", "key") or kids[i + 1].tag not in ("s", "string"): continue
            key = (kids[i].text or "").strip(); val = kids[i + 1].text or ""
            if not key.isdigit() or len(val) < 100: continue
            s = None
            # 공식 레벨은 압축 문자열 맨 앞 13글자("H4sIAAAAAAAAA")가 빠진 채로 들어 있음 (boomlings.dev 문서). 그대로 → 붙여서 순서로 시도
            for cand in (val, "H4sIAAAAAAAAA" + val.strip()):
                try: t = gdlevel.decode_level_data(cand)
                except Exception: continue
                # 앞부분 없이도 우연히 몇 글자로 풀리는 경우가 있어서, 레벨처럼 생겼는지(물체 구분 ';'가 여러 개) 확인
                if t.count(";") >= 2 and "," in t: s = t; break
            if s is not None: out[int(key)] = s
    return out


Y_SHIFT = 90.0   # 엔진은 바닥을 y=0으로, 게임 기록은 바닥을 y=90으로 셈 (21개 매크로 모두 y차 중앙값이 정확히 90.00으로 나와서 확인)


def compare_positions(track, fixes):
    """엔진 위치(track[틱] = (x, y, ...))와 실제 게임 기록(frameFixes)을 비교.
    틱 번호가 1~2틱 어긋날 수 있어서 -2~+2틱 중 y 차이 중앙값이 가장 작은 쪽을 고름.
    → (비교한 틱 수, 고른 어긋남, y차 중앙값, y차 최대, y차가 처음 10을 넘는 틱 또는 None)"""
    pts = [(f["frame"], f["p1"]["x"], f["p1"]["y"]) for f in fixes if "p1" in f and "x" in f["p1"] and "y" in f["p1"]]
    if not pts or not track: return None
    best = None
    for sh in range(-2, 3):
        dy = []
        for fr, x, y in pts:
            t = fr + sh
            if 0 <= t < len(track): dy.append((fr, abs(track[t][1] + Y_SHIFT - y)))
        if not dy: continue
        vals = sorted(v for _, v in dy)
        med = vals[len(vals) // 2]
        if best is None or med < best[2]:
            first = next((fr for fr, v in dy if v > 10.0), None)
            best = (len(dy), sh, med, vals[-1], first)
    return best


def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    levels = load_main_levels(sys.argv[1])
    print("메인 레벨 %d개 읽음: 번호 %s" % (len(levels), sorted(levels)))
    files = sorted(glob.glob(os.path.join(sys.argv[2], "*.gdr.json")) + glob.glob(os.path.join(sys.argv[2], "*.gdr")) + glob.glob(os.path.join(sys.argv[2], "*.gdr2")))
    rows = []
    for f in files:
        name = os.path.basename(f)
        try:
            m = gdmacro.load(f)
        except Exception as e:
            rows.append([name, "", "매크로 못 읽음: %s" % e] + [""] * 9); continue
        lid = int(m.level_id or 0)
        if lid not in levels:
            rows.append([name, lid, "레벨 데이터 없음"] + [""] * 9); print("%-34s 레벨 %s: 레벨 데이터 없음" % (name, lid)); continue
        try:
            r = analyze.analyze(levels[lid], m, log=lambda s: None)
        except Exception as e:
            rows.append([name, lid, "계산 오류: %s" % str(e)[:120]] + [""] * 9); print("%-34s 계산 오류: %s" % (name, str(e)[:120])); continue
        fixes = []
        if f.endswith(".json"):
            try: fixes = json.load(open(f, encoding="utf-8")).get("frameFixes", [])
            except Exception: fixes = []
        cmpres = compare_positions(getattr(r, "track_full", r.track), fixes)   # 죽는 순간까지 비교 (r.track은 죽기 1초 전에서 잘림)
        died = bool(getattr(r, "died", False))
        status = "죽음" if died else "끝까지 감"
        if not died and cmpres and fixes:                                # 안 죽었어도 마지막 1초 높이가 실제 기록과 크게 다르면 통과가 아님 (엔진이 맵 밖으로 날아간 경우)
            tr_ = getattr(r, "track_full", r.track); fx_ = {p_["frame"]: p_["p1"] for p_ in fixes if "y" in p_.get("p1", {})}
            tail = [abs(tr_[t][1] - (fx_[t]["y"] - Y_SHIFT)) for t in range(max(0, len(tr_) - 240), len(tr_)) if t in fx_]
            if tail and sorted(tail)[len(tail) // 2] > 150.0: status = "길 벗어남"
        dt = "%.2f" % (r.death_tick / 240.0) if died else ""
        dx = "%.0f" % r.death_x if died else ""
        cause = getattr(r, "death_cause", "") if died else ""
        row = [name, lid, status, dt, dx, cause, r.n, len(r.windows), "%.2f" % r.precision, r.offset]
        row += (["%d" % cmpres[0], "%.2f" % cmpres[2], "%.1f" % cmpres[3], "" if cmpres[4] is None else "%.2f" % (cmpres[4] / 240.0)] if cmpres else ["", "", "", ""])
        rows.append(row)
        print("%-34s %-9s %6s초  윈도우 %4d  σ/s %7.2f  y차 중앙 %s 최대 %s 처음 벌어짐 %s초" % (name, status, dt or "-", len(r.windows), r.precision, row[11] or "-", row[12] or "-", row[13] or "-"))
    head = ["매크로", "레벨 번호", "결과", "죽은 시각(초)", "죽은 x", "죽은 이유", "재생 틱", "윈도우 수", "σ/s", "맞춤 틱", "위치 비교 틱 수", "y차 중앙값", "y차 최대", "y차 10 넘는 첫 시각(초)"]
    out = sys.argv[3] if len(sys.argv) > 3 else "verify_main_result.csv"
    with open(out, "w", newline="", encoding="utf-8-sig") as fh:
        w = csv.writer(fh); w.writerow(head)
        for r_ in rows: w.writerow(list(r_) + [""] * (len(head) - len(r_)))
    ok = sum(1 for r_ in rows if len(r_) > 2 and r_[2] == "끝까지 감")
    print("\n끝까지 감 %d / 전체 %d  → %s" % (ok, len(rows), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
