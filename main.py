import sys
import os
import pymupdf as fitz
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QComboBox, QFileDialog, QSlider,
    QFrame, QMessageBox
)
from PyQt6.QtGui import QImage, QPixmap, QKeySequence, QShortcut, QIcon
from PyQt6.QtCore import Qt, QTimer


def get_resource_path(filename):
    if hasattr(sys, "_MEIPASS"):
        return os.path.join(sys._MEIPASS, filename)
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), filename)


class PDFEngine:
    def __init__(self):
        self.doc = None
        self.total_pages = 0
        self.cache = {}
        self.max_cache_size = 25

    def open_file(self, filepath):
        self.cache.clear()
        self.doc = fitz.open(filepath)
        self.total_pages = len(self.doc)
        return self.total_pages

    def is_loaded(self):
        return self.doc is not None and self.total_pages > 0

    def render_page(self, page_index):
        if not self.is_loaded() or page_index < 0 or page_index >= self.total_pages:
            return None

        if page_index in self.cache:
            return self.cache[page_index]

        pixmap = self._render_raw(page_index)
        if len(self.cache) >= self.max_cache_size:
            oldest_key = next(iter(self.cache))
            del self.cache[oldest_key]

        self.cache[page_index] = pixmap
        return pixmap

    def _render_raw(self, page_index):
        page = self.doc.load_page(page_index)
        mat = fitz.Matrix(2.0, 2.0)
        pix = page.get_pixmap(matrix=mat, alpha=False)
        img = QImage(pix.samples, pix.width, pix.height, pix.stride, QImage.Format.Format_RGB888).copy()
        return QPixmap.fromImage(img)

    def prefetch(self, current_page):
        targets = [current_page, current_page + 1, current_page + 2, current_page - 1]
        for p in targets:
            if 0 <= p < self.total_pages and p not in self.cache:
                self.cache[p] = self._render_raw(p)


class AudienceWindow(QMainWindow):
    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.is_blank = False
        self.current_pixmap = None

        self.setWindowTitle("Lite PDF Presentation - Audience Display")
        self.setStyleSheet("background-color: black;")

        icon_path = get_resource_path("app_icon.ico")
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))

        self.label = QLabel(self)
        self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.label.setStyleSheet("background-color: black;")
        self.setCentralWidget(self.label)

    def set_slide(self, pixmap, is_blank=False):
        self.is_blank = is_blank
        self.current_pixmap = pixmap
        self.update_display()

    def update_display(self):
        if self.is_blank or self.current_pixmap is None:
            self.label.clear()
            self.label.setStyleSheet("background-color: black;")
            return

        self.label.setStyleSheet("background-color: black;")
        scaled = self.current_pixmap.scaled(
            self.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation
        )
        self.label.setPixmap(scaled)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.update_display()

    def keyPressEvent(self, event):
        self.controller.handle_key(event)


class PresenterWindow(QMainWindow):
    def __init__(self, initial_file=None):
        super().__init__()
        self.engine = PDFEngine()
        self.audience_win = None
        self.current_page = 0
        self.is_blank = False
        self.is_at_end_black = False
        self.is_presenting = False
        self.file_path = ""

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_timer)
        self.elapsed_seconds = 0

        icon_path = get_resource_path("app_icon.ico")
        if os.path.exists(icon_path):
            self.setWindowIcon(QIcon(icon_path))

        self.init_ui()

        if initial_file and os.path.isfile(initial_file):
            self.load_pdf(initial_file)

    def init_ui(self):
        self.setWindowTitle("Lite PDF Presentation")
        self.resize(1100, 720)
        self.setMinimumSize(850, 550)
        self.setAcceptDrops(True)
        self.apply_dark_theme()

        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(12)

        top_bar = QFrame()
        top_bar.setObjectName("topBar")
        top_layout = QHBoxLayout(top_bar)
        top_layout.setContentsMargins(10, 10, 10, 10)

        self.btn_open = QPushButton("📂 Open PDF")
        self.btn_open.setFixedHeight(38)
        self.btn_open.clicked.connect(self.select_file)
        top_layout.addWidget(self.btn_open)

        self.lbl_file = QLabel("No file selected")
        self.lbl_file.setStyleSheet("color: #aaa; font-style: italic;")
        top_layout.addWidget(self.lbl_file, 1)

        lbl_screen = QLabel("Display:")
        lbl_screen.setStyleSheet("font-weight: bold;")
        top_layout.addWidget(lbl_screen)

        self.combo_screens = QComboBox()
        self.combo_screens.setFixedHeight(38)
        self.combo_screens.setMinimumWidth(220)
        top_layout.addWidget(self.combo_screens)

        self.btn_refresh_screens = QPushButton("🔄 Refresh")
        self.btn_refresh_screens.setFixedHeight(38)
        self.btn_refresh_screens.clicked.connect(self.populate_screens)
        top_layout.addWidget(self.btn_refresh_screens)

        self.btn_start = QPushButton("▶ Start Presentation (F5)")
        self.btn_start.setObjectName("btnStart")
        self.btn_start.setFixedHeight(38)
        self.btn_start.clicked.connect(self.toggle_presentation)
        self.btn_start.setEnabled(False)
        top_layout.addWidget(self.btn_start)

        main_layout.addWidget(top_bar)

        preview_container = QWidget()
        preview_layout = QHBoxLayout(preview_container)
        preview_layout.setContentsMargins(0, 0, 0, 0)
        preview_layout.setSpacing(15)

        left_box = QFrame()
        left_box.setObjectName("slideCard")
        left_layout = QVBoxLayout(left_box)

        lbl_cur_title = QLabel("CURRENT SLIDE")
        lbl_cur_title.setStyleSheet("font-weight: bold; color: #4CAF50;")
        left_layout.addWidget(lbl_cur_title)

        self.lbl_cur_preview = QLabel("Please open or drop a PDF file here")
        self.lbl_cur_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.lbl_cur_preview.setStyleSheet("background-color: #121212; border-radius: 6px;")
        left_layout.addWidget(self.lbl_cur_preview, 1)
        preview_layout.addWidget(left_box, 3)

        right_box = QFrame()
        right_box.setObjectName("slideCard")
        right_layout = QVBoxLayout(right_box)

        lbl_next_title = QLabel("NEXT SLIDE")
        lbl_next_title.setStyleSheet("font-weight: bold; color: #2196F3;")
        right_layout.addWidget(lbl_next_title)

        self.lbl_next_preview = QLabel("-")
        self.lbl_next_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.lbl_next_preview.setStyleSheet("background-color: #121212; border-radius: 6px;")
        right_layout.addWidget(self.lbl_next_preview, 1)

        timer_box = QFrame()
        timer_box.setStyleSheet("background-color: #1e1e1e; border-radius: 6px; padding: 6px;")
        t_layout = QVBoxLayout(timer_box)
        t_layout.setContentsMargins(8, 8, 8, 8)

        lbl_timer_head = QLabel("ELAPSED TIME")
        lbl_timer_head.setStyleSheet("color: #888; font-size: 11px;")
        t_layout.addWidget(lbl_timer_head)

        self.lbl_timer = QLabel("00:00:00")
        self.lbl_timer.setStyleSheet("font-size: 26px; font-weight: bold; color: #ffeb3b;")
        t_layout.addWidget(self.lbl_timer)

        btn_reset_timer = QPushButton("Reset Timer")
        btn_reset_timer.clicked.connect(self.reset_timer)
        t_layout.addWidget(btn_reset_timer)
        right_layout.addWidget(timer_box)

        preview_layout.addWidget(right_box, 2)
        main_layout.addWidget(preview_container, 1)

        bottom_bar = QFrame()
        bottom_bar.setObjectName("bottomBar")
        bot_layout = QHBoxLayout(bottom_bar)
        bot_layout.setContentsMargins(10, 8, 10, 8)

        self.btn_prev = QPushButton("◀ Previous")
        self.btn_prev.setFixedHeight(36)
        self.btn_prev.clicked.connect(self.prev_slide)
        bot_layout.addWidget(self.btn_prev)

        self.btn_next = QPushButton("Next ▶")
        self.btn_next.setFixedHeight(36)
        self.btn_next.clicked.connect(self.next_slide)
        bot_layout.addWidget(self.btn_next)

        bot_layout.addSpacing(15)

        self.lbl_page_info = QLabel("Slide: 0 / 0")
        self.lbl_page_info.setStyleSheet("font-weight: bold; font-size: 14px;")
        bot_layout.addWidget(self.lbl_page_info)

        self.slider = QSlider(Qt.Orientation.Horizontal)
        self.slider.setRange(1, 1)
        self.slider.valueChanged.connect(self.on_slider_change)
        bot_layout.addWidget(self.slider, 1)

        self.btn_blank = QPushButton("⬛ Black Screen (B)")
        self.btn_blank.setFixedHeight(36)
        self.btn_blank.clicked.connect(self.toggle_blank)
        bot_layout.addWidget(self.btn_blank)

        main_layout.addWidget(bottom_bar)

        self.setup_shortcuts()
        self.populate_screens()

    def apply_dark_theme(self):
        self.setStyleSheet("""
            QMainWindow {
                background-color: #18181b;
            }
            QWidget {
                color: #e4e4e7;
                font-family: 'Segoe UI', sans-serif;
                font-size: 13px;
            }
            #topBar, #bottomBar, #slideCard {
                background-color: #27272a;
                border-radius: 8px;
                border: 1px solid #3f3f46;
            }
            QPushButton {
                background-color: #3b82f6;
                color: white;
                border: none;
                border-radius: 6px;
                padding: 6px 14px;
                font-weight: 600;
            }
            QPushButton:hover {
                background-color: #2563eb;
            }
            QPushButton:pressed {
                background-color: #1d4ed8;
            }
            QPushButton:disabled {
                background-color: #3f3f46;
                color: #71717a;
            }
            #btnStart {
                background-color: #10b981;
            }
            #btnStart:hover {
                background-color: #059669;
            }
            QComboBox {
                background-color: #18181b;
                border: 1px solid #52525b;
                border-radius: 6px;
                padding: 5px 10px;
                color: white;
            }
            QSlider::groove:horizontal {
                height: 6px;
                background: #3f3f46;
                border-radius: 3px;
            }
            QSlider::sub-page:horizontal {
                background: #3b82f6;
                border-radius: 3px;
            }
            QSlider::handle:horizontal {
                background: #ffffff;
                width: 14px;
                margin: -4px 0;
                border-radius: 7px;
            }
        """)

    def populate_screens(self):
        self.combo_screens.clear()
        screens = QApplication.screens()
        for idx, scr in enumerate(screens):
            geom = scr.geometry()
            name = f"Display {idx + 1}"
            if idx == 0:
                name += " (Primary)"
            else:
                name += " (External / Projector)"
            desc = f"{name} - {geom.width()}x{geom.height()}"
            self.combo_screens.addItem(desc, idx)

        if len(screens) > 1:
            self.combo_screens.setCurrentIndex(1)
        else:
            self.combo_screens.setCurrentIndex(0)

    def setup_shortcuts(self):
        QShortcut(QKeySequence(Qt.Key.Key_F5), self, self.start_presentation)
        QShortcut(QKeySequence(Qt.Key.Key_Escape), self, self.stop_presentation)
        QShortcut(QKeySequence(Qt.Key.Key_Right), self, self.next_slide)
        QShortcut(QKeySequence(Qt.Key.Key_Space), self, self.next_slide)
        QShortcut(QKeySequence(Qt.Key.Key_PageDown), self, self.next_slide)
        QShortcut(QKeySequence(Qt.Key.Key_Left), self, self.prev_slide)
        QShortcut(QKeySequence(Qt.Key.Key_PageUp), self, self.prev_slide)
        QShortcut(QKeySequence(Qt.Key.Key_B), self, self.toggle_blank)

    def select_file(self):
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Open PDF Document", "", "PDF Files (*.pdf)"
        )
        if file_path:
            self.load_pdf(file_path)

    def load_pdf(self, file_path):
        try:
            total = self.engine.open_file(file_path)
            self.file_path = file_path
            self.lbl_file.setText(os.path.basename(file_path))
            self.lbl_file.setStyleSheet("color: #4CAF50; font-weight: bold;")
            self.current_page = 0
            self.is_at_end_black = False
            self.slider.setRange(1, total)
            self.slider.setValue(1)
            self.btn_start.setEnabled(True)
            self.render_views()
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to open PDF:\n{str(e)}")

    def render_views(self):
        if not self.engine.is_loaded():
            return

        total = self.engine.total_pages

        if self.is_at_end_black:
            self.lbl_page_info.setText(f"Slide: {total} / {total} (End)")
            self.lbl_cur_preview.clear()
            self.lbl_cur_preview.setText("End of slide show.\nAdvance again to exit presentation.")
            self.lbl_cur_preview.setStyleSheet("background-color: #000000; color: #a1a1aa; border-radius: 6px; font-size: 14px;")

            self.lbl_next_preview.clear()
            self.lbl_next_preview.setText("Exit Presentation")
            self.lbl_next_preview.setStyleSheet("background-color: #121212; color: #ef4444; border-radius: 6px; font-weight: bold;")

            if self.audience_win:
                self.audience_win.set_slide(None, is_blank=True)
            return

        self.lbl_cur_preview.setStyleSheet("background-color: #121212; border-radius: 6px;")
        self.lbl_next_preview.setStyleSheet("background-color: #121212; border-radius: 6px;")

        self.lbl_page_info.setText(f"Slide: {self.current_page + 1} / {total}")
        self.slider.blockSignals(True)
        self.slider.setValue(self.current_page + 1)
        self.slider.blockSignals(False)

        self.engine.prefetch(self.current_page)

        cur_pix = self.engine.render_page(self.current_page)
        if cur_pix:
            scaled_cur = cur_pix.scaled(
                self.lbl_cur_preview.size(),
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation
            )
            self.lbl_cur_preview.setPixmap(scaled_cur)

        if self.current_page + 1 < total:
            next_pix = self.engine.render_page(self.current_page + 1)
            if next_pix:
                scaled_next = next_pix.scaled(
                    self.lbl_next_preview.size(),
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation
                )
                self.lbl_next_preview.setPixmap(scaled_next)
        else:
            self.lbl_next_preview.clear()
            self.lbl_next_preview.setText("Black Screen (End)")
            self.lbl_next_preview.setStyleSheet("background-color: #121212; color: #a1a1aa; border-radius: 6px;")

        if self.audience_win:
            self.audience_win.set_slide(cur_pix, self.is_blank)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.engine.is_loaded():
            self.render_views()

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            for url in event.mimeData().urls():
                if url.toLocalFile().lower().endswith(".pdf"):
                    event.acceptProposedAction()
                    return
        event.ignore()

    def dropEvent(self, event):
        if event.mimeData().hasUrls():
            for url in event.mimeData().urls():
                file_path = url.toLocalFile()
                if file_path.lower().endswith(".pdf") and os.path.isfile(file_path):
                    self.load_pdf(file_path)
                    event.acceptProposedAction()
                    return

    def toggle_presentation(self):
        if self.is_presenting:
            self.stop_presentation()
        else:
            self.start_presentation()

    def start_presentation(self):
        if not self.engine.is_loaded():
            return

        screens = QApplication.screens()
        selected_idx = self.combo_screens.currentData()
        if selected_idx is None or selected_idx >= len(screens):
            selected_idx = 0
        target_screen = screens[selected_idx]

        if not self.audience_win:
            self.audience_win = AudienceWindow(self)

        self.audience_win.setScreen(target_screen)
        geom = target_screen.geometry()
        self.audience_win.setGeometry(geom)
        self.audience_win.showFullScreen()

        self.is_presenting = True
        self.is_at_end_black = False
        self.btn_start.setText("⏹ Stop Presentation (Esc)")
        self.btn_start.setStyleSheet("background-color: #ef4444;")

        self.timer.start(1000)
        self.render_views()

    def stop_presentation(self):
        if self.audience_win:
            self.audience_win.close()
            self.audience_win = None
        self.is_presenting = False
        self.is_at_end_black = False
        self.btn_start.setText("▶ Start Presentation (F5)")
        self.btn_start.setStyleSheet("")
        self.timer.stop()
        self.render_views()

    def next_slide(self):
        if not self.engine.is_loaded():
            return

        if self.is_at_end_black:
            self.stop_presentation()
            return

        if self.current_page < self.engine.total_pages - 1:
            self.current_page += 1
            if self.is_blank:
                self.is_blank = False
                self.btn_blank.setText("⬛ Black Screen (B)")
            self.render_views()
        elif self.current_page == self.engine.total_pages - 1:
            if self.is_presenting:
                self.is_at_end_black = True
                self.render_views()

    def prev_slide(self):
        if not self.engine.is_loaded():
            return

        if self.is_at_end_black:
            self.is_at_end_black = False
            self.render_views()
            return

        if self.current_page > 0:
            self.current_page -= 1
            if self.is_blank:
                self.is_blank = False
                self.btn_blank.setText("⬛ Black Screen (B)")
            self.render_views()

    def on_slider_change(self, val):
        if self.engine.is_loaded():
            self.is_at_end_black = False
            self.current_page = val - 1
            self.render_views()

    def toggle_blank(self):
        if self.is_at_end_black:
            return
        self.is_blank = not self.is_blank
        if self.is_blank:
            self.btn_blank.setText("⬜ Resume Slide (B)")
        else:
            self.btn_blank.setText("⬛ Black Screen (B)")
        self.render_views()

    def update_timer(self):
        self.elapsed_seconds += 1
        mins, secs = divmod(self.elapsed_seconds, 60)
        hours, mins = divmod(mins, 60)
        self.lbl_timer.setText(f"{hours:02d}:{mins:02d}:{secs:02d}")

    def reset_timer(self):
        self.elapsed_seconds = 0
        self.lbl_timer.setText("00:00:00")

    def handle_key(self, event):
        key = event.key()
        if key in (Qt.Key.Key_Right, Qt.Key.Key_Space, Qt.Key.Key_PageDown):
            self.next_slide()
        elif key in (Qt.Key.Key_Left, Qt.Key.Key_PageUp):
            self.prev_slide()
        elif key == Qt.Key.Key_B:
            self.toggle_blank()
        elif key == Qt.Key.Key_Escape:
            self.stop_presentation()


def main():
    app = QApplication(sys.argv)
    icon_path = get_resource_path("app_icon.ico")
    if os.path.exists(icon_path):
        app.setWindowIcon(QIcon(icon_path))

    initial_file = None
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]) and sys.argv[1].lower().endswith(".pdf"):
        initial_file = sys.argv[1]

    window = PresenterWindow(initial_file=initial_file)
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
