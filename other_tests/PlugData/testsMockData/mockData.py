import time
from pythonosc import udp_client
import numpy as np

# Setup OSC Client to talk to PlugData
# 127.0.0.1 is your own computer, port 12000 is standard for OSC
client = udp_client.SimpleUDPClient("127.0.0.1", 12000)

def send_mock_bo_data():
	print("Starting Mock Data Stream... Press Ctrl+C to stop.")
	while True:
		# Simulate a slow circular "Spinning" gesture
		t = time.time()

		# Simulated Data for Tip A
		yaw_a = (np.sin(t) * 180)
		pitch_a = (np.cos(t * 0.5) * 45)
		acc_a = abs(np.sin(t * 5)) * 2.0  # Constant movement energy

		# Send as a bundle to PlugData
		# Format: /tipA yaw pitch acc
		client.send_message("/tipA", [yaw_a, pitch_a, acc_a])

		time.sleep(0.001)

		# Tip B is usually the inverse for a rigid staff
		client.send_message("/tipB", [-yaw_a, -pitch_a, acc_a])

		time.sleep(0.02) # 50Hz update rate

if __name__ == "__main__":
	try:
		send_mock_bo_data()
	except KeyboardInterrupt:
		print("\nStream stopped.")