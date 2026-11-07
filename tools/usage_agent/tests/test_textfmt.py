from datetime import datetime, timezone, timedelta
from usage_agent import textfmt

TZ = timezone(timedelta(hours=8))
NOW = datetime(2026, 11, 1, 21, 58, 0, tzinfo=TZ)  # 周日


def test_reset_text_within_24h_is_countdown():
    resets_at = NOW + timedelta(hours=2, minutes=14, seconds=30)
    assert textfmt.reset_text(resets_at, NOW) == "2h14m 后重置"


def test_reset_text_under_one_hour_shows_minutes_only():
    assert textfmt.reset_text(NOW + timedelta(minutes=7), NOW) == "7m 后重置"


def test_reset_text_beyond_24h_shows_weekday_and_time():
    resets_at = datetime(2026, 11, 5, 9, 0, tzinfo=TZ)  # 周四
    assert textfmt.reset_text(resets_at, NOW) == "周四 09:00 重置"


def test_reset_text_in_past_is_pending():
    assert textfmt.reset_text(NOW - timedelta(minutes=1), NOW) == "即将重置"


def test_reset_text_under_one_minute_is_pending():
    assert textfmt.reset_text(NOW + timedelta(seconds=30), NOW) == "即将重置"


def test_tokens_text():
    assert textfmt.tokens_text(999) == "999"
    assert textfmt.tokens_text(12_345) == "12.3K"
    assert textfmt.tokens_text(410_967_369) == "411M"
    assert textfmt.tokens_text(1_240_000_000) == "1.24B"
    assert textfmt.tokens_text(999_999) == "1.00M"
    assert textfmt.tokens_text(9_999) == "10.0K"
    assert textfmt.tokens_text(99_999) == "100K"
    assert textfmt.tokens_text(5_000) == "5.00K"


def test_updated_text():
    assert textfmt.updated_text(NOW) == "更新 21:58"


def test_ring_label_combines_window_and_reset():
    assert textfmt.ring_label("5 小时", "2h14m 后重置") == "5 小时 · 2h14m 后重置"
