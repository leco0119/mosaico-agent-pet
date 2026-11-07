from datetime import datetime, timedelta, timezone

import pytest

from usage_agent import codex_source

NOW = datetime(2026, 11, 2, 2, 0, tzinfo=timezone(timedelta(hours=8)))


def _account(used=96, lifetime=5_150_490_649, secondary=None):
    bucket = {
        "limitId": "codex", "limitName": None,
        "primary": {"usedPercent": used, "windowDurationMins": 10080, "resetsAt": 1793834751},
        "secondary": secondary, "planType": "prolite",
    }
    return {
        "limits": {"rateLimits": bucket, "rateLimitsByLimitId": {"codex": bucket}},
        "usage": {"summary": {"lifetimeTokens": lifetime}, "dailyUsageBuckets": []},
    }


def _collect(body):
    return codex_source.collect(now=NOW, fetch=lambda executable: body)


def test_displays_remaining_quota_and_profile_lifetime_total():
    result = _collect(_account())
    assert result["ok"] is True
    assert result["ring_percent"] == 4
    assert result["ring_label"] == "每周 · 周四 07:25 重置"
    assert result["bar_percent"] == -1
    assert result["tokens_total"] == 5_150_490_649
    assert result["tokens_text"] == "5.15B"


def test_short_window_is_ring_and_both_windows_show_remaining():
    body = _account(used=71, secondary={
        "usedPercent": 17, "windowDurationMins": 300, "resetsAt": 1793558400,
    })
    result = _collect(body)
    assert result["ring_percent"] == 83
    assert result["ring_label"].startswith("5 小时 · ")
    assert result["bar_percent"] == 29
    assert result["bar_label"] == "每周"


def test_selects_codex_bucket_instead_of_legacy_premium_bucket():
    body = _account()
    body["limits"]["rateLimits"] = {
        "limitId": "premium", "primary": {
            "usedPercent": 20, "windowDurationMins": 300, "resetsAt": 1793558400,
        },
    }
    assert _collect(body)["ring_percent"] == 4


def test_legacy_codex_bucket_is_supported_when_map_is_absent():
    body = _account()
    del body["limits"]["rateLimitsByLimitId"]
    assert _collect(body)["ring_percent"] == 4


def test_other_bucket_is_not_presented_as_codex_quota():
    body = _account()
    body["limits"]["rateLimitsByLimitId"] = {"premium": body["limits"]["rateLimits"]}
    result = _collect(body)
    assert result["ok"] is False
    assert result["ring_percent"] == -1
    assert result["tokens_text"] == "5.15B"


@pytest.mark.parametrize(("used", "remaining"), [(0, 100), (100, 0), (-5, 100), (105, 0)])
def test_remaining_percentage_is_clamped(used, remaining):
    assert _collect(_account(used=used))["ring_percent"] == remaining


@pytest.mark.parametrize("lifetime", [None, -1, True, "5150490649"])
def test_missing_or_invalid_lifetime_is_not_substituted_with_local_or_zero_total(lifetime):
    result = _collect(_account(lifetime=lifetime))
    assert result["ok"] is False
    assert result["tokens_total"] is None
    assert result["tokens_text"] == ""
    assert result["ring_percent"] == 4


def test_real_zero_lifetime_is_displayed():
    result = _collect(_account(lifetime=0))
    assert result["ok"] is True
    assert result["tokens_text"] == "0"


def test_quota_failure_keeps_valid_account_total():
    body = _account()
    body["limits"] = None
    result = _collect(body)
    assert result["ok"] is False
    assert result["ring_percent"] == -1
    assert result["tokens_total"] == 5_150_490_649


def test_nullable_reset_time_does_not_hide_valid_quota():
    body = _account()
    body["limits"]["rateLimits"]["primary"]["resetsAt"] = None
    result = _collect(body)
    assert result["ring_percent"] == 4
    assert result["ring_label"] == "每周"


def test_fetch_failure_does_not_expose_raw_errors_or_invent_zero_usage():
    def fail(executable):
        raise RuntimeError("private diagnostic data")

    result = codex_source.collect(now=NOW, fetch=fail)
    assert result["ok"] is False
    assert result["ring_percent"] == -1
    assert result["tokens_total"] is None
    assert "private" not in str(result)
