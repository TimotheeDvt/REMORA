import sys
import time
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from PyQt5.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QLabel
from PyQt5.QtCore import Qt, QThread, pyqtSignal, QTimer
from pythonosc import dispatcher, osc_server
from pythonosc import udp_client

class OSCBridge(QThread):
    # We'll use a simple shared state instead of signals for every packet
    # to avoid overwhelming the message queue.
    def __init__(self, state_dict):
        super().__init__()
        self.state = state_dict
        self.forwarder = udp_client.SimpleUDPClient("127.0.0.1", 12001)
        self.last_time = time.time()
        self.alpha = 0.95 # Complementary filter blending factor

    def run(self):
        d = dispatcher.Dispatcher()
        d.map("/esp32/imu", self.handle_imu)

        # Catch absolutely every other OSC message received (like /esp32/connected)
        d.set_default_handler(self.default_handler)

        # Listens on port 8000 for incoming data from any IP using Threading server
        server = osc_server.ThreadingOSCUDPServer(("0.0.0.0", 8000), d)
        print("Listening on port 8000 — move the rod...")
        server.serve_forever()

    def default_handler(self, address, *args):
        print(f"Unmapped -> {address}: {[f'{a:.2f}' if isinstance(a, float) else a for a in args]}")

    def handle_imu(self, address, *args):
        # Uncomment the line below if you want to see the 50Hz raw data in the terminal!
        # print(f"{address}: {[f'{a:.2f}' if isinstance(a, float) else a for a in args]}")
        try:
            ax, ay, az, gx, gy, gz, mx, my, mz = map(float, args)
        except ValueError:
            return

        now = time.time()
        dt = now - self.last_time
        self.last_time = now
        if dt > 0.1: dt = 0.02 # Prevent massive jumps if thread lags

        # 1. Base Tilt from Accelerometer
        acc_roll = np.arctan2(ay, az)
        acc_pitch = np.arctan2(-ax, np.sqrt(ay**2 + az**2))

        # 2. Complementary Filter (Smooths out jitter using Gyroscope)
        new_roll = self.state['roll'] + np.radians(gx) * dt
        new_pitch = self.state['pitch'] + np.radians(gy) * dt

        # Handle wrap-around gracefully (snap to accel if flipped)
        self.state['roll'] = acc_roll if abs(new_roll - acc_roll) > np.pi else (self.alpha * new_roll + (1 - self.alpha) * acc_roll)
        self.state['pitch'] = acc_pitch if abs(new_pitch - acc_pitch) > np.pi else (self.alpha * new_pitch + (1 - self.alpha) * acc_pitch)

        # 3. Simple Yaw from Magnetometer (Tilt-compensated)
        Yh = my * np.cos(acc_roll) - mz * np.sin(acc_roll)
        Xh = mx * np.cos(acc_pitch) + my * np.sin(acc_roll) * np.sin(acc_pitch) + mz * np.cos(acc_roll) * np.sin(acc_pitch)
        self.state['yaw'] = np.arctan2(Yh, Xh)

        # Forward to other software if needed
        self.forwarder.send_message(address, args)

class LiveBoSimulator(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("NIME Bô - High Performance Sync")
        self.staff_length = 4.0

        # Shared thread-safe-ish state
        self.data_state = {'roll': 0.0, 'pitch': 0.0, 'yaw': 0.0}

        # UI Setup
        self.main_widget = QWidget()
        self.setCentralWidget(self.main_widget)
        self.layout = QVBoxLayout(self.main_widget)

        # Plot Setup
        plt.style.use('dark_background')
        self.figure = plt.figure(figsize=(6, 6))
        self.canvas = FigureCanvas(self.figure)
        self.ax = self.figure.add_subplot(111, projection='3d')
        self.layout.addWidget(self.canvas)

        # Pre-create plot elements for FAST real-time updating
        self.staff_line, = self.ax.plot([], [], [], color='white', lw=4)
        self.end_a_point, = self.ax.plot([], [], [], marker='o', color='red', markersize=10, ls='')
        self.end_b_point, = self.ax.plot([], [], [], marker='o', color='cyan', markersize=10, ls='')

        limit = 3
        self.ax.set_xlim([-limit, limit])
        self.ax.set_ylim([-limit, limit])
        self.ax.set_zlim([-limit, limit])
        self.ax.set_axis_off() # Massive speed boost by skipping grid drawing

        # Background OSC Thread
        self.osc_thread = OSCBridge(self.data_state)
        self.osc_thread.start()

        # UI REFRESH TIMER (The Secret Sauce)
        # 16ms = ~60 FPS.
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_plot)
        self.timer.start(16)

    def get_rotation_matrix(self, r, p, y):
        # Angles are already in radians
        Rx = np.array([[1, 0, 0], [0, np.cos(r), -np.sin(r)], [0, np.sin(r), np.cos(r)]])
        Ry = np.array([[np.cos(p), 0, np.sin(p)], [0, 1, 0], [-np.sin(p), 0, np.cos(p)]])
        Rz = np.array([[np.cos(y), -np.sin(y), 0], [np.sin(y), np.cos(y), 0], [0, 0, 1]])
        return Rz @ Ry @ Rx

    def update_plot(self):
        r = self.data_state['roll']
        p = self.data_state['pitch']
        y = self.data_state['yaw']

        rot_matrix = self.get_rotation_matrix(r, p, y)
        end_a = rot_matrix @ np.array([self.staff_length / 2, 0, 0])
        end_b = rot_matrix @ np.array([-self.staff_length / 2, 0, 0])

        # Update properties dynamically instead of clearing plot
        self.staff_line.set_data([end_b[0], end_a[0]], [end_b[1], end_a[1]])
        self.staff_line.set_3d_properties([end_b[2], end_a[2]])

        self.end_a_point.set_data([end_a[0]], [end_a[1]])
        self.end_a_point.set_3d_properties([end_a[2]])

        self.end_b_point.set_data([end_b[0]], [end_b[1]])
        self.end_b_point.set_3d_properties([end_b[2]])

        self.canvas.draw_idle() # Optimized draw call

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = LiveBoSimulator()
    window.show()
    sys.exit(app.exec_())