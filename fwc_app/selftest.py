import sys, json, time
import gdlevel, gdmacro, analyze
lv = gdlevel.list_levels(sys.argv[1])[-1][1]
m = gdmacro.load(sys.argv[2])
r = analyze.analyze(lv, m, sac_patch=(sys.argv[3] == "sac"))
print("경고:", r.warnings)
print("칸별:", r.counts, "σ/s %.2f" % r.precision)
analyze.save_json(r, "/tmp/selftest_%s.json" % sys.argv[3])
