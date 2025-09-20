#!/usr/bin/env python3
"""
Simple HTTP server for testing Xiaozhi Animation Updater
Hosts animation files and provides the required API endpoints
"""

import http.server
import socketserver
import json
import os
import urllib.parse
from datetime import datetime

# Configuration
HOST = "192.168.5.15"
PORT = 8081
ANIMATIONS_DIR = "animations"  # Directory containing your .bin files

class AnimationRequestHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        # Parse the URL and query parameters
        parsed_url = urllib.parse.urlparse(self.path)
        path = parsed_url.path
        query_params = urllib.parse.parse_qs(parsed_url.query)
        
        print(f"[{datetime.now().strftime('%H:%M:%S')}] GET {self.path}")
        
        # Handle API endpoints
        if path.startswith('/api/animations/check'):
            self.handle_check_updates(query_params)
        elif path.startswith('/api/animations/'):
            # Handle direct file downloads
            filename = path.split('/')[-1]
            self.handle_file_download(filename)
        else:
            # Serve static files
            super().do_GET()
    
    def handle_check_updates(self, query_params):
        """Handle /api/animations/check endpoint"""
        device_id = query_params.get('device_id', ['unknown'])[0]
        version = query_params.get('version', ['1.0'])[0]
        
        print(f"  Device ID: {device_id}, Version: {version}")
        
        # Check if animations directory exists and has files
        animations = []
        if os.path.exists(ANIMATIONS_DIR):
            for filename in os.listdir(ANIMATIONS_DIR):
                if filename.endswith('.bin'):
                    file_path = os.path.join(ANIMATIONS_DIR, filename)
                    file_size = os.path.getsize(file_path)
                    download_url = f"http://{HOST}:{PORT}/api/animations/{filename}"
                    
                    animations.append({
                        "filename": filename,
                        "download_url": download_url,
                        "size": file_size,
                        "version": "1.0"
                    })
        
        # Always return updates available for testing
        response = {
            "has_updates": len(animations) > 0,
            "animations": animations,
            "server_time": datetime.now().isoformat(),
            "device_id": device_id,
            "current_version": version
        }
        
        # Send JSON response
        json_response = json.dumps(response, indent=2)
        json_bytes = json_response.encode('utf-8')
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(json_bytes)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        
        self.wfile.write(json_bytes)
        
        print(f"  Response: {len(animations)} animations available")
    
    def handle_file_download(self, filename):
        """Handle direct file downloads"""
        file_path = os.path.join(ANIMATIONS_DIR, filename)
        
        if not os.path.exists(file_path):
            self.send_error(404, f"File not found: {filename}")
            return
        
        # Send file
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Disposition', f'attachment; filename="{filename}"')
        self.send_header('Access-Control-Allow-Origin', '*')
        
        file_size = os.path.getsize(file_path)
        self.send_header('Content-Length', str(file_size))
        self.end_headers()
        
        with open(file_path, 'rb') as f:
            self.wfile.write(f.read())
        
        print(f"  Served file: {filename} ({file_size} bytes)")

def main():
    print("=" * 60)
    print("Xiaozhi Animation Updater Test Server")
    print("=" * 60)
    print(f"Host: {HOST}")
    print(f"Port: {PORT}")
    print(f"Animations Directory: {ANIMATIONS_DIR}")
    print()
    
    # Check if animations directory exists
    if not os.path.exists(ANIMATIONS_DIR):
        print(f"❌ ERROR: {ANIMATIONS_DIR} directory not found!")
        print(f"Please create the directory and place your .bin files there.")
        print(f"Example: mkdir {ANIMATIONS_DIR}")
        return
    
    # List available animation files
    files = [f for f in os.listdir(ANIMATIONS_DIR) if f.endswith('.bin')]
    if files:
        print("📁 Available animation files:")
        for filename in files:
            file_path = os.path.join(ANIMATIONS_DIR, filename)
            size = os.path.getsize(file_path)
            print(f"  ✅ {filename} ({size} bytes)")
    else:
        print(f"❌ No .bin files found in {ANIMATIONS_DIR}/ directory")
        print("Please place your .bin files in the animations/ directory")
        return
    
    print()
    
    # Start the server
    with socketserver.TCPServer((HOST, PORT), AnimationRequestHandler) as httpd:
        print(f"🚀 Server started at http://{HOST}:{PORT}")
        print()
        print("🔗 API Endpoints:")
        print(f"  Check updates: http://{HOST}:{PORT}/api/animations/check?device_id=test&version=1.0")
        print(f"  Download file: http://{HOST}:{PORT}/api/animations/normal1.bin")
        print()
        print("📱 Device Configuration:")
        print(f"  Server URL: http://{HOST}:{PORT}/api/animations")
        print()
        print("Press Ctrl+C to stop the server")
        print("=" * 60)
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n🛑 Server stopped.")

if __name__ == "__main__":
    main()