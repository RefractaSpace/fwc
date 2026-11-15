"""compare_test.py — 리눅스 정답과 윈도우 결과 비교 (윈도우 수학 함수가 달라 결과가 바뀌는지 확인)"""
import json, sys
a = json.load(open(sys.argv[1], encoding="utf-8")); b = json.load(open(sys.argv[2], encoding="utf-8"))
wa = [(w["tick"], w["window"]) for w in a["windows"]]; wb = [(w["tick"], w["window"]) for w in b["windows"]]
same = sum(1 for x, y in zip(wa, wb) if x == y)
print("윈도우 %d개 중 같음 %d개 | σ/s 리눅스 %.4f, 윈도우 %.4f | 재생 틱 %d / %d" % (len(wa), same, a["precision"], b["precision"], a["ticks"], b["ticks"]))
print("완전히 같음" if wa == wb and a["ticks"] == b["ticks"] else "다름 — 이 결과를 Claude에게 알려 주세요")
