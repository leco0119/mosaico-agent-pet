import pytest
from usage_agent import payload

SAMPLE = {
    "generated_at": "2026-11-01T21:58:03+08:00",
    "updated_text": "更新 21:58",
    "claude": {"ok": True, "reason": "", "ring_percent": 62, "ring_label": "5 小时 · 2h14m 后重置",
               "bar_percent": 38, "bar_label": "每周", "bar_reset": "周四 09:00 重置", "tokens_text": "1.24B"},
    "codex": {"ok": True, "reason": "", "ring_percent": 96, "ring_label": "每周 · 周三 14:05 重置",
              "bar_percent": -1, "bar_label": "", "bar_reset": "", "tokens_text": "411M"},
}


def test_encode_lines_in_fixed_order():
    text = payload.encode(SAMPLE)
    lines = text.split("\n")
    assert lines[0] == "v=1"
    assert lines[1] == "updated=更新 21:58"
    assert not any(line.startswith("l.") for line in lines)
    assert "r.bar=-1" in lines
    assert lines[-1] == "r.tokens=411M"
    assert len(text.encode("utf-8")) <= payload.MAX_PAYLOAD_BYTES


def test_encode_historical_data_marks_wire_payload():
    assert "\nstale=1\n" in payload.encode({**SAMPLE, "stale": True})
    assert "\nstale=0\n" in payload.encode(SAMPLE)
    with pytest.raises(payload.PayloadError):
        payload.validate({**SAMPLE, "stale": "yes"})


def test_encode_intentional_quota_disable():
    disabled = {**SAMPLE, "codex": {**SAMPLE["codex"], "quota_disabled": True}}
    assert "r.quota_disabled=1\n" in payload.encode(disabled)
    with pytest.raises(payload.PayloadError):
        payload.validate({**SAMPLE, "codex": {**SAMPLE["codex"], "quota_disabled": "yes"}})


def test_validate_rejects_missing_vendor():
    bad = dict(SAMPLE)
    del bad["codex"]
    with pytest.raises(payload.PayloadError):
        payload.validate(bad)


def test_validate_rejects_newline_in_text():
    bad = {**SAMPLE, "codex": {**SAMPLE["codex"], "ring_label": "a\nb"}}
    with pytest.raises(payload.PayloadError):
        payload.validate(bad)


def test_encode_truncates_long_text_to_31_bytes():
    long = {**SAMPLE, "codex": {**SAMPLE["codex"], "ring_label": "字" * 40}}
    text = payload.encode(long)
    line = next(line_text for line_text in text.split("\n") if line_text.startswith("r.ring_label="))
    value = line.split("=", 1)[1]
    assert len(value.encode("utf-8")) <= 31
    assert value.encode("utf-8").decode("utf-8") == value  # 不在多字节字符中间截断


def test_encode_rejects_oversized_payload(monkeypatch):
    # 字段级截断使正常文档永远不会超限，所以把上限压低来触发检查。
    monkeypatch.setattr(payload, "MAX_PAYLOAD_BYTES", 100)
    with pytest.raises(payload.PayloadError):
        payload.encode(SAMPLE)
