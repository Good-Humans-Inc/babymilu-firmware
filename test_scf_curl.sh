# Simple curl test for SCF endpoint
# Run these commands to test the SCF endpoint response

echo "=== Testing SCF GET Request ==="
curl -v "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/" \
  -H "User-Agent: Xiaozhi-Test/1.0" \
  --max-time 30

echo -e "\n\n=== Testing SCF POST Request ==="
curl -v "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/" \
  -X POST \
  -H "Content-Type: application/json" \
  -H "User-Agent: Xiaozhi-Test/1.0" \
  -d '{"message":"hello world","test":true}' \
  --max-time 30

echo -e "\n\n=== Testing SCF POST with Error Log Format ==="
curl -v "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/" \
  -X POST \
  -H "Content-Type: application/json" \
  -H "User-Agent: Xiaozhi-Test/1.0" \
  -d '{"error_log_content":"E (12345) SYSTEM: hello world test","device_id":"AA:BB:CC:DD:EE:FF","client_id":"xiaozhi-device-001"}' \
  --max-time 30

