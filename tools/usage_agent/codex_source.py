"""Codex account lifetime tokens and remaining quota, matching the profile."""
from __future__ import annotations

import math
from datetime import datetime, timezone
from typing import Callable

from . import codex_rpc, textfmt

EMPTY = {
    "ok": False, "reason": "", "ring_percent": -1, "ring_label": "",
    "bar_percent": -1, "bar_label": "", "bar_reset": "", "tokens_text": "", "tokens_total": None,
}


def _window_name(minutes: int | None) -> str:
    if minutes is None:
        return ""
    if minutes % 1440 == 0 and minutes >= 1440:
        days = minutes // 1440
        return "每周" if days == 7 else f"{days} 天"
    if minutes % 60 == 0:
        return f"{minutes // 60} 小时"
    return f"{minutes} 分钟"


def _number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _windows(body: dict, now: datetime) -> list[tuple]:
    buckets = body.get("rateLimitsByLimitId")
    bucket = buckets.get("codex") if isinstance(buckets, dict) else body.get("rateLimits")
    if not isinstance(bucket, dict) or bucket.get("limitId") not in (None, "codex"):
        return []
    windows = []
    for key in ("primary", "secondary"):
        window = bucket.get(key)
        if not isinstance(window, dict) or not _number(window.get("usedPercent")):
            continue
        raw_minutes = window.get("windowDurationMins")
        minutes = int(raw_minutes) if _number(raw_minutes) and raw_minutes > 0 else None
        remaining = max(0, min(100, 100 - round(window["usedPercent"])))
        reset = ""
        stamp = window.get("resetsAt")
        if _number(stamp):
            try:
                reset = textfmt.reset_text(datetime.fromtimestamp(stamp, tz=timezone.utc), now)
            except (ValueError, OverflowError, OSError):
                pass
        windows.append((minutes if minutes is not None else math.inf, _window_name(minutes), remaining, reset))
    return sorted(windows, key=lambda window: window[0])


def fetch_account(executable: str | None) -> dict:
    return codex_rpc.read_account(codex_rpc.command(executable))


def collect(executable: str | None = None, *, now: datetime | None = None,
            fetch: Callable[[str | None], dict] | None = None) -> dict:
    result = dict(EMPTY)
    now = now or datetime.now().astimezone()
    try:
        body = (fetch or fetch_account)(executable)
    except Exception:
        # Never expose app-server diagnostics, auth details or raw exception text.
        return {**result, "reason": "读取失败"}
    if not isinstance(body, dict):
        return {**result, "reason": "读取失败"}

    usage = body.get("usage")
    summary = usage.get("summary") if isinstance(usage, dict) else None
    lifetime = summary.get("lifetimeTokens") if isinstance(summary, dict) else None
    total_valid = isinstance(lifetime, int) and not isinstance(lifetime, bool) and lifetime >= 0
    if total_valid:
        result["tokens_total"] = lifetime
        result["tokens_text"] = textfmt.tokens_text(lifetime)

    limits = body.get("limits")
    windows = _windows(limits, now) if isinstance(limits, dict) else []
    if windows:
        _, name, percent, reset = windows[0]
        result["ring_percent"] = percent
        result["ring_label"] = textfmt.ring_label(name, reset) if name and reset else name or reset
    if len(windows) > 1:
        _, name, percent, reset = windows[1]
        result.update(bar_percent=percent, bar_label=name, bar_reset=reset)
    result["ok"] = bool(windows) and total_valid
    if not result["ok"]:
        result["reason"] = "读取失败"
    return result
