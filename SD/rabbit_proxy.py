#!/usr/bin/env python3
"""
IoTaWatt Frontend Test Server with Backend Proxy

This server serves the frontend files locally and proxies API calls
to a remote IoTaWatt device for testing purposes.

Usage:
    python3 rabbit_proxy.py [backend_ip] [port] [frontend_ip]
    
Examples:
    python3 rabbit_proxy.py 192.168.2.50 8000 192.168.2.20
    python3 rabbit_proxy.py 192.168.2.50 8000        # Frontend on all interfaces
    python3 rabbit_proxy.py 192.168.2.50             # Uses port 8000, all interfaces
"""

import sys
import urllib.request
import urllib.parse
from http.server import HTTPServer, SimpleHTTPRequestHandler
import os

class IoTaWattProxyHandler(SimpleHTTPRequestHandler):
    # Backend IoTaWatt device IP
    BACKEND_IP = "192.168.2.50"
    
    def end_headers(self):
        # Add CORS headers
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', '*')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200, "ok")
        self.end_headers()

    def do_GET(self):
        # Check if this is an API call that should be proxied
        if self.should_proxy():
            self.proxy_request()
        else:
            # Serve local files
            super().do_GET()
    
    def do_POST(self):
        # POST requests are always API calls, proxy them
        if self.should_proxy():
            self.proxy_request()
        else:
            self.send_error(404, "Not found")

    def should_proxy(self):
        """Determine if this request should be proxied to the backend device"""
        proxy_paths = [
            '/status',
            '/config',
            '/command', 
            '/auth',
            '/vcal',
            '/esp_spiffs',
            '/iotawatt',
            '/edit'  # Add /edit endpoint for configuration uploads
        ]
        
        # Check if the path starts with any proxy path
        for proxy_path in proxy_paths:
            if self.path.startswith(proxy_path):
                return True
        
        # Also proxy any requests for .txt files (like config.txt, tables.txt)
        if self.path.endswith('.txt'):
            return True
            
        return False

    def proxy_request(self):
        """Proxy the request to the backend IoTaWatt device"""
        try:
            # Build the target URL
            backend_url = f"http://{self.BACKEND_IP}{self.path}"
            print(f"Proxying {self.command} {self.path} -> {backend_url}")
            
            # Prepare request data for POST requests
            data = None
            if self.command == 'POST':
                content_length = int(self.headers.get('Content-Length', 0))
                if content_length > 0:
                    data = self.rfile.read(content_length)
            
            # Create the request
            req = urllib.request.Request(backend_url, data=data, method=self.command)
            
            # Copy relevant headers
            for header_name in ['Content-Type', 'Authorization']:
                if header_name in self.headers:
                    req.add_header(header_name, self.headers[header_name])
            
            # Make the request
            with urllib.request.urlopen(req, timeout=10) as response:
                # Send response
                self.send_response(response.status)
                
                # Copy response headers
                for header_name, header_value in response.headers.items():
                    if header_name.lower() not in ['server', 'date']:
                        self.send_header(header_name, header_value)
                
                self.end_headers()
                
                # Copy response body
                self.wfile.write(response.read())
                
        except urllib.error.HTTPError as e:
            print(f"HTTP Error proxying request: {e.code} {e.reason}")
            self.send_error(e.code, e.reason)
        except urllib.error.URLError as e:
            print(f"URL Error proxying request: {e.reason}")
            self.send_error(502, f"Backend connection failed: {e.reason}")
        except Exception as e:
            print(f"Error proxying request: {e}")
            self.send_error(500, "Internal server error")

    def log_message(self, format, *args):
        """Override to customize logging"""
        print(f"{self.address_string()} - {format % args}")


def main():
    # Parse command line arguments
    backend_ip = "192.168.2.50"  # Default
    port = 8000  # Default
    frontend_ip = "192.168.2.20"  # Default frontend IP
    
    if len(sys.argv) > 1:
        backend_ip = sys.argv[1]
    if len(sys.argv) > 2:
        port = int(sys.argv[2])
    if len(sys.argv) > 3:
        frontend_ip = sys.argv[3]
    else:
        # If no frontend IP specified, bind to all interfaces but show the specific IP in messages
        bind_ip = '0.0.0.0'
    
    # Determine bind IP
    bind_ip = frontend_ip if len(sys.argv) > 3 else '0.0.0.0'
    
    # Update the backend IP in the handler
    IoTaWattProxyHandler.BACKEND_IP = backend_ip
    
    # Change to the SD directory
    sd_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(sd_dir)
    
    print("IoTaWatt Frontend Test Server")
    if bind_ip == '0.0.0.0':
        print(f"Frontend: http://{frontend_ip}:{port} (and any other network interface)")
    else:
        print(f"Frontend: http://{frontend_ip}:{port}")
    print(f"Backend Proxy: {backend_ip}")
    print(f"Serving files from: {sd_dir}")
    print("Press Ctrl+C to stop")
    
    # Start server
    httpd = HTTPServer((bind_ip, port), IoTaWattProxyHandler)
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.shutdown()

if __name__ == "__main__":
    main()
