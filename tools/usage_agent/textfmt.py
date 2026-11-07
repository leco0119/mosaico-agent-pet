"""屏幕文案生成：重置倒计时、token 缩写、更新时间。"""
from __future__ import annotations

from datetime import datetime, timedelta

WEEKDAYS = ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]


def reset_text(resets_at: datetime, now: datetime) -> str:
    """重置文案。resets_at 与 now 都必须是带时区的 datetime。"""
    remaining = resets_at - now
    if remaining < timedelta(minutes=1):
        return "即将重置"
    if remaining < timedelta(hours=24):
        minutes = int(remaining.total_seconds() // 60)
        hours, minutes = divmod(minutes, 60)
        if hours == 0:
            return f"{minutes}m 后重置"
        return f"{hours}h{minutes:02d}m 后重置"
    local = resets_at.astimezone(now.tzinfo)
    return f"{WEEKDAYS[local.weekday()]} {local:%H:%M} 重置"


def tokens_text(count: int) -> str:
    if count < 1000:
        return str(count)
    for unit, scale in (("B", 1e9), ("M", 1e6), ("K", 1e3)):
        if count >= scale * 0.9995:
            value = count / scale
            if value >= 99.95:
                return f"{value:.0f}{unit}"
            if value >= 9.995:
                return f"{value:.1f}{unit}"
            return f"{value:.2f}{unit}"
    return str(count)


def updated_text(now: datetime) -> str:
    return f"更新 {now:%H:%M}"


def ring_label(window: str, reset: str) -> str:
    return f"{window} · {reset}"
