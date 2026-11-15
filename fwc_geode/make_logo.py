# -*- coding: utf-8 -*-
"""FWC Window Counter 모드 로고 (logo.png, 512x512). 직접 그린 원본 도안.
도안: 어두운 남색 바탕 + 240Hz 눈금 고리 + 살 수 있는 구간(윈도우)을 초록 호로 표시 + 가운데 프레임 수."""
import math, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
S = 4; N = 512 * S
out = sys.argv[1] if len(sys.argv) > 1 else "logo.png"
img = Image.new("RGBA", (N, N), (0, 0, 0, 0)); d = ImageDraw.Draw(img)
d.rounded_rectangle([0, 0, N - 1, N - 1], radius=96 * S, fill=(13, 18, 34, 255))
cx = cy = N // 2
# 은은한 안쪽 빛
glow = Image.new("RGBA", (N, N), (0, 0, 0, 0)); g = ImageDraw.Draw(glow)
g.ellipse([cx - 170 * S, cy - 170 * S, cx + 170 * S, cy + 170 * S], fill=(40, 200, 140, 46))
img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(40 * S))); d = ImageDraw.Draw(img)
R = 190 * S; W = 30 * S
box = [cx - R, cy - R, cx + R, cy + R]
d.arc(box, 0, 360, fill=(46, 56, 84, 255), width=W)                    # 바탕 고리
# 눈금 24개 (프레임)
for k in range(24):
    a = math.radians(k * 15 - 90); r0, r1 = R + 16 * S, R + (34 if k % 6 == 0 else 26) * S
    d.line([cx + r0 * math.cos(a), cy + r0 * math.sin(a), cx + r1 * math.cos(a), cy + r1 * math.sin(a)], fill=(120, 134, 170, 255), width=5 * S)
# 윈도우(살 수 있는 구간): 4칸 = 60도, 위쪽 가운데에
a0, a1 = -90 - 30, -90 + 30
d.arc(box, a0, a1, fill=(52, 224, 150, 255), width=W)
for a in (a0, a1):                                                    # 구간 끝 둥글게
    r = R - W / 2; x = cx + r * math.cos(math.radians(a)); y = cy + r * math.sin(math.radians(a))
    d.ellipse([x - W / 2, y - W / 2, x + W / 2, y + W / 2], fill=(52, 224, 150, 255))
# 누른 자리 (흰 점)
a = math.radians(-90 + 8); r = R - W / 2; x = cx + r * math.cos(a); y = cy + r * math.sin(a)
d.ellipse([x - 13 * S, y - 13 * S, x + 13 * S, y + 13 * S], fill=(255, 255, 255, 255))
# 가운데 숫자 + 글자
f1 = ImageFont.truetype("/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf", 190 * S)
f2 = ImageFont.truetype("/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf", 44 * S)
d.text((cx, cy - 14 * S), "4", font=f1, fill=(255, 255, 255, 255), anchor="mm")
d.text((cx, cy + 108 * S), "FRAMES", font=f2, fill=(52, 224, 150, 255), anchor="mm")
img.resize((512, 512), Image.LANCZOS).save(out)
