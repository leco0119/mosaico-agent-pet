"""Generate the Codex-only 480x480 scene and optional design previews.

python tools/ui/gen_usage_scene.py --fonts fonts --out components/app_pet/ui --main-only
python tools/ui/check_contract.py components/app_pet/ui/main.json
"""
import argparse
import json
from pathlib import Path

BG, CARD, BORDER = "#0B0B0D", "#1A1B20", "#30323A"
TRACK, FOOT_BG = "#303138", "#101115"
TXT, TXT2, MUTED = "#F3F3EF", "#C8C9D0", "#A3A4AC"
BANDS = (("red", "#FF4D4F", "#3A1C1E", "额度较低"),
         ("amber", "#FFA940", "#3A2A14", "留意额度"),
         ("green", "#3DD68C", "#183429", "额度充足"))
CS_RING_PCT = "0123456789%—"
CS_RING_LABEL = "0123456789 ·hm:小时后重置每周天分钟即将一二三四五六日额度"
CS_BAR_LABEL = "0123456789 小时每周天分钟"
CS_RESET = "0123456789 hm:后重置周一二三四五六日即将"
CS_TOKENS = "0123456789.KMB—"
CS_REASON = "未登录读取失败无数据额度记录"
CS_STATUS = "0123456789-:… ·更新中等待点此重试未响应PC历史数据连接正在读取账户"
CS_ACTION = "点击更新中…重试"


def build(data, font_dir):
    font_cn = f"{font_dir}/NotoSansSC-Regular.ttf"
    font_num = f"{font_dir}/DejaVuSans-Bold.ttf"
    objects = []

    def add(obj):
        objects.append(obj)
        return len(objects) - 1

    def label(parent, x, y, w, h, text, size=16, color=TXT, *, name=None,
              charset=None, numeric=False, align="left"):
        obj = {"type": "label", "parent": parent, "x": x, "y": y, "w": w, "h": h,
               "text": text, "font_size": size, "fg_color": color, "text_align": align,
               "overflow": "ellipsis"}
        if name:
            obj.update(name=name, bind=name, bind_target="text")
        if charset:
            obj["font_charset"] = charset
        if numeric:
            obj["font"] = font_num
        return add(obj)

    add({"type": "rect", "parent": -1, "x": 24, "y": 23, "w": 4, "h": 20,
         "radius": 2, "bg_color": "#5B8DEF"})
    label(-1, 40, 15, 180, 38, "Codex", 24, numeric=True)
    add({"type": "button", "parent": -1, "x": 316, "y": 10, "w": 148, "h": 48,
         "text": "‹ 桌宠  2/2", "font_size": 16, "fg_color": MUTED, "bg_color": BG,
         "callback": "open_pet"})
    card = add({"type": "container", "parent": -1, "x": 16, "y": 68, "w": 448, "h": 248,
                "bg_color": CARD, "radius": 24, "border_color": BORDER, "border_width": 1})
    percent = data.get("percent", -1)
    band = "red" if 0 <= percent <= 10 else "amber" if 10 < percent <= 30 else "green"
    ring = {"type": "arc", "parent": card, "x": 20, "y": 24, "w": 200, "h": 200,
            "min": 0, "max": 100, "start_angle": 0, "sweep": 360, "thickness": 18, "interactive": False}
    add({**ring, "value": 0, "bg_color": TRACK, "fg_color": TRACK})
    for key, color, tint, _ in BANDS:
        shown = percent >= 0 and key == band
        add({**ring, "name": f"r_ring_{key}", "bind": f"r_ring_{key}_vis", "bind_target": "visible",
             "value": percent if shown else 0, "bg_color": TRACK if key == "green" else tint,
             "fg_color": color, "hidden": not shown})
    label(card, 30, 78, 180, 66, f"{percent}%" if percent >= 0 else "—", 46,
          name="r_ring_pct", charset=CS_RING_PCT, numeric=True, align="center")
    label(card, 20, 148, 200, 28, "剩余额度", 17, MUTED, align="center")
    label(card, 246, 40, 178, 32, data.get("window", "额度"), 20,
          name="r_ring_label", charset=CS_RING_LABEL)
    label(card, 246, 80, 178, 28, data.get("reset", ""), 16, MUTED,
          name="r_ring_reset", charset=CS_RESET)
    label(card, 246, 120, 178, 28, data.get("reason", ""), 16, TXT2,
          name="r_reason", charset=CS_REASON)

    second = data.get("second")
    for key, color, tint, text in BANDS:
        shown = percent >= 0 and key == band and not second and not data.get("reason")
        hint = add({"type": "container", "parent": card, "name": f"r_hint_{key}",
                    "bind": f"r_hint_{key}", "bind_target": "visible", "hidden": not shown,
                    "x": 246, "y": 132, "w": 112, "h": 34, "radius": 8, "bg_color": tint})
        label(hint, 10, 3, 92, 28, text, 16, color, align="center")

    group = add({"type": "container", "parent": card, "name": "r_bar_group",
                 "bind": "r_bar_group", "bind_target": "visible", "hidden": second is None,
                 "x": 246, "y": 158, "w": 178, "h": 74})
    label(group, 0, 0, 106, 26, second["label"] if second else "", 17, MUTED,
          name="r_bar_label", charset=CS_BAR_LABEL)
    label(group, 108, 0, 70, 26, f'{second["percent"]}%' if second else "", 18,
          name="r_bar_pct", charset=CS_RING_PCT, numeric=True, align="right")
    for key, color, tint, _ in BANDS:
        value = second["percent"] if second else -1
        active = "red" if 0 <= value <= 10 else "amber" if 10 < value <= 30 else "green"
        shown = value >= 0 and key == active
        add({"type": "progress", "parent": group, "name": f"r_bar_{key}",
             "bind": f"r_bar_{key}_vis", "bind_target": "visible", "hidden": not shown,
             "x": 0, "y": 32, "w": 178, "h": 7, "min": 0, "max": 100, "value": value if shown else 0,
             "bg_color": TRACK if key == "green" else tint, "fg_color": color, "radius": 3})
    label(group, 0, 46, 178, 24, second["reset"] if second else "", 14, MUTED,
          name="r_bar_reset", charset=CS_RESET)

    total = add({"type": "container", "parent": -1, "x": 16, "y": 330, "w": 448, "h": 78,
                 "bg_color": CARD, "radius": 20, "border_color": BORDER, "border_width": 1})
    label(total, 20, 9, 170, 30, "token 累计", 18)
    label(total, 20, 43, 170, 24, "官方账户统计", 13, MUTED)
    label(total, 192, 13, 232, 52, data.get("tokens", "—"), 36,
          name="r_tokens", charset=CS_TOKENS, numeric=True, align="right")
    footer = add({"type": "button", "parent": -1, "name": "refresh", "callback": "refresh",
                  "x": 0, "y": 424, "w": 480, "h": 56, "radius": 0, "bg_color": FOOT_BG})
    add({"type": "rect", "parent": footer, "x": 24, "y": 0, "w": 432, "h": 1, "bg_color": BORDER})
    label(footer, 24, 14, 330, 28, data.get("status", "等待 PC 连接"), 16, MUTED,
          name="status_text", charset=CS_STATUS)
    label(footer, 366, 14, 90, 28, data.get("action", "点击更新"), 16,
          name="refresh_text", charset=CS_ACTION, align="right")
    return {"screen": "usage", "w": 480, "h": 480, "screen_bg": BG, "font": font_cn,
            "default_font_size": 16, "swipe": True, "objects": objects}


SCENES = {
    "usage.json": {},
    "preview_a.json": {"percent": 2, "window": "每周额度", "reset": "周四 07:25 重置",
                       "tokens": "5.32B", "status": "更新 14:32"},
    "preview_b.json": {"percent": 76, "window": "5 小时额度", "reset": "2h14m 后重置", "tokens": "5.32B",
                       "second": {"label": "每周", "percent": 22, "reset": "周四 07:25 重置"}, "status": "更新 14:32"},
    "preview_c.json": {"reason": "读取失败", "status": "PC 未响应", "action": "点击重试"},
    "preview_x.json": {"percent": 100, "window": "5 小时额度", "reset": "2h14m 后重置", "tokens": "999.99B",
                       "second": {"label": "每周", "percent": 100, "reset": "周四 07:25 重置"}, "status": "更新 14:32"},
    "preview_zero.json": {"percent": 0, "window": "每周额度", "reset": "周四 07:25 重置", "tokens": "5.32B",
                          "reason": "读取失败", "status": "10-31 01:11 · 历史数据"},
}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fonts", default="fonts")
    parser.add_argument("--out", default=str(Path(__file__).resolve().parents[2] / "components/app_pet/ui"))
    parser.add_argument("--main-only", action="store_true")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, data in SCENES.items():
        if args.main_only and name != "usage.json":
            continue
        (out / name).write_text(json.dumps(build(data, args.fonts.rstrip("/")), ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        print("wrote", out / name)
