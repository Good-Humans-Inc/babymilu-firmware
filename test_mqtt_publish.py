#!/usr/bin/env python3
"""
MQTT Test Publisher Script
Publishes a test message to the device's MQTT subscribe topic.
"""

import argparse
import json
import sys

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt library not installed.")
    print("Install it with: pip install paho-mqtt")
    sys.exit(1)


def publish_test_message(
    broker_host,
    broker_port,
    topic,
    message_type="test",
    message_text="hello world",
    volume=None,
    ota_url=None,
):
    """
    Publish a test message to the MQTT broker.

    Args:
        broker_host: MQTT broker hostname or IP
        broker_port: MQTT broker port
        topic: MQTT topic to publish to
        message_type: Type field for the JSON message
        message_text: Message content
        volume: Volume value (0-100) for adjust_volume type
        ota_url: Custom OTA URL for set_ota_url type
    """
    client = mqtt.Client()

    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"Connected to broker {broker_host}:{broker_port}")
        else:
            print(f"Failed to connect to broker. Return code: {rc}")
            sys.exit(1)

    def on_publish(client, userdata, mid):
        print(f"Message published successfully (mid: {mid})")
        client.disconnect()

    client.on_connect = on_connect
    client.on_publish = on_publish

    payload = {"type": message_type}

    if message_type == "adjust_volume" and volume is not None:
        if volume < 0 or volume > 100:
            print(f"Error: Volume must be between 0-100, got: {volume}")
            sys.exit(1)
        payload["volume"] = volume
        payload["message"] = str(volume)
    elif message_type == "set_ota_url":
        if not ota_url:
            print("Error: --ota-url is required when using --type 'set_ota_url'")
            sys.exit(1)
        payload["message"] = ota_url
    elif message_type == "wifi_clear_credential":
        payload["message"] = ""
    else:
        payload["message"] = message_text

    json_payload = json.dumps(payload)

    print(f"Connecting to broker: {broker_host}:{broker_port}")
    print(f"Topic: {topic}")
    print(f"Payload: {json_payload}")
    print("-" * 60)

    try:
        client.connect(broker_host, broker_port, 60)
        result = client.publish(topic, json_payload, qos=1)

        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print("Publishing message...")
            client.loop_forever()
        else:
            print(f"Failed to publish message. Error code: {result.rc}")
            sys.exit(1)
    except Exception as exc:
        print(f"Error: {exc}")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Publish a test message to device MQTT topic",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Use default values
  python test_mqtt_publish.py

  # Custom MAC address
  python test_mqtt_publish.py --mac "aa:bb:cc:dd:ee:ff"

  # Custom broker
  python test_mqtt_publish.py --broker "192.168.1.100" --port 1883

  # Current broker shortcut
  python test_mqtt_publish.py --current-broker "34.30.176.148"

  # Custom message
  python test_mqtt_publish.py --message "custom test message" --type "custom_type"

  # Trigger remote animation update
  python test_mqtt_publish.py --type "remote_anim_update" --message ""

  # Trigger WiFi reconfiguration via NimBLE (device will reboot)
  python test_mqtt_publish.py --type "wifi_reconfig_nimble" --message "" --mac "90:e5:b1:a8:ad:24"

  # Clear stored WiFi credentials and reboot into BLE onboarding
  python test_mqtt_publish.py --type "wifi_clear_credential" --mac "90:e5:b1:a8:ad:24"

  # Adjust volume to 75
  python test_mqtt_publish.py --type "adjust_volume" --volume 75

  # Set a custom OTA URL and reboot the device
  python test_mqtt_publish.py --type "set_ota_url" --ota-url "http://10.0.0.25:8000/xiaozhi/ota/" --mac "90:e5:b1:a8:ad:24"
        """,
    )

    parser.add_argument(
        "--broker",
        default="136.111.52.199",
        help="MQTT broker hostname or IP (default: 136.111.52.199)",
    )
    parser.add_argument(
        "--current-broker",
        default=None,
        help="Current MQTT broker hostname or IP (overrides --broker if provided)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=1883,
        help="MQTT broker port (default: 1883)",
    )
    parser.add_argument(
        "--mac",
        default="90:e5:b1:ae:8f:54",
        help="Device MAC address for topic (default: 90:e5:b1:ae:8f:54)",
    )
    parser.add_argument(
        "--message",
        default="hello world",
        help="Message text to send (default: 'hello world')",
    )
    parser.add_argument(
        "--type",
        default="test",
        help="Message type field (default: 'test')",
    )
    parser.add_argument(
        "--volume",
        type=int,
        default=None,
        help="Volume value (0-100) for adjust_volume type (default: None)",
    )
    parser.add_argument(
        "--ota-url",
        default=None,
        help="Custom OTA URL for set_ota_url type (default: None)",
    )
    parser.add_argument(
        "--topic",
        default=None,
        help="Full MQTT topic (overrides --mac if provided)",
    )

    args = parser.parse_args()

    if args.current_broker:
        args.broker = args.current_broker

    if args.topic:
        topic = args.topic
    else:
        topic = f"xiaozhi/{args.mac.lower()}/down"

    if args.type == "adjust_volume":
        if args.volume is None:
            print("Error: --volume is required when using --type 'adjust_volume'")
            sys.exit(1)
        if args.volume < 0 or args.volume > 100:
            print(f"Error: Volume must be between 0-100, got: {args.volume}")
            sys.exit(1)

    if args.type == "set_ota_url" and not args.ota_url:
        print("Error: --ota-url is required when using --type 'set_ota_url'")
        sys.exit(1)

    publish_test_message(
        broker_host=args.broker,
        broker_port=args.port,
        topic=topic,
        message_type=args.type,
        message_text=args.message,
        volume=args.volume,
        ota_url=args.ota_url,
    )

    print("-" * 60)
    print("Script completed successfully")
    print("\nCheck device logs for:")
    print("  - MQTT RX topic=... len=... prefix=...")
    if args.type == "adjust_volume":
        print("  - Processing message type: adjust_volume")
        print(f"  - Volume adjusted to {args.volume}")
    elif args.type == "set_ota_url":
        print("  - Processing message type: set_ota_url")
        print(f"  - Custom OTA URL saved to NVS: {args.ota_url}")
        print("  - Device rebooted and will use the custom OTA URL on next startup")
    else:
        print(f"  - Processing message type: {args.type}")


if __name__ == "__main__":
    main()
