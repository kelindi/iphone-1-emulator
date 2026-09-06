from http.server import ThreadingHTTPServer,SimpleHTTPRequestHandler
from functools import partial
from threading import Thread,Event
from pathlib import Path
root=Path('.context/jit-continuation/browser')
for port,arm in [(8184,'baseline'),(8185,'candidate')]:
 server=ThreadingHTTPServer(('0.0.0.0',port),partial(SimpleHTTPRequestHandler,directory=str(root/arm)))
 Thread(target=server.serve_forever,daemon=True).start()
 print(port,arm,flush=True)
Event().wait()
