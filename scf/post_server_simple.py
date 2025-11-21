#!/usr/bin/env python3

from flask import Flask, request, jsonify
import socket

app = Flask(__name__)

mydict = []

@app.route('/', methods=['POST'])
def handle_post():
    """Handle POST requests."""
    try:
        # Read directly from stream to bypass chunked encoding parsing issues
        content_length = request.headers.get('Content-Length')
        if content_length:
            raw_data = request.stream.read(int(content_length))
            message = raw_data.decode('utf-8')
            mydict.append(message)
            print(message)
        else:
            # If no Content-Length, try to read available data
            raw_data = request.stream.read()
            if raw_data:
                message = raw_data.decode('utf-8')
                mydict.append(message)
                print(message)
            else:
                print("(empty body)")
    except Exception as e:
        print(f"Error reading data: {e}")
    
    return mydict

if __name__ == '__main__':
   app.run(host='0.0.0.0',port=9000)