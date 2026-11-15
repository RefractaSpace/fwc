"""overlay.py — 계산 결과 → OBS에 덮어씌우는 투명 영상 (왼쪽 위 칸별 개수, 왼쪽 아래 진행률·σ/s)
라우치카머 영상 배치를 참고한 화면 (그대로 베끼지 않음). 글꼴: Pusab (Flat-it 프리웨어, fonts/Pusab.otf)
영상 시작 = 매크로 0프레임(레벨 시작). 녹화 영상과 맞출 때는 OBS에서 시작 시점을 맞춤.
인코더: VP9 + 투명(.webm). 그 ffmpeg에 VP9가 없으면 QuickTime Animation(.mov, 투명) 으로 저장."""
import os, subprocess
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


def render(res, out_path, W=1280, H=720, fps=60, progress=None):
    import analyze
    ff = _ffmpeg()
    font_path = os.path.join(HERE, "fonts", "Pusab.otf")
    fonts = {}
    def font(sz):
        if sz not in fonts: fonts[sz] = ImageFont.truetype(font_path, sz)
        return fonts[sz]
    def text(dr, xy, s, col, sz):
        dr.text(xy, s, fill=col + (255,), font=font(sz), anchor="ls", stroke_width=max(2, sz // 7), stroke_fill=(0, 0, 0, 255))
    presses = [w for w in res.windows if w["kind"] == "press"]
    pt = np.array([w["tick"] for w in presses]); pv = res.precision_run
    xs = np.array([x for x, _, _ in res.track]); n = len(xs)
    nf = int(n / 240.0 * fps) + 1
    vp9 = has_vp9(ff)
    if not vp9 and out_path.lower().endswith(".webm"): out_path = out_path[:-5] + ".mov"
    codec = ["-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-b:v", "0", "-crf", "30", "-deadline", "realtime", "-cpu-used", "8", "-row-mt", "1"] if vp9 \
        else ["-c:v", "qtrle", "-pix_fmt", "argb"]
    cmd = [ff, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba", "-s", "%dx%d" % (W, H), "-r", str(fps), "-i", "pipe:0"] + codec + [out_path]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, **_nowin())
    for i in range(nf):
        k = min(n - 1, int(i * 240 / fps))
        j = int(np.searchsorted(pt, k, side="right"))
        cnt = {b: 0 for b in ORDER}
        for w in presses[:j]: cnt[analyze.bin_name(w["window"])] += 1
        im = Image.new("RGBA", (W, H), (0, 0, 0, 0)); dr = ImageDraw.Draw(im)
        for r, b in enumerate(ORDER):
            y = 44 + r * 47
            text(dr, (22, y), b + ":", COLORS[b], 40); text(dr, (150, y), str(cnt[b]), COLORS[b], 40)
        pct = max(0.0, min(100.0, xs[k] / res.level_end * 100)) if res.level_end else 0.0
        text(dr, (22, H - 62), "%.2f%%" % pct, (255, 255, 255), 40)
        text(dr, (22, H - 16), "%.2f" % (pv[j - 1] if j > 0 else 0.0), (255, 255, 255), 40)
        p.stdin.write(im.tobytes())
        if progress and i % 60 == 0: progress(i / nf)
    p.stdin.close(); p.wait()
    if p.returncode != 0: raise RuntimeError("ffmpeg 오류")
    return out_path
