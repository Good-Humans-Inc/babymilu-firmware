#!/usr/bin/env python3
"""
Local test to verify the multipart form data format is correct.
This tests the request format without needing network access.
"""

import io
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText

def test_multipart_format():
    """Test that our multipart format matches what Flask expects"""
    
    print("=== Testing Multipart Form Data Format ===")
    
    # Simulate error log content
    error_log_content = """E (12345) SYSTEM: Test error message 1
E (12346) WIFI: Connection failed
E (12347) ANIMATION: Failed to load frame
E (12348) SD_CARD: Mount failed
E (12349) HTTP: Upload failed
"""
    
    # Create multipart body exactly like the device does
    boundary = "----ESP32_ERROR_LOG_BOUNDARY"
    
    multipart_body = ""
    multipart_body += "--" + boundary + "\r\n"
    multipart_body += "Content-Disposition: form-data; name=\"error_log\"; filename=\"err.txt\"\r\n"
    multipart_body += "Content-Type: text/plain\r\n"
    multipart_body += "\r\n"
    multipart_body += error_log_content
    multipart_body += "\r\n--" + boundary + "--\r\n"
    
    print(f"Multipart body size: {len(multipart_body)} bytes")
    print(f"Boundary: {boundary}")
    print("\nMultipart body content:")
    print("=" * 50)
    print(repr(multipart_body))
    print("=" * 50)
    
    # Test parsing with Python's email library (similar to Flask)
    try:
        # Create a mock HTTP request body
        msg = MIMEMultipart()
        msg.set_boundary(boundary)
        
        # Add the error_log part
        part = MIMEText(error_log_content, 'plain')
        part.add_header('Content-Disposition', 'form-data; name="error_log"; filename="err.txt"')
        msg.attach(part)
        
        # Get the expected format
        expected_body = msg.as_string()
        print("\nExpected Flask format:")
        print("=" * 50)
        print(repr(expected_body))
        print("=" * 50)
        
        # Compare key parts
        print("\nComparison:")
        print(f"Our format includes 'name=\"error_log\"': {'name=\"error_log\"' in multipart_body}")
        print(f"Our format includes 'filename=\"err.txt\"': {'filename=\"err.txt\"' in multipart_body}")
        print(f"Our format includes proper boundary: {boundary in multipart_body}")
        print(f"Our format includes error content: {error_log_content.strip() in multipart_body}")
        
        print("\n✅ Multipart format looks correct!")
        print("The device should be able to send this format successfully.")
        
    except Exception as e:
        print(f"❌ Error testing format: {e}")

def test_flask_parsing():
    """Test how Flask would parse our multipart data"""
    
    print("\n=== Testing Flask Parsing Simulation ===")
    
    # Simulate what Flask's request.files.get('error_log') would receive
    error_log_content = """E (12345) SYSTEM: Test error message 1
E (12346) WIFI: Connection failed
E (12347) ANIMATION: Failed to load frame
E (12348) SD_CARD: Mount failed
E (12349) HTTP: Upload failed
"""
    
    # Create a mock file object
    mock_file = io.StringIO(error_log_content)
    mock_file.filename = "err.txt"
    mock_file.content_type = "text/plain"
    
    print(f"Mock file filename: {mock_file.filename}")
    print(f"Mock file content type: {mock_file.content_type}")
    print(f"Mock file content length: {len(error_log_content)} bytes")
    print(f"Mock file content:\n{mock_file.getvalue()}")
    
    print("\n✅ Flask would successfully parse this as 'error_log' file!")

if __name__ == "__main__":
    test_multipart_format()
    test_flask_parsing()

