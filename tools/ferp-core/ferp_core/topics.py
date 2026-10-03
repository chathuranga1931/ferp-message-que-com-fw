"""
topics.py — FERP MQTT topic layout (must match ModuleMqtt::_build_topics).

    ferp/{dev_type}/{group}/{topic_id}/cmd       commands → device
    ferp/{dev_type}/{group}/{topic_id}/resp      responses ← device
    ferp/{dev_type}/{group}/{topic_id}/evt       events ← device
    ferp/{dev_type}/{group}/{topic_id}/ota/...   OTA ctrl / data / resp

topic_id is the device UUID (lower-case, dashes removed — uuid_to_topic_id())
when provisioned, otherwise the MAC address without separators.
"""

from typing import Optional, Tuple

ROOT = "ferp"


def topic_id(device_id: str) -> str:
    return device_id.replace(":", "").replace("-", "").lower()


def device_base(dev_type: str, group: str, device_id: str) -> str:
    return f"{ROOT}/{dev_type}/{group}/{topic_id(device_id)}"


def wildcard(dev_type: str, kind: str) -> str:
    """Subscription for one kind ('resp', 'evt') across all devices of a type."""
    return f"{ROOT}/{dev_type}/+/+/{kind}"


def parse(topic: str) -> Optional[Tuple[str, str, str, str]]:
    """'ferp/ferp-com/default/abc/resp' → ('ferp-com', 'default', 'abc', 'resp')."""
    parts = topic.split("/")
    if len(parts) < 5 or parts[0] != ROOT:
        return None
    return parts[1], parts[2], parts[3], "/".join(parts[4:])
