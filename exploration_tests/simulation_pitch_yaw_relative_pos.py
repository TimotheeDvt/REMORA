import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                             QDial, QLabel, QHBoxLayout, QPushButton)
from PyQt5.QtCore import Qt

class BoSimulator(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("NIME Bô - Multi-View Spatial Test")
        self.staff_length = 4.0

        # Main Widget and Layout
        self.main_widget = QWidget()
        self.setCentralWidget(self.main_widget)
        self.layout = QVBoxLayout(self.main_widget)

        # 3D Matplotlib Canvas
        plt.style.use('seaborn-v0_8-muted')
        self.figure = plt.figure(figsize=(8, 8))
        self.canvas = FigureCanvas(self.figure)
        self.ax = self.figure.add_subplot(111, projection='3d')
        self.layout.addWidget(self.canvas)

        # View Selection Layout
        self.view_layout = QHBoxLayout()
        self.layout.addLayout(self.view_layout)

        self.btn_3d = self.create_view_button("Perspective (3D)", lambda: self.set_view(30, -60))
        self.btn_top = self.create_view_button("Top View (XY)", lambda: self.set_view(90, -90))
        self.btn_front = self.create_view_button("Front View (XZ)", lambda: self.set_view(0, -90))
        self.btn_side = self.create_view_button("Side View (YZ)", lambda: self.set_view(0, 0))

        # Controls Layout (Knobs)
        self.controls_layout = QHBoxLayout()
        self.layout.addLayout(self.controls_layout)

        self.pitch_dial, self.pitch_label = self.create_knob("PITCH (Tilt)", self.controls_layout)
        self.yaw_dial, self.yaw_label = self.create_knob("YAW (Pivot)", self.controls_layout)

        self.update_plot()

    def create_view_button(self, text, callback):
        btn = QPushButton(text)
        btn.setMinimumHeight(40)
        btn.setStyleSheet("font-weight: bold; background-color: #f0f0f0;")
        btn.clicked.connect(callback)
        self.view_layout.addWidget(btn)
        return btn

    def set_view(self, elev, azim):
        """Sets the camera perspective and redraws."""
        self.ax.view_init(elev=elev, azim=azim)
        self.canvas.draw()

    def create_knob(self, title, layout):
        container = QVBoxLayout()
        title_label = QLabel(title)
        title_label.setAlignment(Qt.AlignCenter)
        title_label.setStyleSheet("font-weight: bold; font-size: 14px;")

        dial = QDial()
        dial.setRange(-180, 180)
        dial.setValue(0)
        dial.setNotchesVisible(True)
        dial.setWrapping(True)
        dial.setFixedSize(150, 150)
        dial.valueChanged.connect(self.update_plot)

        value_label = QLabel("0°")
        value_label.setAlignment(Qt.AlignCenter)
        value_label.setStyleSheet("font-family: monospace; font-size: 12px; color: blue;")

        container.addWidget(title_label)
        container.addWidget(dial)
        container.addWidget(value_label)
        layout.addLayout(container)
        return dial, value_label

    def get_rotation_matrix(self, p, y):
        p, y = np.radians([p, y])
        # Rotation around Y (Pitch)
        Ry = np.array([[np.cos(p), 0, np.sin(p)],
                       [0, 1, 0],
                       [-np.sin(p), 0, np.cos(p)]])
        # Rotation around Z (Yaw)
        Rz = np.array([[np.cos(y), -np.sin(y), 0],
                       [np.sin(y), np.cos(y), 0],
                       [0, 0, 1]])
        return Rz @ Ry

    def update_plot(self):
        curr_elev = self.ax.elev
        curr_azim = self.ax.azim

        self.ax.clear()
        self.ax.view_init(elev=curr_elev, azim=curr_azim)

        p = self.pitch_dial.value()
        y = self.yaw_dial.value()
        self.pitch_label.setText(f"Value: {p}°")
        self.yaw_label.setText(f"Value: {y}°")

        end_a_neutral = np.array([self.staff_length / 2, 0, 0])
        end_b_neutral = np.array([-self.staff_length / 2, 0, 0])

        rot_matrix = self.get_rotation_matrix(p, y)
        end_a = rot_matrix @ end_a_neutral
        end_b = rot_matrix @ end_b_neutral

        self.ax.plot([end_b[0], end_a[0]], [end_b[1], end_a[1]], [end_b[2], end_a[2]],
                     color='black', lw=6, alpha=0.9)
        self.ax.scatter(end_a[0], end_a[1], end_a[2], color='red', s=120, label='Tip A')
        self.ax.scatter(end_b[0], end_b[1], end_b[2], color='blue', s=120, label='Tip B')

        limit = self.staff_length / 1.2
        self.ax.set_xlim([-limit, limit])
        self.ax.set_ylim([-limit, limit])
        self.ax.set_zlim([-limit, limit])
        self.ax.set_xlabel('X (Depth)')
        self.ax.set_ylabel('Y (Width)')
        self.ax.set_zlabel('Z (Height)')
        self.ax.grid(True)
        self.canvas.draw()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = BoSimulator()
    window.resize(900, 1000)
    window.show()
    sys.exit(app.exec_())