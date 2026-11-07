"""Retain known display values when an observation is unavailable."""
from __future__ import annotations

import json
from copy import deepcopy
from pathlib import Path

from . import payload

QUOTA_FIELDS = ("ring_percent", "ring_label", "bar_percent", "bar_label", "bar_reset")


def load(path: Path) -> dict | None:
    try:
        return payload.validate(json.loads(path.read_text(encoding="utf-8")))
    except (OSError, ValueError):
        return None


def merge(current: dict, previous: dict | None) -> dict:
    result = deepcopy(current)
    result["stale"] = False
    if previous is None:
        return result
    for vendor in payload.VENDOR_KEYS:
        new, old = result[vendor], previous[vendor]
        if new["ok"]:
            continue  # successful absence, including deliberately disabled quota
        if not new.get("quota_disabled", False) and new["ring_percent"] < 0 and old.get("ring_percent", -1) >= 0:
            new.update({key: old.get(key, -1 if key.endswith("percent") else "") for key in QUOTA_FIELDS})
            result["stale"] = True
        if not new["tokens_text"] and old.get("tokens_text"):
            new["tokens_text"] = old["tokens_text"]
            result["stale"] = True
    if result["stale"]:
        for key in ("generated_at", "updated_text"):
            result[key] = previous.get(key, current.get(key, ""))
    return result
