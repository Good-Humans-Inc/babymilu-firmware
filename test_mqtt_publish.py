#!/usr/bin/env python3
"""
MQTT Test Publisher Script
Publishes a "hello world" test message to the device's MQTT subscribe topic.
"""

import json
import sys
import argparse
try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt library not installed.")
    print("Install it with: pip install paho-mqtt")
    sys.exit(1)


def publish_test_message(broker_host, broker_port, topic, message_type="test", message_text="hello world", volume=None):
    """
    Publish a test message to the MQTT broker.
    
    Args:
        broker_host: MQTT broker hostname or IP
        broker_port: MQTT broker port
        topic: MQTT topic to publish to
        message_type: Type field for the JSON message
        message_text: Message content
        volume: Volume value (0-100) for adjust_volume type
    """
    # Create MQTT client
    client = mqtt.Client()
    
    # Set up callbacks
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"✓ Connected to broker {broker_host}:{broker_port}")
        else:
            print(f"✗ Failed to connect to broker. Return code: {rc}")
            sys.exit(1)
    
    def on_publish(client, userdata, mid):
        print(f"✓ Message published successfully (mid: {mid})")
        client.disconnect()
    
    client.on_connect = on_connect
    client.on_publish = on_publish
    
    # Create JSON message
    payload = {
        "type": message_type
    }
    
    # For adjust_volume type, use volume field (preferred) or message field (fallback)
    if message_type == "adjust_volume" and volume is not None:
        if volume < 0 or volume > 100:
            print(f"✗ Error: Volume must be between 0-100, got: {volume}")
            sys.exit(1)
        payload["volume"] = volume
        payload["message"] = str(volume)  # Also include in message for compatibility
    else:
        payload["message"] = message_text
    
    json_payload = json.dumps(payload)
    
    print(f"Connecting to broker: {broker_host}:{broker_port}")
    print(f"Topic: {topic}")
    print(f"Payload: {json_payload}")
    print("-" * 60)
    
    try:
        # Connect to broker
        client.connect(broker_host, broker_port, 60)
        
        # Publish message
        result = client.publish(topic, json_payload, qos=1)
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"Publishing message...")
            # Wait for publish callback
            client.loop_forever()
        else:
            print(f"✗ Failed to publish message. Error code: {result.rc}")
            sys.exit(1)
            
    except Exception as e:
        print(f"✗ Error: {e}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Publish a test message to device MQTT topic",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Use default values from OTA response
  python test_mqtt_publish.py
  
  # Custom MAC address
  python test_mqtt_publish.py --mac "aa:bb:cc:dd:ee:ff"
  
  # Custom broker
  python test_mqtt_publish.py --broker "192.168.1.100" --port 1883
  
  # Custom message
  python test_mqtt_publish.py --message "custom test message" --type "custom_type"
  
  # Trigger remote animation update
  python test_mqtt_publish.py --type "remote_anim_update" --message ""
  
  # Adjust volume to 50
  python test_mqtt_publish.py --type "adjust_volume" --volume 50
  
  # Adjust volume to 75
  python test_mqtt_publish.py --type "adjust_volume" --volume 75
        """
    )
    
    parser.add_argument(
        "--broker",
        default="34.30.176.148",
        help="MQTT broker hostname or IP (default: 34.30.176.148)"
    )
    
    parser.add_argument(
        "--port",
        type=int,
        default=1883,
        help="MQTT broker port (default: 1883)"
    )
    
    parser.add_argument(
        "--mac",
        default="90:e5:b1:ae:8f:54",
        help="Device MAC address for topic (default: 90:e5:b1:ae:8f:54)"
    )
    
    parser.add_argument(
        "--message",
        default="hello world",
        help="Message text to send (default: 'hello world')"
    )
    
    parser.add_argument(
        "--type",
        default="test",
        help="Message type field (default: 'test')"
    )
    
    parser.add_argument(
        "--volume",
        type=int,
        default=None,
        help="Volume value (0-100) for adjust_volume type (default: None)"
    )
    
    parser.add_argument(
        "--topic",
        default=None,
        help="Full MQTT topic (overrides --mac if provided)"
    )
    
    args = parser.parse_args()
    
    # Build topic from MAC if not provided
    if args.topic:
        topic = args.topic
    else:
        # Ensure MAC is lowercase
        mac = args.mac.lower()
        topic = f"xiaozhi/{mac}/down"
    
    # Validate volume if adjust_volume type is used
    if args.type == "adjust_volume":
        if args.volume is None:
            print("✗ Error: --volume is required when using --type 'adjust_volume'")
            sys.exit(1)
        if args.volume < 0 or args.volume > 100:
            print(f"✗ Error: Volume must be between 0-100, got: {args.volume}")
            sys.exit(1)
    
    # Publish the message
    publish_test_message(
        broker_host=args.broker,
        broker_port=args.port,
        topic=topic,
        message_type=args.type,
        message_text=args.message,
        volume=args.volume
    )
    
    print("-" * 60)
    print("✓ Script completed successfully")
    print("\nCheck device logs for:")
    print("  - MQTT RX topic=... len=... prefix=...")
    if args.type == "adjust_volume":
        print(f"  - Processing message type: adjust_volume")
        print(f"  - Volume adjusted to {args.volume}")
    else:
        print(f"  - Processing message type: {args.type}")


if __name__ == "__main__":
    main()

