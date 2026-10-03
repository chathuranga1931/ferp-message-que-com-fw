"""
fake_device.py — minimal FERP device simulator on an MQTT broker, for testing
the web app without hardware.

    python devtools/fake_device.py --broker localhost --group default --id sim001

Answers MsgConfigGetKey / MsgConfigSet / MsgDevInfoRead on .../cmd (echoing the
command seq like ModuleMqtt) and runs the OTA ctrl/data protocol on .../ota/*.
Add a device with MAC "sim001" (group "default") in the web app to talk to it.
"""

import argparse
import json
import struct
import time

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion

STRING, UINT32, BOOL = 1, 0, 2


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--broker", default="localhost")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--dev-type", default="ferp-com")
    ap.add_argument("--group", default="default")
    ap.add_argument("--id", default="sim001")
    args = ap.parse_args()

    base = f"ferp/{args.dev_type}/{args.group}/{args.id}"
    config: dict[int, tuple[int, list[int]]] = {
        0x1001: (STRING, list(b"SimWifi")),
        0x2004: (UINT32, list(struct.pack("<I", 60))),
        0x2003: (BOOL, [1]),
        0x6007: (STRING, list(b"P01")),     # NOZZLE_0_ID
        0x6008: (STRING, list(b"P02")),     # NOZZLE_1_ID
    }
    devinfo = {0xA001: "", 0xA002: args.group, 0xA003: args.id, 0xA004: "9.9.9-sim", 0xA005: "sim-hw", 0xA006: ""}
    ota = {"size": 0, "received": 0}

    c = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2, client_id=f"fake-{args.id}")

    def reply(seq, msg, data):
        c.publish(f"{base}/resp", json.dumps({"seq": seq, "msg": msg, "data": data}), qos=1)

    def ota_reply(payload):
        c.publish(f"{base}/ota/resp", json.dumps(payload), qos=1)

    def on_connect(client, userdata, flags, rc, props=None):
        client.subscribe([(f"{base}/cmd", 1), (f"{base}/ota/ctrl", 1), (f"{base}/ota/data", 0)])
        print(f"fake device up on {base}")

    def on_message(client, userdata, m):
        if m.topic.endswith("/ota/data"):
            off = struct.unpack(">I", m.payload[:4])[0]
            ota["received"] = off + len(m.payload) - 4
            ota_reply({"cmd": "ota_chunk", "status": "ok", "offset_next": ota["received"]})
            return
        p = json.loads(m.payload)
        if m.topic.endswith("/ota/ctrl"):
            cmd = p.get("cmd")
            if cmd == "ota_start":
                ota.update(size=p["data"]["size"], received=0)
            print("ota", cmd, p.get("data"))
            ota_reply({"cmd": cmd, "status": "ok"})
            return
        seq, msg, d = p.get("seq"), p.get("msg"), p.get("data") or {}
        print("cmd", msg, d)
        if msg == "MsgConfigGetKey":
            t, data = config.get(d["key"], (STRING, []))
            reply(seq, "MsgConfigValue", {"key": d["key"], "type": t, "data_size": len(data), "data": data})
        elif msg == "MsgConfigSet":
            config[d["key"]] = (d["type"], list(d["data"]))
        elif msg == "MsgDevInfoRead":
            v = devinfo.get(d["key"], "")
            reply(seq, "MsgDevInfoValue", {"key": d["key"], "type": STRING, "is_valid": bool(v), "value": v})
        else:
            c.publish(f"{base}/evt", json.dumps({"seq": 0, "msg": "MsgTick1000ms", "data": {"echo": msg}}))

    c.on_connect, c.on_message = on_connect, on_message
    c.connect(args.broker, args.port)
    try:
        c.loop_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
