r"""usage.json 的结构校验与设备行文本编码。

行文本每行 key=value，UTF-8，\n 分隔。文本字段按 UTF-8 字节截断到 31 字节
（设备端缓冲 32 字节含结尾 0），不在多字节字符中间截断。
"""
from __future__ import annotations

VENDOR_KEYS = ("codex",)
# Keep the original r.* wire keys so existing stored snapshots remain readable.
PREFIX = {"codex": "r"}
# 设备端缓冲：USAGE_TEXT_MAX 32 / USAGE_TOKENS_MAX 16 / USAGE_UPDATED_MAX 24（见 components/app_usage/include/usage_payload.h），减 1 留给结尾 0
TEXT_LIMIT = 31
TOKENS_LIMIT = 15
UPDATED_LIMIT = 23
# 整段行文本的 UTF-8 字节上限；设备端 CONFIG_ESP_IRIS_RPC_BODY_BYTES=2048（Iris 传输层拒绝超长请求体）
MAX_PAYLOAD_BYTES = 2000


class PayloadError(ValueError):
    pass


def _truncate_utf8(text: str, limit: int) -> str:
    data = text.encode("utf-8")
    if len(data) <= limit:
        return text
    return data[:limit].decode("utf-8", errors="ignore")


def _check_text(value: object, name: str) -> str:
    if not isinstance(value, str):
        raise PayloadError(f"{name} must be a string")
    if "\n" in value or "\r" in value:
        raise PayloadError(f"{name} must not contain newlines")
    return value


def _check_percent(value: object, name: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise PayloadError(f"{name} must be an int")
    if value != -1 and not 0 <= value <= 100:
        raise PayloadError(f"{name} must be -1 or 0..100")
    return value


def validate(doc: dict) -> dict:
    if not isinstance(doc, dict):
        raise PayloadError("document must be an object")
    _check_text(doc.get("updated_text", ""), "updated_text")
    if not isinstance(doc.get("stale", False), bool):
        raise PayloadError("stale must be bool")
    for vendor in VENDOR_KEYS:
        v = doc.get(vendor)
        if not isinstance(v, dict):
            raise PayloadError(f"missing vendor {vendor}")
        if not isinstance(v.get("ok"), bool):
            raise PayloadError(f"{vendor}.ok must be bool")
        if not isinstance(v.get("quota_disabled", False), bool):
            raise PayloadError(f"{vendor}.quota_disabled must be bool")
        for key in ("reason", "ring_label", "bar_label", "bar_reset", "tokens_text"):
            _check_text(v.get(key, ""), f"{vendor}.{key}")
        for key in ("ring_percent", "bar_percent"):
            _check_percent(v.get(key, -1), f"{vendor}.{key}")
    return doc


def encode(doc: dict) -> str:
    validate(doc)
    lines = ["v=1", "updated=" + _truncate_utf8(doc.get("updated_text", ""), UPDATED_LIMIT)]
    lines.append(f"stale={1 if doc.get('stale', False) else 0}")
    for vendor in VENDOR_KEYS:
        p = PREFIX[vendor]
        v = doc[vendor]
        lines.append(f"{p}.ok={1 if v['ok'] else 0}")
        lines.append(f"{p}.quota_disabled={1 if v.get('quota_disabled', False) else 0}")
        lines.append(f"{p}.reason={_truncate_utf8(v.get('reason', ''), TEXT_LIMIT)}")
        lines.append(f"{p}.ring={v.get('ring_percent', -1)}")
        lines.append(f"{p}.ring_label={_truncate_utf8(v.get('ring_label', ''), TEXT_LIMIT)}")
        lines.append(f"{p}.bar={v.get('bar_percent', -1)}")
        lines.append(f"{p}.bar_label={_truncate_utf8(v.get('bar_label', ''), TEXT_LIMIT)}")
        lines.append(f"{p}.bar_reset={_truncate_utf8(v.get('bar_reset', ''), TEXT_LIMIT)}")
        lines.append(f"{p}.tokens={_truncate_utf8(v.get('tokens_text', ''), TOKENS_LIMIT)}")
    text = "\n".join(lines)
    size = len(text.encode("utf-8"))
    if size > MAX_PAYLOAD_BYTES:
        raise PayloadError(f"encoded payload is {size} bytes, limit {MAX_PAYLOAD_BYTES}")
    return text
