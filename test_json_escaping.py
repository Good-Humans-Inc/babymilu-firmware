#!/usr/bin/env python3
"""
Test script to verify JSON escaping functionality matches the firmware implementation.
"""

def escape_json_string(content):
    """Escape JSON string content (matches firmware implementation)"""
    escaped_content = ""
    for c in content:
        if c == '"':
            escaped_content += '\\"'
        elif c == '\\':
            escaped_content += '\\\\'
        elif c == '\n':
            escaped_content += '\\n'
        elif c == '\r':
            escaped_content += '\\r'
        elif c == '\t':
            escaped_content += '\\t'
        else:
            escaped_content += c
    return escaped_content

def test_json_escaping():
    """Test JSON escaping with various error log content"""
    
    # Test cases with different special characters
    test_cases = [
        "Simple error message",
        "Error with \"quotes\" in message",
        "Error with \\backslash\\ in message",
        "Error with\nnewline\ncharacters",
        "Error with\ttab\tcharacters",
        "Error with\r\ncarriage return\r\nand newline",
        "Complex error: \"Failed to read file\\nPath: /sdcard/data.txt\\nError: Permission denied\"",
        "Mixed: \"Quote\" and \\backslash\\ and\nnewline and\ttab"
    ]
    
    print("=== Testing JSON Escaping ===")
    
    for i, test_content in enumerate(test_cases, 1):
        print(f"\nTest Case {i}:")
        print(f"Original: {repr(test_content)}")
        
        escaped = escape_json_string(test_content)
        print(f"Escaped:  {repr(escaped)}")
        
        # Test that the escaped content creates valid JSON
        try:
            json_payload = f'{{"error_log_content":"{escaped}","device_id":"AA:BB:CC:DD:EE:FF","client_id":"test-device"}}'
            parsed = json.loads(json_payload)
            print(f"✅ Valid JSON: {parsed['error_log_content'] == test_content}")
        except json.JSONDecodeError as e:
            print(f"❌ Invalid JSON: {e}")
    
    print("\n=== Testing Complete Error Log Content ===")
    
    # Test with realistic error log content
    error_log_content = """E (12345) SYSTEM: Test error message 1
E (12346) WIFI: Connection failed - "Unable to connect to SSID"
E (12347) ANIMATION: Failed to load animation file
E (12348) SD_CARD: Write error on SD card
E (12349) HTTP: Request timeout
E (12350) MEMORY: Out of memory error
E (12351) GPIO: Pin configuration error
E (12352) SPIFFS: File system error
E (12353) OTA: Update failed
E (12354) AUDIO: Codec initialization failed
E (12355) DISPLAY: Screen initialization error"""
    
    print(f"Original error log content:\n{repr(error_log_content)}")
    
    escaped_content = escape_json_string(error_log_content)
    print(f"\nEscaped content:\n{repr(escaped_content)}")
    
    # Create complete JSON payload
    json_payload = f'{{"error_log_content":"{escaped_content}","device_id":"AA:BB:CC:DD:EE:FF","client_id":"xiaozhi-device-001"}}'
    
    print(f"\nComplete JSON payload:\n{json_payload}")
    
    # Verify JSON is valid and content matches
    try:
        parsed = json.loads(json_payload)
        print(f"\n✅ JSON is valid!")
        print(f"✅ Content matches: {parsed['error_log_content'] == error_log_content}")
        print(f"✅ Device ID: {parsed['device_id']}")
        print(f"✅ Client ID: {parsed['client_id']}")
        print(f"✅ Content length: {len(parsed['error_log_content'])} bytes")
    except json.JSONDecodeError as e:
        print(f"\n❌ Invalid JSON: {e}")

if __name__ == "__main__":
    import json
    test_json_escaping()

