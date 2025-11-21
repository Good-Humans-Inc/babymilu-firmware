#!/usr/bin/env python3
"""
Simple server to receive GET and POST requests.
"""

from flask import Flask, request, jsonify
import socket

app = Flask(__name__)


@app.route('/', methods=['GET'])
def handle_get():
    """Handle GET requests."""
    return jsonify({"message": "GET request received", "method": "GET"}), 200


@app.route('/', methods=['POST'])
def handle_post():
    """Handle POST requests."""
    try:
        # Read directly from stream to bypass chunked encoding parsing issues
        content_length = request.headers.get('Content-Length')
        if content_length:
            raw_data = request.stream.read(int(content_length))
            message = raw_data.decode('utf-8')
            print(message)
        else:
            # If no Content-Length, try to read available data
            raw_data = request.stream.read()
            if raw_data:
                message = raw_data.decode('utf-8')
                print(message)
            else:
                print("(empty body)")
    except Exception as e:
        print(f"Error reading data: {e}")
    
    return jsonify({"message": "POST request received", "method": "POST"}), 200


def get_local_ip():
    """Get local IPv4 address."""
    try:
        # Connect to a remote address to determine local IP
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"


if __name__ == '__main__':
    local_ip = get_local_ip()
    print(f"\nServer running on:")
    print(f"  Local:   http://localhost:5000/")
    print(f"  Network: http://{local_ip}:5000/")
    print()
    
    app.run(host='0.0.0.0', port=5000, debug=True)
