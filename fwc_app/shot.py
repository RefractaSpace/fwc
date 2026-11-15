import os, sys, time
os.environ["QT_QPA_PLATFORM"] = "offscreen"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QEventLoop, QTimer
import gui
app = QApplication(sys.argv)
w = gui.Main(); w.show()
w.load_level_path(sys.argv[1]); w.c_level.setCurrentIndex(w.c_level.count() - 1)
w.load_macro_path(sys.argv[2])
w.grab().save(sys.argv[3] + "_before.png")
w.run()
loop = QEventLoop(); w.w.finished.connect(loop.quit); loop.exec()
app.processEvents()
w.grab().save(sys.argv[3] + "_after.png")
print("결과", w.l_prec.text())
