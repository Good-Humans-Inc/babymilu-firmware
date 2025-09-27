#!/usr/bin/env python3
"""
WiFi Flow Testing Script
Tests: 1st startup -> connected -> lose wifi -> reconnect another wifi
"""

import json
import time
import serial
import sys

def clear_wifi_via_serial(port='COM3', baudrate=115200):
    """Clear WiFi configuration via serial command"""
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        time.sleep(2)  # Wait for connection
        
        # Send clear WiFi command via serial (if your device supports it)
        ser.write(b'clear_wifi\n')
        time.sleep(1)
        ser.close()
        print(f"✅ WiFi clear command sent via {port}")
    except Exception as e:
        print(f"❌ Serial communication failed: {e}")

def clear_wifi_via_mcp():
    """Clear WiFi configuration via MCP WebSocket"""
    import websocket
    
    def on_message(ws, message):
        print(f"Received: {message}")
    
    def on_error(ws, error):
        print(f"Error: {error}")
    
    def on_close(ws, close_status_code, close_msg):
        print("Connection closed")
    
    def on_open(ws):
        # Send MCP clear WiFi command
        clear_command = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {
                "name": "self.wifi.clear_configuration",
                "arguments": {}
            }
        }
        ws.send(json.dumps(clear_command))
        print("✅ WiFi clear command sent via MCP")
    
    try:
        # Replace with your device's WebSocket URL
        ws_url = "ws://192.168.1.100:8080/ws"
        ws = websocket.WebSocketApp(ws_url,
                                  on_open=on_open,
                                  on_message=on_message,
                                  on_error=on_error,
                                  on_close=on_close)
        ws.run_forever()
    except Exception as e:
        print(f"❌ WebSocket communication failed: {e}")

def main():
    print("🔧 WiFi Flow Testing Helper")
    print("Choose clearing method:")
    print("1. IDF.py erase-flash (most thorough)")
    print("2. IDF.py erase-partition nvs")
    print("3. Serial command (if supported)")
    print("4. MCP WebSocket command")
    print("5. Manual button press")
    
    choice = input("Enter choice (1-5): ")
    
    if choice == "1":
        print("Run: idf.py erase-flash && idf.py build && idf.py flash")
    elif choice == "2":
        print("Run: idf.py --port COM_PORT erase-partition nvs")
    elif choice == "3":
        port = input("Enter serial port (e.g., COM3): ")
        clear_wifi_via_serial(port)
    elif choice == "4":
        clear_wifi_via_mcp()
    elif choice == "5":
        print("Button instructions:")
        print("- Most boards: Single click during startup (when WiFi not connected)")
        print("- Some boards: Long press boot button")
        print("- Some boards: Triple click + long press")
        print("- Check your specific board implementation above")
    
    print("\n🧪 Testing Flow:")
    print("1. Clear WiFi configuration (using method above)")
    print("2. Flash and start device")
    print("3. Device should start nimBLE for WiFi config")
    print("4. Connect via BLE 'Xiaozhi-WiFi' and configure WiFi")
    print("5. Device connects to WiFi and nimBLE stops")
    print("6. Disconnect WiFi router/AP to simulate WiFi loss")
    print("7. Device should automatically start nimBLE for reconfiguration")
    print("8. Configure new WiFi via BLE")
    print("9. Device restarts and connects to new WiFi")

if __name__ == "__main__":
    main()
