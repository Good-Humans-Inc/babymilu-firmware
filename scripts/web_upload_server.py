#!/usr/bin/env python3
"""
Simple web server for uploading images from phone to device
Run this on your computer, then access from phone browser
"""

import http.server
import socketserver
import urllib.parse
import json
import os
import sys
import requests
from PIL import Image
import io
import struct

class UploadHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/':
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            
            html = """
            <!DOCTYPE html>
            <html>
            <head>
                <title>Xiaozhi Animation Uploader</title>
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <style>
                    body { font-family: Arial, sans-serif; margin: 20px; }
                    .container { max-width: 500px; margin: 0 auto; }
                    .form-group { margin: 15px 0; }
                    label { display: block; margin-bottom: 5px; font-weight: bold; }
                    input, select { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; }
                    button { background: #007bff; color: white; padding: 12px 20px; border: none; border-radius: 4px; cursor: pointer; width: 100%; }
                    button:hover { background: #0056b3; }
                    .status { margin: 10px 0; padding: 10px; border-radius: 4px; }
                    .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
                    .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
                    .info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; }
                </style>
            </head>
            <body>
                <div class="container">
                    <h1>🎭 Xiaozhi Animation Uploader</h1>
                    
                    <form id="uploadForm" enctype="multipart/form-data">
                        <div class="form-group">
                            <label for="deviceIp">Device IP Address:</label>
                            <input type="text" id="deviceIp" placeholder="192.168.1.100" required>
                        </div>
                        
                        <div class="form-group">
                            <label for="imageFile">Select Image:</label>
                            <input type="file" id="imageFile" accept="image/*" required>
                        </div>
                        
                        <div class="form-group">
                            <label for="filename">Animation Filename:</label>
                            <input type="text" id="filename" placeholder="my_animation.bin" required>
                        </div>
                        
                        <div class="form-group">
                            <label for="width">Width (pixels):</label>
                            <input type="number" id="width" value="64" min="16" max="256">
                        </div>
                        
                        <div class="form-group">
                            <label for="height">Height (pixels):</label>
                            <input type="number" id="height" value="64" min="16" max="256">
                        </div>
                        
                        <button type="submit">Upload Animation</button>
                    </form>
                    
                    <div id="status"></div>
                    
                    <div class="info">
                        <h3>📱 How to use:</h3>
                        <ol>
                            <li>Enter your device's IP address</li>
                            <li>Select an image from your phone</li>
                            <li>Choose a filename for the animation</li>
                            <li>Set desired dimensions (64x64 recommended)</li>
                            <li>Tap "Upload Animation"</li>
                        </ol>
                    </div>
                </div>
                
                <script>
                    document.getElementById('uploadForm').addEventListener('submit', async function(e) {
                        e.preventDefault();
                        
                        const deviceIp = document.getElementById('deviceIp').value;
                        const imageFile = document.getElementById('imageFile').files[0];
                        const filename = document.getElementById('filename').value;
                        const width = parseInt(document.getElementById('width').value);
                        const height = parseInt(document.getElementById('height').value);
                        
                        const statusDiv = document.getElementById('status');
                        statusDiv.innerHTML = '<div class="info">Converting and uploading...</div>';
                        
                        try {
                            const formData = new FormData();
                            formData.append('image', imageFile);
                            formData.append('filename', filename);
                            formData.append('width', width);
                            formData.append('height', height);
                            
                            const response = await fetch('/convert_and_upload', {
                                method: 'POST',
                                body: formData
                            });
                            
                            const result = await response.json();
                            
                            if (result.success) {
                                statusDiv.innerHTML = '<div class="success">✅ Upload successful! Animation saved as ' + filename + '</div>';
                            } else {
                                statusDiv.innerHTML = '<div class="error">❌ Upload failed: ' + result.error + '</div>';
                            }
                        } catch (error) {
                            statusDiv.innerHTML = '<div class="error">❌ Error: ' + error.message + '</div>';
                        }
                    });
                </script>
            </body>
            </html>
            """
            
            self.wfile.write(html.encode())
        else:
            super().do_GET()
    
    def do_POST(self):
        if self.path == '/convert_and_upload':
            try:
                # Parse form data
                content_length = int(self.headers['Content-Length'])
                post_data = self.rfile.read(content_length)
                
                # Simple form parsing (in production, use proper library)
                boundary = self.headers['Content-Type'].split('boundary=')[1]
                parts = post_data.split(b'--' + boundary.encode())
                
                form_data = {}
                image_data = None
                
                for part in parts:
                    if b'Content-Disposition: form-data' in part:
                        if b'name="image"' in part:
                            # Extract image data
                            header_end = part.find(b'\r\n\r\n')
                            if header_end != -1:
                                image_data = part[header_end + 4:]
                        elif b'name="filename"' in part:
                            header_end = part.find(b'\r\n\r\n')
                            if header_end != -1:
                                form_data['filename'] = part[header_end + 4:].decode().strip()
                        elif b'name="width"' in part:
                            header_end = part.find(b'\r\n\r\n')
                            if header_end != -1:
                                form_data['width'] = int(part[header_end + 4:].decode().strip())
                        elif b'name="height"' in part:
                            header_end = part.find(b'\r\n\r\n')
                            if header_end != -1:
                                form_data['height'] = int(part[header_end + 4:].decode().strip())
                
                if not image_data or 'filename' not in form_data:
                    self.send_error(400, "Missing required data")
                    return
                
                # Convert image to animation format
                animation_data = convert_image_to_animation_data(
                    image_data, 
                    form_data.get('width', 64), 
                    form_data.get('height', 64)
                )
                
                if not animation_data:
                    self.send_error(500, "Image conversion failed")
                    return
                
                # Upload to device
                device_ip = "192.168.1.100"  # You'll need to get this from the form
                success = upload_to_device(device_ip, form_data['filename'], animation_data)
                
                # Send response
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self.end_headers()
                
                response = {
                    'success': success,
                    'error': 'Upload failed' if not success else None
                }
                
                self.wfile.write(json.dumps(response).encode())
                
            except Exception as e:
                self.send_error(500, f"Server error: {str(e)}")
        else:
            self.send_error(404, "Not found")

def convert_image_to_animation_data(image_data, width, height):
    """Convert image data to animation format"""
    try:
        # Open image from bytes
        img = Image.open(io.BytesIO(image_data))
        if img.mode != 'RGB':
            img = img.convert('RGB')
        
        # Resize
        img = img.resize((width, height), Image.Resampling.LANCZOS)
        
        # Create header
        magic = 0x4C56474C
        color_format = 0x0B
        flags = 0x00
        stride = width * 2
        
        header = struct.pack('<IIIIII', magic, color_format, flags, width, height, stride)
        
        # Convert to RGB565
        frame_data = bytearray()
        pixels = img.getdata()
        
        for pixel in pixels:
            r, g, b = pixel
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            frame_data.extend(struct.pack('<H', rgb565))
        
        return header + frame_data
        
    except Exception as e:
        print(f"Image conversion error: {e}")
        return None

def upload_to_device(device_ip, filename, animation_data):
    """Upload animation data to device"""
    try:
        url = f"http://{device_ip}:8080/upload"
        params = {"filename": filename}
        
        response = requests.post(url, params=params, data=animation_data, timeout=30)
        
        if response.status_code == 200:
            result = response.json()
            return result.get('success', False)
        else:
            print(f"Upload failed: {response.status_code} - {response.text}")
            return False
            
    except Exception as e:
        print(f"Upload error: {e}")
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python web_upload_server.py <port>")
        print("Example: python web_upload_server.py 8000")
        print("Then access http://<your_computer_ip>:8000 from your phone")
        sys.exit(1)
    
    port = int(sys.argv[1])
    
    with socketserver.TCPServer(("", port), UploadHandler) as httpd:
        print(f"🌐 Web upload server running on port {port}")
        print(f"📱 Access from phone: http://<your_computer_ip>:{port}")
        print(f"💡 Find your computer IP with: ipconfig (Windows) or ifconfig (Mac/Linux)")
        print("Press Ctrl+C to stop")
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n👋 Server stopped")

if __name__ == "__main__":
    main()
