"""overlay.py — 계산 결과 → OBS에 덮어씌우는 투명 영상 (왼쪽 위 칸별 개수, 왼쪽 아래 진행률·σ/s)
라우치카머 영상 배치를 참고한 화면 (그대로 베끼지 않음). 글꼴: Pusab (Flat-it 프리웨어, fonts/Pusab.otf)
영상 시작 = 매크로 0프레임(레벨 시작). 녹화 영상과 맞출 때는 OBS에서 시작 시점을 맞춤.
인코더: VP9 + 투명(.webm, 작음). fast=True 또는 VP9가 없으면 QuickTime Animation(.mov, 투명, 빠르지만 큼).
빠르게 한 것 (결과 그림은 예전과 같음):
  - 글자 그림을 한 번만 그려 두고 붙이기만 함 (같은 글자·색·크기는 다시 안 그림)
  - 영상을 조각(CPU 수만큼)으로 나눠 ffmpeg 여러 개가 동시에 인코딩한 뒤 다시 인코딩 없이 이어 붙임"""
import os, subprocess, threading
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
COLORS = {"17+": (255, 107, 230), "13+": (197, 140, 255), "9+": (127, 124, 255), "7-8": (108, 198, 255), "5-6": (107, 227, 107),
          "4": (255, 255, 255), "3": (255, 227, 77), "2": (255, 171, 61), "1": (255, 77, 77)}
ORDER = ["17+", "13+", "9+", "7-8", "5-6", "4", "3", "2", "1"]


def _ffmpeg():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return "ffmpeg"


def has_vp9(ff):
    try:
        out = subprocess.run([ff, "-hide_banner", "-encoders"], capture_output=True, text=True, **_nowin()).stdout
        return "libvpx-vp9" in out
    except Exception:
        return False


def _nowin():
    return {"creationflags": 0x08000000} if os.name == "nt" else {}


class _Text:
    """문자열 하나를 테두리 포함해 한 번만 그려 두고 (RGBA 그림, 기준점) 으로 돌려줌. 같은 문자열은 다시 안 그림"""
    def __init__(self, font_path):
        self.font_path = font_path; self.fonts = {}; self.cache = {}

    def font(self, sz):
        if sz not in self.fonts: self.fonts[sz] = ImageFont.truetype(self.font_path, sz)
        return self.fonts[sz]

    def get(self, s, col, sz):
        k = (s, col, sz)
        if k not in self.cache:
            f = self.font(sz); sw = max(2, sz // 7)
            # 기준점(anchor "ls" = 왼쪽, 글자 바닥선)을 그림 안 (pad, pad+sz) 에 둠
            pad = sw + 4; W = int(f.getlength(s)) + 2 * pad + sz; H = 2 * sz + 2 * pad
            im = Image.new("RGBA", (W, H), (0, 0, 0, 0)); dr = ImageDraw.Draw(im)
            dr.text((pad, pad + sz), s, fill=col + (255,), font=f, anchor="ls", stroke_width=sw, stroke_fill=(0, 0, 0, 255))
            bb = im.getbbox() or (0, 0, 1, 1)
            self.cache[k] = (im.crop(bb), (bb[0] - pad, bb[1] - pad - sz))
        return self.cache[k]

    def draw(self, im, xy, s, col, sz):
        g, (dx, dy) = self.get(s, col, sz)
        im.alpha_composite(g, (int(xy[0] + dx), int(xy[1] + dy)))


def _frames(res, T, W, H, fps, i0, i1):
    """프레임 i0 ~ i1-1 그림을 차례로 (bytes) — 구간마다 따로 불러도 결과 같음"""
    import analyze
    presses = [w for w in res.windows if w["kind"] == "press"]
    pt = np.array([w["tick"] for w in presses]); pv = res.precision_run
    bins = [analyze.bin_name(w["window"]) for w in presses]
    xs = np.array([x for x, _, _ in res.track]); n = len(xs)
    base = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for r, b in enumerate(ORDER):
        T.draw(base, (22, 44 + r * 47), b + ":", COLORS[b], 40)
    cnt = {b: 0 for b in ORDER}; j_done = 0; numlayer = None; last_key = None
    for i in range(i0, i1):
        k = min(n - 1, int(i * 240 / fps))
        j = int(np.searchsorted(pt, k, side="right"))
        while j_done < j: cnt[bins[j_done]] += 1; j_done += 1
        key = tuple(cnt[b] for b in ORDER)
        if key != last_key:                                     # 개수가 바뀔 때만 숫자 다시 그림
            numlayer = base.copy()
            for r, b in enumerate(ORDER):
                T.draw(numlayer, (150, 44 + r * 47), str(cnt[b]), COLORS[b], 40)
            last_key = key
        im = numlayer.copy()
        pct = max(0.0, min(100.0, xs[k] / res.level_end * 100)) if res.level_end else 0.0
        T.draw(im, (22, H - 62), "%.2f%%" % pct, (255, 255, 255), 40)
        T.draw(im, (22, H - 16), "%.2f" % (pv[j - 1] if j > 0 else 0.0), (255, 255, 255), 40)
        yield im.tobytes()


def render(res, out_path, W=1280, H=720, fps=60, progress=None, fast=False, parts=None):
    """parts = 동시에 인코딩할 조각 수 (기본: CPU 수, 최대 8). 조각마다 ffmpeg 하나 → 다 끝나면 이어 붙임 (다시 인코딩 안 함)"""
    import tempfile, shutil
    ff = _ffmpeg()
    font_path = os.path.join(HERE, "fonts", "Pusab.otf")
    n = len(res.track); nf = int(n / 240.0 * fps) + 1
    fast = fast or out_path.lower().endswith(".mov")          # .mov 로 저장하면 빠른 방식
    vp9 = (not fast) and has_vp9(ff)
    if not vp9 and out_path.lower().endswith(".webm"): out_path = out_path[:-5] + ".mov"
    ext = ".webm" if vp9 else ".mov"
    codec = ["-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-b:v", "0", "-crf", "30", "-deadline", "realtime", "-cpu-used", "8", "-row-mt", "1"] if vp9 \
        else ["-c:v", "qtrle", "-pix_fmt", "argb"]
    parts = parts or max(1, min(8, os.cpu_count() or 1))
    parts = max(1, min(parts, nf // 120 or 1))                  # 조각은 최소 2초
    tmp = tempfile.mkdtemp(prefix="fwc_ov_")
    bounds = [nf * k // parts for k in range(parts + 1)]
    done = [0] * parts; errs = []; lock = threading.Lock()

    def job(k):
        try:
            T = _Text(font_path)
            seg = os.path.join(tmp, "seg%02d%s" % (k, ext))
            cmd = [ff, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", "%dx%d" % (W, H), "-r", str(fps), "-i", "pipe:0"] + codec + [seg]
            p = subprocess.Popen(cmd, stdin=subprocess.PIPE, **_nowin())
            for c, b in enumerate(_frames(res, T, W, H, fps, bounds[k], bounds[k + 1])):
                p.stdin.write(b)
                if c % 30 == 0:
                    with lock:
                        done[k] = c
                        if progress: progress(0.97 * sum(done) / nf)
            p.stdin.close(); p.wait()
            if p.returncode != 0: errs.append("조각 %d 인코딩 실패" % k)
        except Exception as e:
            errs.append(str(e))

    ths = [threading.Thread(target=job, args=(k,), daemon=True) for k in range(parts)]
    for t in ths: t.start()
    for t in ths: t.join()
    if errs: shutil.rmtree(tmp, ignore_errors=True); raise RuntimeError("ffmpeg 오류: " + errs[0])
    lst = os.path.join(tmp, "list.txt")
    with open(lst, "w", encoding="utf-8") as f:
        for k in range(parts): f.write("file '%s'\n" % os.path.join(tmp, "seg%02d%s" % (k, ext)).replace("\\", "/"))
    r = subprocess.run([ff, "-v", "error", "-y", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", out_path], capture_output=True, text=True, **_nowin())
    shutil.rmtree(tmp, ignore_errors=True)
    if r.returncode != 0: raise RuntimeError("ffmpeg 이어 붙이기 오류: " + (r.stderr or "")[-300:])
    if progress: progress(1.0)
    return out_path
