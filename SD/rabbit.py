from http.server import HTTPServer, SimpleHTTPRequestHandler

class CORSRequestHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        # Add CORS headers
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', '*')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200, "ok")
        self.end_headers()

# Run server
if __name__ == "__main__":
    PORT = 8000
    httpd = HTTPServer(('0.0.0.0', PORT), CORSRequestHandler)
    print(f"Serving with CORS enabled on port {PORT}")
    httpd.serve_forever()

