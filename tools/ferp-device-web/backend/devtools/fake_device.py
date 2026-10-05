"""
fake_device.py — minimal FERP device simulator on an MQTT broker, for testing
the web app without hardware.

    python devtools/fake_device.py --broker localhost --group default --id sim001

Answers MsgConfigGetKey / MsgConfigSet / MsgDevInfoRead on .../cmd (echoing the
command seq like ModuleMqtt) and runs the OTA ctrl/data protocol on .../ota/*.
Add a device with MAC "sim001" (group "default") in the web app to talk to it.

    python devtools/fake_device.py --printer --id simprn01

simulates an HSYS receipt printer instead (dev type ferp-printer, receipt
config keys, MsgPrinterGetStatus / MsgPrinterCmd / MsgPrintJob /
MsgCloudPrintGetStatus) — register it with device type "Printer".
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
    ap.add_argument("--printer", action="store_true", help="simulate a receipt printer (ferp-printer)")
    args = ap.parse_args()
    if args.printer and args.dev_type == "ferp-com":
        args.dev_type = "ferp-printer"

    base = f"ferp/{args.dev_type}/{args.group}/{args.id}"
    config: dict[int, tuple[int, list[int]]] = {
        0x1001: (STRING, list(b"SimWifi")),
        0x2004: (UINT32, list(struct.pack("<I", 60))),
        0x2003: (BOOL, [1]),
        0x6007: (STRING, list(b"P01")),     # NOZZLE_0_ID
        0x6008: (STRING, list(b"P02")),     # NOZZLE_1_ID
    }
    if args.printer:
        u32 = lambda n: (UINT32, list(struct.pack("<I", n)))
        config.update({
            0x7101: (STRING, list(b"SIM FILLING STATION")), 0x7102: (STRING, list(b"Lanka IOC")),
            0x7104: (STRING, list(b"No 1, Main Road")), 0x7105: (STRING, list(b"Colombo")),
            0x7107: (STRING, list(b"Tel 011 0000000")), 0x7108: (STRING, list(b"THANK YOU! COME AGAIN!")),
            0x7117: (STRING, list(b"www.myfuelstation.net (0712209310)")),
            0x7112: u32(2), 0x7115: u32(40), 0x710E: u32(2), 0x7114: u32(0), 0x710F: u32(9600), 0x7118: u32(60),
            0x7110: (BOOL, [1]), 0x7111: (BOOL, [0]), 0x7113: (BOOL, [1]), 0x7116: (BOOL, [1]), 0x7119: (BOOL, [1]),
        })
    printer = {"print_count": 392, "jobs_ok": 0, "jobs_failed": 0, "last_job_id": 0}
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
        elif args.printer and msg in ("MsgPrinterGetStatus", "MsgPrinterCmd"):
            if msg == "MsgPrinterCmd":
                cmd = d.get("cmd", 0)
                if cmd == 1: printer["print_count"] = 0
                elif cmd == 0: printer["print_count"] += 1; printer["jobs_ok"] += 1
                elif cmd == 2: printer["jobs_ok"] += 1
            reply(seq, "MsgPrinterStatus", {"transport": "uart", "ready": True, "last_result": 0,
                                            "last_cmd": d.get("cmd", 255) if msg == "MsgPrinterCmd" else 255,
                                            "detail": "UART2 9600 baud (sim)", **printer})
        elif args.printer and msg == "MsgPrintJob":
            time.sleep(1.0)    # printing takes a moment
            if d.get("kind", 0) != 1: printer["print_count"] += 1
            printer["jobs_ok"] += 1; printer["last_job_id"] = d.get("job_id", 0)
            reply(seq, "MsgPrintResult", {"job_id": d.get("job_id", 0), "result": 0, "source": 2,
                                          "print_count": printer["print_count"]})
        elif args.printer and msg == "MsgCloudPrintGetStatus":
            reply(seq, "MsgCloudPrintStatus", {"state": 3, "notify_subscribed": True, "last_http": 400, "polls": 12,
                                               "printed": 3, "failed": 0, "rejected": 0, "queue_id": "print-queue-sim",
                                               "notify_topic": "sim/notify"})
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
