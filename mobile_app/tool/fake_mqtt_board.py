#!/usr/bin/env python3
"""Fake face-unlock board on an MQTT broker (docs/mqtt_contract.md), so the
app's MQTT side can be tested without hardware.

  Enter     a known face asks for approval (topic approval-request)
  q Enter   quit (publishes status "offline")

It publishes status "online", checks each answer (request id, and the HMAC
signature when --phone-key is given) and replies with an event:
door_opened, approval_denied or approval_timeout.

  pip install paho-mqtt
  AIO_KEY=<key> python3 tool/fake_mqtt_board.py --user <adafruit user>
  python3 tool/fake_mqtt_board.py --host 127.0.0.1 --port 1883 --no-tls
  python3 tool/fake_mqtt_board.py --self-test     # HMAC vector, no broker

Adafruit IO free: 30 messages/min for the whole account, so press Enter
slowly. The key is read from AIO_KEY (or --key) and never printed.
"""

import argparse
import hashlib
import hmac
import json
import os
import random
import sys
import threading
from datetime import datetime

ADAFRUIT_HOST = "io.adafruit.com"

# Same vector as test/core/mqtt/broker_messages_test.dart.
TEST_KEY_HEX = "00112233445566778899aabbccddeeff"
TEST_VECTOR = [
    ("fu-a1b2c3", 1234, True, "8ad156c76928fc07521359b09ffda63e1606787577cfdbcbdceee72ce6335734"),
    ("fu-a1b2c3", 1234, False, "cf85f519e79a9189242a27dc5f3deb2b76a4ecbddb9e5071cf5ce56b5aedf6ce"),
]


def sign_answer(key_hex, device, request_id, approve):
    """HMAC-SHA256(key, "<device>|<request>|<approve>") as lowercase hex."""
    message = f"{device}|{request_id}|{'true' if approve else 'false'}"
    return hmac.new(bytes.fromhex(key_hex), message.encode(), hashlib.sha256).hexdigest()


def self_test():
    ok = True
    for device, request_id, approve, expected in TEST_VECTOR:
        got = sign_answer(TEST_KEY_HEX, device, request_id, approve)
        print(f"{'OK  ' if got == expected else 'FAIL'} {device}|{request_id}|{approve}")
        ok = ok and got == expected
    return 0 if ok else 1


def default_prefix(host, user, device):
    """Same rule as defaultTopicPrefix() in lib/core/mqtt/broker_messages.dart."""
    if host.strip().lower() == ADAFRUIT_HOST:
        return f"{user}/feeds/{device}-"
    return f"faceunlock/{device}/"


class FakeBoard:
    def __init__(self, client, args):
        self.client = client
        self.args = args
        self.prefix = args.prefix or default_prefix(args.host, args.user, args.device)
        self.lock = threading.Lock()
        self.waiting = None  # {"request", "name"} while asking approval
        # Random start, like the firmware: an old answer cannot match.
        self.next_request = random.randint(1000, 60000)

    def topic(self, name):
        return self.prefix + name

    def publish(self, name, payload, retain=False):
        text = payload if isinstance(payload, str) else json.dumps(payload)
        self.client.publish(self.topic(name), text, qos=1, retain=retain)
        print(f"  → {name}: {text}")

    def event(self, event_type, name="", request_id=0):
        self.publish("event", {
            "type": event_type,
            "name": name,
            "face": 1 if name else 0,
            "request": request_id,
            "time": datetime.now().isoformat(timespec="seconds"),
        })

    def visitor(self):
        with self.lock:
            request_id = self.next_request
            self.next_request += 1
            self.waiting = {"request": request_id, "name": self.args.name}
        self.publish("approval-request", {
            "request": request_id,
            "face": 1,
            "name": self.args.name,
            "similarity": 0.71,
            "expires_s": self.args.expires,
        })
        self.event("approval_requested", self.args.name, request_id)
        threading.Timer(self.args.expires, self.expire, args=(request_id,)).start()

    def expire(self, request_id):
        with self.lock:
            if self.waiting is None or self.waiting["request"] != request_id:
                return
            self.waiting = None
        print(f"  request {request_id}: no answer in time")
        self.event("approval_timeout", self.args.name, request_id)

    def on_answer(self, payload):
        try:
            answer = json.loads(payload)
            request_id = answer["request"]
            approve = answer["approve"]
        except (ValueError, KeyError, TypeError):
            print(f"  ✗ unreadable answer: {payload!r}")
            return
        if not isinstance(approve, bool) or not isinstance(request_id, int):
            print(f"  ✗ wrong types in answer: {payload!r}")
            return
        if self.args.phone_key:
            expected = sign_answer(self.args.phone_key, self.args.device, request_id, approve)
            if not hmac.compare_digest(str(answer.get("sig", "")), expected):
                print("  ✗ signature wrong or missing: door stays locked")
                return
            print(f"  ✓ signature OK (phone {answer.get('phone')})")
        else:
            print("  (no --phone-key: signature not checked)")
        with self.lock:
            if self.waiting is None or self.waiting["request"] != request_id:
                print(f"  ✗ request {request_id} is not the one waiting: ignored")
                return
            name = self.waiting["name"]
            self.waiting = None
        self.event("door_opened" if approve else "approval_denied", name, request_id)


def run(args):
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        print("Needs paho-mqtt: pip install paho-mqtt", file=sys.stderr)
        return 1
    key = args.key or os.environ.get("AIO_KEY", "")
    if args.host.strip().lower() == ADAFRUIT_HOST and not (args.user and key):
        print("Adafruit IO needs --user and the key in AIO_KEY (or --key)", file=sys.stderr)
        return 1

    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"fu-fake-board-{random.randint(0, 0xFFFFFF):06x}",
    )
    board = FakeBoard(client, args)
    if args.user:
        client.username_pw_set(args.user, key)
    if not args.no_tls:
        client.tls_set()
    client.will_set(board.topic("status"), '"offline"', qos=1, retain=True)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"✗ broker refused: {reason_code}")
            return
        print(f"✓ connected to {args.host}:{args.port}, topics {board.prefix}*")
        client.subscribe(board.topic("approval-answer"), qos=1)
        if args.host.strip().lower() == ADAFRUIT_HOST:
            client.subscribe(f"{args.user}/throttle", qos=1)
        board.publish("status", '"online"', retain=True)

    def on_message(client, userdata, message):
        payload = message.payload.decode("utf-8", errors="replace")
        print(f"← {message.topic}: {payload}")
        if message.topic == board.topic("approval-answer"):
            board.on_answer(payload)

    client.on_connect = on_connect
    client.on_message = on_message
    print("Enter: visitor asks for approval, q Enter: quit")
    client.connect(args.host, args.port, keepalive=30)
    client.loop_start()
    try:
        for line in sys.stdin:
            if line.strip().lower() == "q":
                break
            board.visitor()
    except KeyboardInterrupt:
        pass
    board.publish("status", '"offline"', retain=True)
    client.loop_stop()
    client.disconnect()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=ADAFRUIT_HOST)
    parser.add_argument("--port", type=int, default=8883)
    parser.add_argument("--no-tls", action="store_true", help="plain MQTT (port 1883)")
    parser.add_argument("--user", default="", help="Adafruit IO username")
    parser.add_argument("--key", default="", help="password / AIO key (better: AIO_KEY env)")
    parser.add_argument("--device", default="fu-a1b2c3", help="board id, same as in the app")
    parser.add_argument("--prefix", default="", help="topic prefix; empty: the app's default")
    parser.add_argument("--phone-key", default="", help="hex key: check answer signatures")
    parser.add_argument("--name", default="Hao", help="who stands at the door")
    parser.add_argument("--expires", type=int, default=30, help="seconds to answer")
    parser.add_argument("--self-test", action="store_true", help="check the HMAC vector and exit")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.phone_key:
        try:
            bytes.fromhex(args.phone_key)
        except ValueError:
            parser.error("--phone-key must be hex")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
