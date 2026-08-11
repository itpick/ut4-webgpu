import http.server, socketserver, sys
os_dir = sys.argv[2] if len(sys.argv) > 2 else '.'
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=os_dir, **k)
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Resource-Policy', 'cross-origin')
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()
    def guess_type(self, path):
        if path.endswith('.wasm'): return 'application/wasm'
        if path.endswith('.js'): return 'text/javascript'
        return super().guess_type(path)
    def log_message(self, fmt, *a):
        # Observability: log every request (esp. 404s) with a real timestamp
        # instead of swallowing it -- needed to diagnose "Failed to load
        # resource: 404" console lines, which don't carry the URL on their own.
        sys.stderr.write("%s - - [%s] %s\n" % (self.address_string(), self.log_date_time_string(), fmt % a))
        sys.stderr.flush()
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8799
socketserver.ThreadingTCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(('127.0.0.1', port), H) as httpd:
    print('serving on 127.0.0.1:%d from %s' % (port, os_dir), flush=True)
    httpd.serve_forever()
