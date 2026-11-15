"""export_lib.py — 파이썬 엔진 레벨을 C++ 엔진 텍스트로 (파이프라인 export_level/json2txt 와 exe 가 같이 씀)"""
import json

def level_dict(P, L, start):
    """파이썬 엔진 레벨·시작 상태 → C++용 사전"""
    MODES = ["cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"]
    KINDS = ["solid", "hazard", "saw", "slope", "orb", "pad", "portal", "mode"]
    ORBA = ["yellow", "pink", "red", "blue", "green", "black", "dash", "gravity_dash", "spider"]
    PADA = ["yellow", "pink", "red", "blue", "spider"]
    objs = {}
    kinds = set()
    for c, lst in L.cells.items():
        for o in lst: objs[o.uid] = o; kinds.add(o.kind)
    for o in getattr(L, "movers", []): objs[o.uid] = o; kinds.add(o.kind)
    assert not any(o.kind == "slope" for o in getattr(L, "movers", [])), "움직이는 경사는 C++에서 아직 안 됨"
    assert kinds <= set(KINDS), kinds
    def arg(o):
        if o.kind == "orb": return ORBA.index(o.arg)
        if o.kind == "pad": return PADA.index(o.arg)
        if o.kind in ("mode", "portal"): return int(o.arg)
        return 0
    out = {"objs": [], "cells": {str(c): [o.uid for o in lst] for c, lst in L.cells.items()},
           "hblocks": {str(c): v for c, v in L.hblocks.items()}, "dblocks": {str(c): v for c, v in L.dblocks.items()},
           "teleports": [list(t) for t in L.teleports], "groups": {str(g): v for g, v in L.groups.items()},
           "grav_unlink": [list(t) for t in getattr(L, "grav_unlink", [])], "spider27_x": P.SPIDER27_X, "bands": [list(b) for b in P.DUAL_BAND_OVERRIDE], "off_by": {str(k): v for k, v in getattr(L, "off_by", {}).items()}, "sec": L.sec,
           "settings": dict(P.SETTINGS),
           "dsg": [P.DUAL_SHIP_G, P.DUAL_SHIP_G_X0], "dff": int(P.DUAL_FALL_FLIP), "free": sorted(getattr(L, "free_uids", [])), "su": int(P.SLOPE_UNDERSIDE), "ofa": int(P.ORB_FIRST_ALL), "dss": int(P.DUAL_SPAWN_SRC), "dbu": int(P.DUAL_BOUNDS_UNSNAP), "dsp": int(P.DUAL_SHARED_SPEED),
           "movers": [[o.uid, o.groups] for o in getattr(L, "movers", [])],
           "hmovers": [[i, list(r), gs] for i, r, gs in getattr(L, "hmovers", [])],
           "moves": [[m.g, m.tx, m.dx, m.dy, m.ticks, m.easing, m.rate, int(m.lock_x), m.mod_x, m.x_end] for m in (L.moves.all if getattr(L, "moves", None) else [])],
           "spd": [L.moves.spd.xs, L.moves.spd.v] if getattr(L, "moves", None) else [[], []],
           "gm": [L.gmods.xs, L.gmods.v] if getattr(L, "gmods", None) else [[], []],
           "flags": {k: getattr(P, k) for k in ["ROT_FROM_131", "SPIDER_ENTRY_HOLD", "TWIN_SPAWN_PUSH", "DUAL_OFF_FIRST", "DUAL_BOUNDS_PLAYER",
                     "MINI_WAVE_VEL2", "TP_BEFORE_UPDATE", "TP_PORTAL_DELAY", "PORTAL_REVERSE", "SHARED_USED", "BALL_ORB_FIRST", "DUAL_GRAV_LINK", "DUAL_EXIT_SWAP", "SPIDER_RAY_SOLID", "SPIDER27", "SPIDER_ORB_FIRST"]}}
    for uid in sorted(objs):
        o = objs[uid]
        out["objs"].append({"uid": o.uid, "id": o.id, "kind": KINDS.index(o.kind), "arg": arg(o), "x": o.x, "y": o.y, "w": o.w, "h": o.h,
            "minx": o.minx, "maxx": o.maxx, "miny": o.miny, "maxy": o.maxy, "rot": o.rot, "flip_x": bool(o.flip_x),
            "upside": bool(getattr(o, "upside", False)), "orient": o.orient, "hazard_slope": bool(o.hazard_slope), "radius": o.radius,
            "corners": o.corners, "tp_y": getattr(o, "tp_y", 0.0) if o.id == 747 else 0.0})
    assert not P.DASH_OVERRIDE and not P.ORB_OVERRIDE
    def pl(p):
        d = {}
        for k in P.Player.__slots__:
            v = getattr(p, k)
            if k == "mode": v = MODES.index(v)
            elif k in ("snap_obj", "slope"): v = v.uid if v is not None else -1
            elif k == "used": v = sorted(v)
            elif k == "dual": v = pl(v) if v else None
            elif k == "ceiling": v = v if v is not None else None
            d[k] = v
        return d

    out["start"] = pl(start)
    if getattr(L, "strig", False):                          # 상태 트리거 모드
        out["strig"] = {"dyn": sorted(L.dyn_groups),
                        "trigs": [[i, x, y, how, multi, gs, sorted(kv.items()), sorted(ls.items())] for i, x, y, how, multi, gs, kv, ls in L.trigs],
                        "anc": [[g, a[0], a[1], a[2]] for g, a in sorted(L.anchors.items())], "rev": sorted(getattr(L, "rev_uids", []))}
    return out


def dict_to_txt(d, path):
    """레벨 사전 → C++가 읽는 텍스트 (숫자는 repr로 정확히)"""
    o = open(path, "w")
    R = repr
    def b(v): return "1" if v else "0"
    o.write("SET %s %s\n" % (b(d["settings"]["fix_gravity_bug"]), b(d["settings"]["fix_radius"])))
    f = d["flags"]
    o.write("FLAGS " + " ".join(str(int(f[k])) for k in ["ROT_FROM_131", "SPIDER_ENTRY_HOLD", "TWIN_SPAWN_PUSH", "DUAL_OFF_FIRST", "DUAL_BOUNDS_PLAYER",
            "MINI_WAVE_VEL2", "TP_BEFORE_UPDATE", "TP_PORTAL_DELAY", "PORTAL_REVERSE", "SHARED_USED", "BALL_ORB_FIRST", "DUAL_GRAV_LINK", "DUAL_EXIT_SWAP", "SPIDER_RAY_SOLID", "SPIDER27", "SPIDER_ORB_FIRST"]) + "\n")
    o.write("SEC %s\n" % R(float(d["sec"])))
    for ob in d["objs"]:
        cs = ob["corners"] or []
        o.write("OBJ %d %d %d %d %s %s %s %s %s %s %s %s %s %s %s %d %s %s %s %d %s\n" % (ob["uid"], ob["id"], ob["kind"], ob["arg"],
            R(float(ob["x"])), R(float(ob["y"])), R(float(ob["w"])), R(float(ob["h"])), R(float(ob["minx"])), R(float(ob["maxx"])), R(float(ob["miny"])), R(float(ob["maxy"])),
            R(float(ob["rot"])), b(ob["flip_x"]), b(ob["upside"]), ob["orient"], b(ob["hazard_slope"]), R(float(ob["radius"])), R(float(ob["tp_y"])),
            len(cs), " ".join(R(float(v)) for p in cs for v in p)))
    for c, l in d["cells"].items(): o.write("CELL %s %d %s\n" % (c, len(l), " ".join(map(str, l))))
    for tag in ("hblocks", "dblocks"):
        for c, l in d[tag].items(): o.write("%s %s %d %s\n" % ("HB" if tag == "hblocks" else "DB", c, len(l), " ".join(R(float(v)) for r in l for v in r)))
    st = d.get("strig")
    if st:
        o.write("STRIG 1\n")
        o.write("DYN %d %s\n" % (len(st["dyn"]), " ".join(map(str, st["dyn"]))))
        for k, (i, x, y, how, multi, gs, kv, ls) in enumerate(st["trigs"]):
            o.write("TRG %d %d %s %s %d %d %d %s %d %s\n" % (k, i, R(float(x)), R(float(y)), how, multi, len(gs), " ".join(map(str, gs)),
                    len(kv), " ".join("%d %s" % (kk, R(float(vv))) for kk, vv in kv)))
            for kk, vv in ls: o.write("TRGL %d %d %d %s\n" % (k, kk, len(vv), " ".join(R(float(v)) for v in vv)))
        for u in st.get("rev", []): o.write("REV %d\n" % u)
        for g, x, y, gs in st["anc"]:
            o.write("ANC %d %s %s %d %s\n" % (g, R(float(x)), R(float(y)), len(gs), " ".join(map(str, gs))))
    for tx, g, k in ([] if st else d["teleports"]): o.write("TP %s %d %d\n" % (R(float(tx)), g, k))   # 상태 모드: 텔레포트도 트리거로
    for g, (x, y) in d["groups"].items(): o.write("GROUP %s %s %s\n" % (g, R(float(x)), R(float(y))))
    for tx, v in d["grav_unlink"]: o.write("GU %s %d\n" % (R(float(tx)), v))
    for bd in d.get("bands", []): o.write("BAND %r %r %r %r\n" % tuple(bd))
    if "spider27_x" in d: o.write("S27X %r\n" % d["spider27_x"])
    for k, v in d.get("off_by", {}).items(): o.write("OFF %s %d\n" % (k, v))
    if "dsg" in d: o.write("DSG %r %r\n" % (float(d["dsg"][0]), float(d["dsg"][1])))
    if "dff" in d: o.write("DFF %d\n" % d["dff"])
    if "su" in d: o.write("SU %d\n" % d["su"])
    if "ofa" in d: o.write("OFA %d\n" % d["ofa"])
    if "dsp" in d: o.write("DSP %d\n" % d["dsp"])
    if "dbu" in d: o.write("DBU %d\n" % d["dbu"])
    if "dss" in d: o.write("DSS %d\n" % d["dss"])
    for u in d.get("free", []): o.write("FREE %d\n" % u)
    for g, tx, dx, dy, tk, ez, rate, lk, mx, xe in sorted(d.get("moves", []), key=lambda m: (m[0], m[1])):
        o.write("MV %d %r %r %r %r %d %r %d %r %r\n" % (g, float(tx), float(dx), float(dy), float(tk), ez, float(rate), lk, float(mx), float(xe)))
    for x, v in zip(*d.get("spd", [[], []])): o.write("SPD %r %r\n" % (float(x), float(v)))
    for x, v in zip(*d.get("gm", [[], []])): o.write("GM %r %r\n" % (float(x), float(v)))
    for u, gs in d.get("movers", []): o.write("MOVER %d %d %s\n" % (u, len(gs), " ".join(map(str, gs))))
    for i, r, gs in d.get("hmovers", []): o.write("HM %d %r %r %r %r %d %s\n" % (i, float(r[0]), float(r[1]), float(r[2]), float(r[3]), len(gs), " ".join(map(str, gs))))
    KEYS = ["x", "y", "last_x", "last_y", "y_vel", "x_vel", "speed", "y_start", "gravity", "mini", "upside_down", "holding_jump", "jump_unused",
            "can_jump", "on_ground", "is_jumping", "holding_release_delayed", "can_ring_jump", "touched_ring", "is_dashing", "dash_y",
            "velocity_uncapped", "last_flip_tick", "tick", "snap_obj", "snap_distance", "is_on_slope", "was_on_slope", "slope", "slope_velocity",
            "slope_start_tick", "dead", "half", "mode", "robot_boost_charge", "robot_boost_invalidated", "floor", "ceiling", "spider_hold", "dual_y_ref", "tp_wait"]
    def pl(tag, p):
        vals = []
        for k in KEYS:
            v = p[k]
            if k in ("ceiling", "dual_y_ref"): vals.append("nan" if v is None else R(float(v)))
            elif isinstance(v, bool): vals.append(b(v))
            elif isinstance(v, int): vals.append(str(v))
            else: vals.append(R(float(v)))
        o.write("%s %s %d %s\n" % (tag, " ".join(vals), len(p["used"]), " ".join(map(str, p["used"]))))
    pl("MAIN", d["start"])
    if d["start"]["dual"]: pl("TWIN", d["start"]["dual"])

    o.close()
