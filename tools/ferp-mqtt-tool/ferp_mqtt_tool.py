#!/usr/bin/env python3
"""
ferp_mqtt_tool.py

CLI tool for sending MQTT commands to a FERP device and reading its responses.

Topic format:
    ferp/{dev-type}/{group}/{device_id}/cmd        send command here
    ferp/{dev-type}/{group}/{device_id}/resp       receive response here
    ferp/{dev-type}/{group}/{device_id}/evt        receive unsolicited events here

Usage examples:
    # Read MQTT config
    python ferp_mqtt_tool.py \\
        --broker broker.emqx.io --port 1883 \\
        --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \\
        --cmd MSG_CONFIG_GET_MQTT

    # Set a config value
    python ferp_mqtt_tool.py \\
        --broker broker.emqx.io --port 1883 \\
        --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \\
        --cmd MSG_CONFIG_SET \\
        --data '{"key":"mqtt_host","value":"broker.local"}'

    # Listen for events (no --cmd means listen-only mode)
    python ferp_mqtt_tool.py \\
        --broker broker.emqx.io --port 1883 \\
        --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \\
        --listen
"""

import argparse
import json
import sys
import time
import threading
import random
import paho.mqtt.client as mqtt

from messages.msg_defs import CMD_MSGS, RESP_MSGS, ALL_MSGS

# ---------------------------------------------------------------------------
# Topic helpers
# ---------------------------------------------------------------------------

def build_topic_base(dev_type: str, group: str, device_id: str) -> str:
    return f"ferp/{dev_type}/{group}/{device_id}"


def cmd_topic(base: str) -> str:
    return f"{base}/cmd"


def resp_topic(base: str) -> str:
    return f"{base}/resp"


def evt_topic(base: str) -> str:
    return f"{base}/evt"


# ---------------------------------------------------------------------------
# Shared state between MQTT callbacks and main thread
# ---------------------------------------------------------------------------

class _State:
    def __init__(self):
        self.connected = threading.Event()
        self.response_received = threading.Event()
        self.last_response: dict | None = None
        self.pending_seq: int | None = None


# ---------------------------------------------------------------------------
# MQTT callbacks
# ---------------------------------------------------------------------------

def _on_connect(client, userdata: _State, flags, rc):
    if rc == 0:
        userdata.connected.set()
    else:
        print(f"[error] Connection refused (rc={rc})", file=sys.stderr)


def _on_message(client, userdata: _State, message):
    try:
        payload = json.loads(message.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        print(f"[warn] Could not decode message on {message.topic}: {exc}", file=sys.stderr)
        return

    # Pretty-print all incoming messages
    topic_suffix = message.topic.split("/")[-1]
    colour = "\033[36m" if topic_suffix == "evt" else "\033[32m"
    reset = "\033[0m"
    print(f"{colour}← [{topic_suffix}]{reset} {json.dumps(payload, indent=2)}")

    # Wake waiting command if sequence matches
    if userdata.pending_seq is not None:
        seq = payload.get("seq")
        if seq == userdata.pending_seq:
            userdata.last_response = payload
            userdata.response_received.set()
        # Also wake on any message with same seq (response msg name differs from command)


# ---------------------------------------------------------------------------
# Core send-and-wait
# ---------------------------------------------------------------------------

def send_command(
    client: mqtt.Client,
    state: _State,
    base_topic: str,
    msg_id: str,
    data: dict,
    timeout_s: float = 10.0,
) -> dict | None:
    seq = random.randint(1, 0xFFFF_FFFF)
    state.pending_seq = seq
    state.response_received.clear()
    state.last_response = None

    payload = {"seq": seq, "msg": msg_id, "data": data}
    payload_str = json.dumps(payload)

    print(f"\033[33m→ [cmd]\033[0m {payload_str}")
    client.publish(cmd_topic(base_topic), payload_str, qos=1)

    if not state.response_received.wait(timeout=timeout_s):
        print(f"[warn] No response within {timeout_s}s", file=sys.stderr)
        return None

    return state.last_response


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="FERP MQTT command tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--broker",    default="broker.emqx.io", help="MQTT broker IP or hostname (default: broker.emqx.io)")
    parser.add_argument("--port",      type=int, default=1883, help="MQTT port (default: 1883)")
    parser.add_argument("--dev-type",  required=True, help="Device type string (e.g. ferp-fuel)")
    parser.add_argument("--group",     required=True, help="Device group (e.g. site_a)")
    parser.add_argument("--device-id", required=True, help="Device ID / MAC (e.g. AA:BB:CC:DD:EE:FF)")
    parser.add_argument("--cmd",       help="Message ID to send (e.g. MSG_CONFIG_GET_MQTT)")
    parser.add_argument("--data",      default="{}", help='JSON data object for the command (default: {})')
    parser.add_argument("--listen",    action="store_true", help="Subscribe and print all events (no command sent)")
    parser.add_argument("--timeout",   type=float, default=10.0, help="Response wait timeout in seconds (default: 10)")
    parser.add_argument("--client-id", default="", help="MQTT client ID (auto-generated if omitted)")
    return parser.parse_args()


def main():
    args = parse_args()

    if not args.cmd and not args.listen:
        print("[error] Specify --cmd <MSG_ID> to send a command, or --listen to watch events.",
              file=sys.stderr)
        sys.exit(1)

    if args.cmd and args.cmd not in CMD_MSGS:
        known = ", ".join(sorted(CMD_MSGS.keys()))
        print(f"[warn] '{args.cmd}' is not in the known command list. Known: {known}")

    try:
        data_obj = json.loads(args.data)
    except json.JSONDecodeError as exc:
        print(f"[error] --data is not valid JSON: {exc}", file=sys.stderr)
        sys.exit(1)

    base = build_topic_base(args.dev_type, args.group, args.device_id)

    state = _State()
    client_id = args.client_id or f"ferp-tool-{random.randint(1000, 9999)}"
    client = mqtt.Client(client_id=client_id)
    client.user_data_set(state)
    client.on_connect = _on_connect
    client.on_message = _on_message

    print(f"Connecting to {args.broker}:{args.port} ...")
    client.connect(args.broker, args.port, keepalive=60)
    client.loop_start()

    if not state.connected.wait(timeout=10.0):
        print("[error] Could not connect to broker within 10s", file=sys.stderr)
        client.loop_stop()
        sys.exit(1)

    print(f"Connected. Base topic: {base}")

    # Subscribe to responses and events
    client.subscribe(resp_topic(base), qos=1)
    client.subscribe(evt_topic(base), qos=0)
    print(f"Subscribed to {resp_topic(base)}")
    print(f"Subscribed to {evt_topic(base)}")

    if args.listen:
        print("Listening for events. Press Ctrl+C to exit.")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
    else:
        response = send_command(client, state, base, args.cmd, data_obj, timeout_s=args.timeout)
        if response is not None:
            msg_name = response.get("msg", "?")
            print(f"\033[32mResponse received: {msg_name}\033[0m")
        else:
            print("\033[31mNo response received.\033[0m")
            sys.exit(2)

    client.loop_stop()
    client.disconnect()


if __name__ == "__main__":
    main()
