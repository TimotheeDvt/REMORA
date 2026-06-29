from pythonosc import dispatcher, osc_server

def handler(address, *args):
    print(f"{address}: {[f'{a:.2f}' if isinstance(a, float) else a for a in args]}")

d = dispatcher.Dispatcher()
# Catch absolutely every OSC message received
d.set_default_handler(handler)

# Listens on port 8000 for incoming data from any IP
server = osc_server.ThreadingOSCUDPServer(("0.0.0.0", 8000), d)
print("Listening on port 8000 - move the rod...")
server.serve_forever()
