from flask import Flask, request
app = Flask(__name__)

# Static URLs to cycle through (version 1.0.1)
STATIC_URLS = [
    "https://gitee.com/xie-hangxuan/test/raw/master/animations_mega.bin",
    "https://gitee.com/xie-hangxuan/test/raw/master/animations_mega.bin"
]

# Server versions to alternate between
server_versions = ["1.0.1", "1.0.2"]
current_version_index = 1  # Start with 1.0.2 (index 1)
server_version = server_versions[current_version_index]

# Dictionary to store device_id registrations
device_registrations = {}
url_index = 0  # Counter to cycle through URLs

@app.route('/')
def hello_world():
    global url_index, server_version, current_version_index
    
    # Check for different actions via query parameters
    action = request.args.get('action', 'register')
    
    if action == 'status':
        status_text = f"""Status Report:
Server Version: {server_version}
Total Devices: {len(device_registrations)}
URLs Used: {url_index}
URLs Available: {len(STATIC_URLS) - url_index}
Registered Devices: {', '.join(device_registrations.keys()) if device_registrations else 'None'}"""
        return status_text
    
    elif action == 'reset':
        device_registrations.clear()
        url_index = 0
        # Switch to the other server version
        current_version_index = 1 - current_version_index  # Toggle between 0 and 1
        server_version = server_versions[current_version_index]
        return f"Registrations reset. Server version switched to {server_version}"
    
    elif action == 'register':
        # Device registration with version checking
        device_id = request.args.get('device_id', '')
        device_version = request.args.get('version', '1.0.0')  # Default to 1.0.0 if not provided
        
        if not device_id:
            return "No device_id provided"
        
        # Check if device version is up to date
        if device_version == server_version:
            # Device is up to date, return empty response
            return ""
        
        # Device needs update, check if already registered
        if device_id in device_registrations:
            return device_registrations[device_id]
        
        if url_index >= len(STATIC_URLS):
            return "url unavailable"
        
        assigned_url = STATIC_URLS[url_index]
        device_registrations[device_id] = assigned_url
        url_index += 1
        
        # For first registration, return both URL and server version
        return f"{assigned_url},{server_version}"
    
    else:
        return "Invalid action. Use: register, status, or reset"

if __name__ == '__main__':
   app.run(host='0.0.0.0',port=9000)
