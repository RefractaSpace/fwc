"""gui.py — 지메 프레임 윈도우 카운터 (화면). 레벨 + 매크로 → 누름마다 윈도우, 칸별 개수, σ/s, OBS 오버레이 저장.
게임 파일은 읽기만 함 (저장 파일 CCLocalLevels.dat도 읽기만)."""
import os, sys, json, traceback, csv, glob
from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtGui import QColor, QFont, QFontDatabase
from PySide6.QtWidgets import (QApplication, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel, QPushButton, QFileDialog, QComboBox, QInputDialog,
                               QSpinBox, QCheckBox, QPlainTextEdit, QTableWidget, QTableWidgetItem, QHeaderView, QMessageBox, QProgressBar, QGroupBox)

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gdlevel, gdmacro, analyze, overlay

APP_NAME = "GD 프레임 윈도우 카운터"


class Worker(QThread):
    log = Signal(str); done = Signal(object); fail = Signal(str)
    def __init__(self, level_string, macro, offset, noswap):
        super().__init__(); self.args = (level_string, macro, offset, noswap)
    def run(self):
        try:
            ls, m, off, ns = self.args
            sp = os.environ.get("FWC_SAC_PATCH") == "1"            # 시험용: sacrilegium 쇼케이스 고침 (자체 시험에서만)
            self.done.emit(analyze.analyze(ls, m, offset=off, noswap392=ns, log=self.log.emit, sac_patch=sp))
        except Exception as e:
            # 오류가 나도 쇼케이스(오버레이·지오드 보내기)는 할 수 있게: 빈 결과를 만들어 넘김 (윈도우 0개 + 주의 문구)
            r = analyze.Result()
            r.windows = []; r.counts = {nm: 0 for nm, _, _ in analyze.BINS}; r.precision = 0.0; r.precision_run = []
            r.warnings = ["오류로 윈도우를 못 구함 (빈 결과): %s" % str(e).split("\n")[0]]
            r.n = 0; r.level_end = 0.0; r.track = []; r.offset = self.args[2] or 0; r.died = True; r.death_cause = str(e)
            self.log.emit("오류: %s" % traceback.format_exc(limit=3))
            self.done.emit(r)


class OverlayWorker(QThread):
    prog = Signal(float); done = Signal(str); fail = Signal(str)
    def __init__(self, res, path):
        super().__init__(); self.res, self.path = res, path
    def run(self):
        try: self.done.emit(overlay.render(self.res, self.path, progress=self.prog.emit))
        except Exception as e: self.fail.emit(str(e))


class Main(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME); self.resize(980, 720)
        self.levels = []; self.macro = None; self.res = None
        fid = QFontDatabase.addApplicationFont(os.path.join(HERE, "fonts", "Pusab.otf"))
        self.pusab = QFontDatabase.applicationFontFamilies(fid)[0] if fid >= 0 else None
        v = QVBoxLayout(self)

        box = QGroupBox("1. 레벨"); g = QGridLayout(box)
        self.b_level = QPushButton("레벨 파일 열기 (.gmd / .txt)"); self.b_level.clicked.connect(self.open_level)
        self.b_save = QPushButton("게임 저장 파일에서 고르기"); self.b_save.clicked.connect(self.open_save)
        self.c_level = QComboBox(); self.c_level.setMinimumWidth(380)
        g.addWidget(self.b_level, 0, 0); g.addWidget(self.b_save, 0, 1); g.addWidget(self.c_level, 0, 2)
        v.addWidget(box)

        box = QGroupBox("2. 매크로 (xdBot .gdr / .gdr.json, 이클립스 .gdr2)"); h = QHBoxLayout(box)
        self.b_macro = QPushButton("매크로 파일 열기"); self.b_macro.clicked.connect(self.open_macro)
        self.l_macro = QLabel("(없음)")
        h.addWidget(self.b_macro); h.addWidget(self.l_macro, 1)
        v.addWidget(box)

        box = QGroupBox("3. 설정·계산"); h = QHBoxLayout(box)
        h.addWidget(QLabel("입력 밀기(틱)"))
        self.s_off = QSpinBox(); self.s_off.setRange(-10, 10); self.s_off.setToolTip("매크로 프레임과 엔진 틱이 어긋나면 맞춤")
        self.k_auto = QCheckBox("자동"); self.k_auto.setChecked(True); self.k_auto.setToolTip("-2~+2틱 중 매크로가 가장 오래 살아남는 값")
        self.s_off.setEnabled(False); self.k_auto.toggled.connect(lambda on: self.s_off.setEnabled(not on))
        h.addWidget(self.s_off); h.addWidget(self.k_auto)
        self.k_noswap = QCheckBox("돌린 작은 가시(392) 히트박스 안 돌림"); self.k_noswap.setChecked(True)
        h.addWidget(self.k_noswap)
        h.addStretch(1)
        self.b_run = QPushButton("계산"); self.b_run.setMinimumWidth(120); self.b_run.clicked.connect(self.run)
        h.addWidget(self.b_run)
        v.addWidget(box)

        mid = QHBoxLayout()
        self.t_bins = QTableWidget(9, 2); self.t_bins.setHorizontalHeaderLabels(["윈도우(틱)", "누름 수"])
        self.t_bins.verticalHeader().setVisible(False); self.t_bins.setFixedWidth(230)
        self.t_bins.verticalHeader().setDefaultSectionSize(30); self.t_bins.setFixedHeight(30 * 9 + 44)
        self.t_bins.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        for r, b in enumerate(overlay.ORDER):
            it = QTableWidgetItem(b); it.setForeground(QColor(*overlay.COLORS[b])); it.setBackground(QColor(30, 32, 40))
            if self.pusab: it.setFont(QFont(self.pusab, 13))
            self.t_bins.setItem(r, 0, it)
            it2 = QTableWidgetItem("-"); it2.setForeground(QColor(*overlay.COLORS[b])); it2.setBackground(QColor(30, 32, 40))
            if self.pusab: it2.setFont(QFont(self.pusab, 13))
            self.t_bins.setItem(r, 1, it2)
        left = QVBoxLayout(); left.addWidget(self.t_bins)
        self.l_prec = QLabel("σ/s: -"); self.l_prec.setStyleSheet("font-size:20px; font-weight:bold")
        self.l_prec.setToolTip("24시간 안에 깨려면 필요한 타이밍 정확도 (NaNDL 식). 클수록 어려운 타이밍")
        left.addWidget(self.l_prec)
        self.l_warn = QLabel(""); self.l_warn.setWordWrap(True); self.l_warn.setFixedWidth(230); self.l_warn.setStyleSheet("color:#d33; font-weight:bold")
        left.addWidget(self.l_warn); left.addStretch(1)
        mid.addLayout(left)
        self.t_press = QTableWidget(0, 5); self.t_press.setHorizontalHeaderLabels(["시각(초)", "모드", "윈도우", "일찍", "늦게"])
        self.t_press.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch); self.t_press.verticalHeader().setVisible(False)
        mid.addWidget(self.t_press, 1)
        v.addLayout(mid, 1)

        self.log = QPlainTextEdit(); self.log.setReadOnly(True); self.log.setMaximumHeight(130)
        v.addWidget(self.log)
        self.prog = QProgressBar(); self.prog.setRange(0, 100); self.prog.setVisible(False); v.addWidget(self.prog)
        h = QHBoxLayout()
        self.b_csv = QPushButton("결과 저장 (.csv / .json)"); self.b_csv.clicked.connect(self.save_results); self.b_csv.setEnabled(False)
        self.b_ov = QPushButton("OBS 오버레이 저장 (.webm)"); self.b_ov.clicked.connect(self.save_overlay); self.b_ov.setEnabled(False)
        self.b_geode = QPushButton("지오드로 보내기 (게임 안 실시간 표시)"); self.b_geode.clicked.connect(self.send_geode); self.b_geode.setEnabled(False)
        h.addWidget(self.b_csv); h.addWidget(self.b_ov); h.addWidget(self.b_geode); h.addStretch(1)
        v.addLayout(h)
        self.say("레벨과 매크로를 고른 뒤 [계산]을 누르세요. 게임 파일은 읽기만 합니다.")

    def say(self, s): self.log.appendPlainText(s)

    def _set_levels(self, lv):
        self.levels = lv; self.c_level.clear()
        for n, s in lv: self.c_level.addItem("%s  (%d자)" % (n, len(s)))

    def open_level(self):
        p, _ = QFileDialog.getOpenFileName(self, "레벨 파일", "", "레벨 (*.gmd *.txt *.dat);;모든 파일 (*)")
        if p: self.load_level_path(p)

    def load_level_path(self, p):
        try:
            self._set_levels(gdlevel.list_levels(p)); self.say("레벨 %d개 읽음: %s" % (len(self.levels), os.path.basename(p)))
        except Exception as e:
            QMessageBox.warning(self, APP_NAME, "레벨을 못 읽었어요: %s" % e)

    def open_save(self):
        """게임 저장 파일(읽기만). 지메를 여러 개 쓰면(예: GeometryDash, GeometryDash2208) 저장 폴더도 여러 개라 고르게 함"""
        base = os.environ.get("LOCALAPPDATA", "")
        found = sorted(glob.glob(os.path.join(base, "GeometryDash*", "CCLocalLevels.dat")))
        d = ""
        if len(found) == 1: d = found[0]
        elif len(found) > 1:
            names = [os.path.basename(os.path.dirname(f)) for f in found]
            n, ok = QInputDialog.getItem(self, APP_NAME, "저장 폴더가 여러 개예요. 어느 게임의 저장 파일을 읽을까요?", names, len(names) - 1, False)
            if not ok: return
            d = found[names.index(n)]
        if not d:
            d, _ = QFileDialog.getOpenFileName(self, "CCLocalLevels.dat", "", "저장 파일 (CCLocalLevels.dat);;모든 파일 (*)")
            if not d: return
        self.load_level_path(d)

    def open_macro(self):
        p, _ = QFileDialog.getOpenFileName(self, "매크로", "", "매크로 (*.gdr *.json *.gdr2);;모든 파일 (*)")
        if p: self.load_macro_path(p)

    def load_macro_path(self, p):
        try:
            self.macro = gdmacro.load(p)
            m = self.macro
            self.l_macro.setText("%s · %s · %.0f틱 · 입력 %d개 · 레벨 \"%s\"" % (os.path.basename(p), m.fmt, m.tps, len(m.inputs), m.level_name))
            if m.level_name:
                for i, (n, _) in enumerate(self.levels):
                    if n == m.level_name: self.c_level.setCurrentIndex(i)
        except Exception as e:
            QMessageBox.warning(self, APP_NAME, "매크로를 못 읽었어요: %s" % e)

    def run(self):
        if not self.levels or self.macro is None:
            QMessageBox.information(self, APP_NAME, "레벨과 매크로를 먼저 골라 주세요."); return
        ls = self.levels[self.c_level.currentIndex()][1]
        self.b_run.setEnabled(False); self.say("계산 시작…")
        self.w = Worker(ls, self.macro, None if self.k_auto.isChecked() else self.s_off.value(), self.k_noswap.isChecked())
        self.w.log.connect(self.say); self.w.done.connect(self.show_result); self.w.fail.connect(self.failed); self.w.start()

    def failed(self, msg):
        self.b_run.setEnabled(True); self.say("오류: " + msg.split("\n")[0]); QMessageBox.warning(self, APP_NAME, msg)

    def show_result(self, res):
        self.res = res; self.b_run.setEnabled(True)
        if self.k_auto.isChecked(): self.s_off.setValue(res.offset)
        self.b_csv.setEnabled(True); self.b_ov.setEnabled(True); self.b_geode.setEnabled(True)
        for r, b in enumerate(overlay.ORDER): self.t_bins.item(r, 1).setText(str(res.counts[b]))
        self.l_prec.setText("σ/s: %.2f" % res.precision)
        pr = [w for w in res.windows if w["kind"] == "press"]
        self.t_press.setRowCount(len(pr))
        for i, w in enumerate(pr):
            b = analyze.bin_name(w["window"])
            vals = ["%.3f" % (w["tick"] / 240.0), w["mode"], str(w["window"]), str(w["early"]), str(w["late"])]
            for c, s in enumerate(vals):
                it = QTableWidgetItem(s)
                if c == 2: it.setForeground(QColor(*overlay.COLORS[b])); it.setBackground(QColor(30, 32, 40))
                self.t_press.setItem(i, c, it)
        for w in res.warnings: self.say("주의: " + w)
        self.l_warn.setText("\n".join(res.warnings))

    def save_results(self):
        p, _ = QFileDialog.getSaveFileName(self, "결과 저장", "windows.csv", "CSV (*.csv);;JSON (*.json)")
        if not p: return
        if p.lower().endswith(".json"): analyze.save_json(self.res, p)
        else:
            with open(p, "w", newline="", encoding="utf-8-sig") as f:
                wr = csv.writer(f); wr.writerow(["종류", "틱", "시각(초)", "모드", "윈도우", "일찍", "늦게", "칸"])
                for w in self.res.windows:
                    wr.writerow([w["kind"], w["tick"], "%.4f" % (w["tick"] / 240.0), w["mode"], w["window"], w["early"], w["late"], analyze.bin_name(w["window"])])
        self.say("저장: " + p)

    @staticmethod
    def _has_gd_exe(d):
        """지메 실행 파일이 있는 폴더인가 (GeometryDash.exe, GeometryDash2208.exe 같은 이름도 됨)"""
        return bool(d) and os.path.isdir(d) and bool(glob.glob(os.path.join(d, "GeometryDash*.exe")))

    def _gd_dir(self):
        """지메 설치 폴더. 전에 고른 곳 → 없으면 스팀 기본 위치·내려받은 옛 버전 위치를 찾고, 여러 개면 물어봄. 고른 폴더는 다음에도 씀"""
        cfg = os.path.join(os.path.expanduser("~"), ".fwc_counter_gd_dir.txt")
        try:
            saved = open(cfg, encoding="utf-8").read().strip()
            if self._has_gd_exe(saved): return saved
        except OSError: pass
        cands = []
        for root in (os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), os.environ.get("ProgramFiles", r"C:\Program Files")):
            cands.append(os.path.join(root, "Steam", "steamapps", "common", "Geometry Dash"))
            cands += glob.glob(os.path.join(root, "Steam", "steamapps", "content", "app_322170", "depot_*"))
        found = []
        for c in cands:
            if self._has_gd_exe(c) and c not in found: found.append(c)
        p = None
        if len(found) == 1: p = found[0]
        elif len(found) > 1:
            n, ok = QInputDialog.getItem(self, APP_NAME, "지메가 여러 곳에 있어요. 지오드 모드를 쓰는 게임 폴더를 고르세요:", found, len(found) - 1, False)
            if not ok: return None
            p = n
        else:
            p = QFileDialog.getExistingDirectory(self, "지오메트리 대시 설치 폴더 (GeometryDash 실행 파일 있는 곳)")
            if not self._has_gd_exe(p): return None
        try: open(cfg, "w", encoding="utf-8").write(p)
        except OSError: pass
        return p

    def send_geode(self):
        """계산 결과를 지오드 모드 설정 폴더에 <레벨 이름>.json 으로 넣음 (게임 파일은 안 건드림: geode/config 안 모드 폴더만)"""
        if self.res is None or not self.levels: return
        gd = self._gd_dir()
        if not gd:
            QMessageBox.warning(self, APP_NAME, "지메 실행 파일이 있는 폴더를 찾지 못했어요."); return
        name = self.levels[self.c_level.currentIndex()][0]
        d = os.path.join(gd, "geode", "config", "fwc.window-counter", "levels")
        os.makedirs(d, exist_ok=True)
        safe = "".join(ch if ch not in '\\/:*?"<>|' else "_" for ch in name)
        p = os.path.join(d, safe + ".json")
        analyze.save_json(self.res, p)
        self.say("지오드로 보냄: " + p + "  (게임에서 레벨 \"%s\" 를 열면 왼쪽 위에 표시)" % name)

    def save_overlay(self):
        p, _ = QFileDialog.getSaveFileName(self, "OBS 오버레이", "overlay.webm", "WebM - 작은 파일 (*.webm);;MOV - 빠름, 큰 파일 (*.mov)")
        if not p: return
        self.prog.setVisible(True); self.prog.setValue(0); self.b_ov.setEnabled(False)
        self.ow = OverlayWorker(self.res, p)
        self.ow.prog.connect(lambda f: self.prog.setValue(int(f * 100)))
        self.ow.done.connect(lambda q: (self.prog.setVisible(False), self.b_ov.setEnabled(True), self.say("오버레이 저장: " + q)))
        self.ow.fail.connect(lambda m: (self.prog.setVisible(False), self.b_ov.setEnabled(True), QMessageBox.warning(self, APP_NAME, m)))
        self.ow.start()


def selftest(level, macro, out_png):
    """화면 없이 시험: GD_FWC --selftest 레벨 매크로 결과.png → 계산 후 화면을 그림으로 저장, 결과를 표준출력에"""
    from PySide6.QtCore import QEventLoop
    app = QApplication(sys.argv[:1])
    w = Main(); w.show()
    w.load_level_path(level); w.c_level.setCurrentIndex(w.c_level.count() - 1)
    w.load_macro_path(macro); w.run()
    loop = QEventLoop(); w.w.finished.connect(loop.quit); loop.exec(); app.processEvents()
    w.grab().save(out_png)
    if w.res is not None: analyze.save_json(w.res, os.path.splitext(out_png)[0] + ".json")
    print(w.l_prec.text(), "|", w.l_warn.text().replace("\n", " / "))
    return 0


def main():
    if len(sys.argv) >= 5 and sys.argv[1] == "--selftest":
        sys.exit(selftest(sys.argv[2], sys.argv[3], sys.argv[4]))
    app = QApplication(sys.argv)
    w = Main(); w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
