from flask import Flask, request
import json
from datetime import datetime

app = Flask(__name__)

# Global variables to store logs and error data
last_error_log = ""
request_logs = []
post_request_count = 0  # Simple counter for POST requests received

def log_message(message):
    """Add a message to the request logs and also print it
    Note: Only POST requests with error_log files are logged to request_logs
    """
    global request_logs
    request_logs.append(message)
    print(message)

@app.route('/', methods=['POST'])
def handle_post():
    global last_error_log, request_logs, post_request_count
    
    # Increment POST request counter immediately
    post_request_count += 1
    
    # Send immediate response first to avoid timeout
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # Check if this is JSON data
    if request.content_type and 'application/json' in request.content_type:
        try:
            # Parse JSON data
            json_data = request.get_json()
            if json_data:
                # Check if this is an error log upload
                if 'error_log_content' in json_data:
                    # Store the error log content globally
                    last_error_log = json_data['error_log_content']
                    device_id = json_data.get('device_id', 'unknown')
                    
                    # Send immediate plain text response for error log upload
                    response_text = f"ERROR_LOG_UPLOAD_SUCCESS - File size: {len(json_data['error_log_content'])} bytes, Device: {device_id}, Time: {timestamp}"
                    
                    response = app.response_class(
                        response=response_text,
                        status=200,
                        mimetype='text/plain',
                        headers={
                            'Connection': 'close',
                            'Content-Type': 'text/plain',
                            'Cache-Control': 'no-cache'
                        }
                    )
                    
                    # Log after sending response to avoid delay
                    request_logs.append(f"=== POST Request Received at {timestamp} ===")
                    request_logs.append(f"Request Type: ERROR LOG UPLOAD (JSON from firmware)")
                    request_logs.append(f"Device ID: {device_id}")
                    request_logs.append(f"Error log content size: {len(json_data['error_log_content'])} bytes")
                    request_logs.append(f"=== Received error log content ===")
                    request_logs.append(json_data['error_log_content'])
                    request_logs.append("=== End error log content ===")
                    request_logs.append(f"=== Sending plain text response ===")
                    request_logs.append(f"Response: {response_text}")
                    
                    return response
                
                else:
                    # This is a general JSON message (like "hello world" test)
                    # Send immediate plain text response for simple messages
                    response_text = f"GENERAL_MESSAGE_SUCCESS - Received: {json_data.get('message', 'unknown')}, Time: {timestamp}"
                    
                    response = app.response_class(
                        response=response_text,
                        status=200,
                        mimetype='text/plain',
                        headers={
                            'Connection': 'close',
                            'Content-Type': 'text/plain',
                            'Cache-Control': 'no-cache'
                        }
                    )
                    
                    # Log after sending response to avoid delay
                    request_logs.append(f"=== POST Request Received at {timestamp} ===")
                    request_logs.append("Request Type: GENERAL JSON MESSAGE")
                    request_logs.append(f"JSON data received: {json_data}")
                    request_logs.append(f"=== Sending plain text response ===")
                    request_logs.append(f"Response: {response_text}")
                    
                    return response
                
        except Exception as e:
            error_msg = f"ERROR processing JSON request: {str(e)}"
            response_text = f"ERROR - {error_msg}"
            
            response = app.response_class(
                response=response_text,
                status=500,
                mimetype='text/plain',
                headers={
                    'Connection': 'close',
                    'Content-Type': 'text/plain',
                    'Cache-Control': 'no-cache'
                }
            )
            
            # Log after sending response
            request_logs.append(f"=== POST Request Received at {timestamp} ===")
            request_logs.append(f"ERROR: {error_msg}")
            
            return response
    
    # Check if this is an error log upload (has error_log file) - legacy support
    error_log_file = request.files.get('error_log')
    
    if error_log_file:
        try:
            # Read error log content
            error_content = error_log_file.read().decode('utf-8')
            
            # Store the error log content globally
            last_error_log = error_content
            
            # Send immediate plain text response
            response_text = f"ERROR_LOG_UPLOAD_SUCCESS - File size: {len(error_content)} bytes, Time: {timestamp}"
            
            response = app.response_class(
                response=response_text,
                status=200,
                mimetype='text/plain',
                headers={
                    'Connection': 'close',
                    'Content-Type': 'text/plain',
                    'Cache-Control': 'no-cache'
                }
            )
            
            # Log after sending response
            request_logs.append(f"=== POST Request Received at {timestamp} ===")
            request_logs.append("Request Type: ERROR LOG UPLOAD (legacy file upload)")
            request_logs.append(f"File received: {error_log_file.filename}, size: {error_log_file.content_length}")
            request_logs.append("=== Received error log content ===")
            request_logs.append(error_content)
            request_logs.append("=== End error log content ===")
            request_logs.append(f"=== Sending plain text response ===")
            request_logs.append(f"Response: {response_text}")
            
            return response
            
        except Exception as e:
            error_msg = f"ERROR processing error log: {str(e)}"
            response_text = f"ERROR - {error_msg}"
            
            response = app.response_class(
                response=response_text,
                status=500,
                mimetype='text/plain',
                headers={
                    'Connection': 'close',
                    'Content-Type': 'text/plain',
                    'Cache-Control': 'no-cache'
                }
            )
            
            # Log after sending response
            request_logs.append(f"=== POST Request Received at {timestamp} ===")
            request_logs.append(f"ERROR: {error_msg}")
            
            return response
    
    else:
        # Regular POST without error_log file
        response_text = f"REGULAR_POST_SUCCESS - Files: {list(request.files.keys())}, Time: {timestamp}"
        
        response = app.response_class(
            response=response_text,
            status=200,
            mimetype='text/plain',
            headers={
                'Connection': 'close',
                'Content-Type': 'text/plain',
                'Cache-Control': 'no-cache'
            }
        )
        
        # Log after sending response
        request_logs.append(f"=== POST Request Received at {timestamp} ===")
        request_logs.append("Request Type: REGULAR POST")
        request_logs.append(f"Files received: {list(request.files.keys())}")
        request_logs.append(f"Form data: {dict(request.form)}")
        request_logs.append(f"=== Sending plain text response ===")
        request_logs.append(f"Response: {response_text}")
        
        return response

@app.route('/simple', methods=['POST'])
def handle_simple_post():
    """Simple text-based endpoint for basic POST requests"""
    global last_error_log, request_logs
    
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    log_message(f"=== SIMPLE POST Request Received at {timestamp} ===")
    log_message(f"Content-Type: {request.content_type}")
    log_message(f"Content-Length: {request.content_length}")
    
    # Check if this is JSON data
    if request.content_type and 'application/json' in request.content_type:
        try:
            json_data = request.get_json()
            if json_data:
                log_message(f"JSON data received: {json_data}")
                
                # Store any error log content if present
                if 'error_log_content' in json_data:
                    last_error_log = json_data['error_log_content']
                    log_message("Error log content stored")
                
                # Return simple text response
                response_text = f"OK - Request processed at {timestamp}"
                log_message(f"Returning simple text response: {response_text}")
                
                response = app.response_class(
                    response=response_text,
                    status=200,
                    mimetype='text/plain',
                    headers={
                        'Connection': 'close',
                        'Content-Type': 'text/plain',
                        'Cache-Control': 'no-cache'
                    }
                )
                return response
        except Exception as e:
            error_msg = f"ERROR processing simple request: {str(e)}"
            log_message(error_msg)
            response = app.response_class(
                response=f"ERROR: {error_msg}",
                status=500,
                mimetype='text/plain',
                headers={
                    'Connection': 'close',
                    'Content-Type': 'text/plain',
                    'Cache-Control': 'no-cache'
                }
            )
            return response
    
    # Default response for non-JSON requests
    response_text = f"OK - Simple POST received at {timestamp}"
    log_message(f"Returning default simple response: {response_text}")
    
    response = app.response_class(
        response=response_text,
        status=200,
        mimetype='text/plain',
        headers={
            'Connection': 'close',
            'Content-Type': 'text/plain',
            'Cache-Control': 'no-cache'
        }
    )
    return response

@app.route('/', methods=['GET'])
def get_error_log():
    global last_error_log, request_logs, post_request_count
    
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # Check if this is a GET request with message data (wild approach!)
    message_header = request.headers.get('X-Message', '')
    test_header = request.headers.get('X-Test', '')
    error_log_header = request.headers.get('X-Error-Log', '')
    device_id_header = request.headers.get('X-Device-Id', '')
    
    # Also check query parameters as fallback
    message_param = request.args.get('message', '')
    test_param = request.args.get('test', '')
    error_log_param = request.args.get('error_log', '')
    device_id_param = request.args.get('device_id', '')
    
    # Use headers first, then query parameters
    message = message_header or message_param
    test = test_header or test_param
    error_log_content = error_log_header or error_log_param
    device_id = device_id_header or device_id_param
    
    # If we have message data, process it like a POST request
    if message or error_log_content:
        # Increment counter for GET requests with data
        post_request_count += 1
        
        if error_log_content:
            # Store error log content
            last_error_log = error_log_content
            
            # Log after sending response
            request_logs.append(f"=== GET Request with Error Log at {timestamp} ===")
            request_logs.append(f"Device ID: {device_id}")
            request_logs.append(f"Error log content size: {len(error_log_content)} bytes")
            request_logs.append("=== Received error log content ===")
            request_logs.append(error_log_content)
            request_logs.append("=== End error log content ===")
            
            response_text = f"ERROR_LOG_UPLOAD_SUCCESS - File size: {len(error_log_content)} bytes, Device: {device_id}, Time: {timestamp}"
            request_logs.append(f"=== Sending plain text response ===")
            request_logs.append(f"Response: {response_text}")
            
            response = app.response_class(
                response=response_text,
                status=200,
                mimetype='text/plain',
                headers={
                    'Connection': 'close',
                    'Content-Type': 'text/plain',
                    'Cache-Control': 'no-cache'
                }
            )
            return response
            
        elif message:
            # Log after sending response
            request_logs.append(f"=== GET Request with Message at {timestamp} ===")
            request_logs.append(f"Message: {message}")
            request_logs.append(f"Test: {test}")
            
            response_text = f"GENERAL_MESSAGE_SUCCESS - Received: {message}, Test: {test}, Time: {timestamp}"
            request_logs.append(f"=== Sending plain text response ===")
            request_logs.append(f"Response: {response_text}")
            
            response = app.response_class(
                response=response_text,
                status=200,
                mimetype='text/plain',
                headers={
                    'Connection': 'close',
                    'Content-Type': 'text/plain',
                    'Cache-Control': 'no-cache'
                }
            )
            return response
    
    # Regular GET request behavior
    # Check for action parameter in query string
    action = request.args.get('action', '').lower()
    
    if action == 'clear':
        # Clear all data
        last_error_log = ""
        request_logs = []
        post_request_count = 0  # Reset counter too
        
        response_data = {
            "success": True,
            "message": "All logs and error data cleared successfully",
            "action": "clear",
            "timestamp": timestamp,
            "post_request_count": post_request_count
        }
        
        return json.dumps(response_data), 200
    
    elif action == 'get_logs':
        response_data = {
            "success": True,
            "message": "Logs retrieved successfully",
            "action": "get_logs",
            "timestamp": timestamp,
            "post_request_count": post_request_count,
            "error_log": {
                "has_content": bool(last_error_log),
                "content": last_error_log if last_error_log else "No error log uploaded yet",
                "file_size": len(last_error_log) if last_error_log else 0
            },
            "request_logs": request_logs if request_logs else ["No request logs available yet"],
            "total_logs": len(request_logs)
        }
        
        return json.dumps(response_data, indent=2), 200
    
    else:
        # Default behavior - return all data
        response_data = {
            "success": True,
            "message": "Data retrieved successfully",
            "timestamp": timestamp,
            "post_request_count": post_request_count,
            "error_log": {
                "has_content": bool(last_error_log),
                "content": last_error_log if last_error_log else "No error log uploaded yet",
                "file_size": len(last_error_log) if last_error_log else 0
            },
            "request_logs": request_logs if request_logs else ["No request logs available yet"],
            "total_logs": len(request_logs)
        }
        
        return json.dumps(response_data, indent=2), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=9000)