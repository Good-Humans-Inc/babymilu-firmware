from flask import Flask, request
app = Flask(__name__)

# Static URLs to cycle through
STATIC_URLS = [
    "https://gitee.com/xie-hangxuan/test/raw/master/normal1.bin",
    "https://gitee.com/xie-hangxuan/test/raw/master/temp/normal1.bin"
]

# Dictionary to store device_id registrations
device_registrations = {}
url_index = 0  # Counter to cycle through URLs

@app.route('/')
def hello_world():
    global url_index
    
    # Check for different actions via query parameters
    action = request.args.get('action', 'register')
    
    if action == 'status':
        status_text = f"""Status Report:
Total Devices: {len(device_registrations)}
URLs Used: {url_index}
URLs Available: {len(STATIC_URLS) - url_index}
Registered Devices: {', '.join(device_registrations.keys()) if device_registrations else 'None'}"""
        return status_text
    
    elif action == 'reset':
        device_registrations.clear()
        url_index = 0
        return "Registrations reset"
    
    elif action == 'register':
        # Original device registration logic
        device_id = request.args.get('device_id', '')
        
        if not device_id:
            return "No device_id provided"
        
        if device_id in device_registrations:
            return device_registrations[device_id]
        
        if url_index >= len(STATIC_URLS):
            return "url unavailable"
        
        assigned_url = STATIC_URLS[url_index]
        device_registrations[device_id] = assigned_url
        url_index += 1
        
        return assigned_url
    
    else:
        return "Invalid action. Use: register, status, or reset"

if __name__ == '__main__':
   app.run(host='0.0.0.0',port=9000)
