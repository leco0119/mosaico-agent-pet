#!/usr/bin/env python3
"""Send a screen-only pet state or read its diagnostics, without console popups."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import time
from host_runtime import DEFAULT_DEVICE, MosaicoClient


@contextmanager
def connected_client(device_id: str = DEFAULT_DEVICE):
    client = MosaicoClient(device_id)
    try:
        if client.configured_device is None:
            if not client.present():
                raise RuntimeError("No Mosaico USB control endpoint is present")
            client.connect_auto()
        yield client
    finally:
        client.close()


def rpc(method: str, payload: str = "") -> str:
    with connected_client() as client:
        return client.rpc(method, payload)


def build_payload(args: argparse.Namespace) -> str:
    fields = [("A", args.anim), ("P", args.phrase),
              ("D", args.detail), ("T", args.title)]
    return "\n".join(f"{key}={' '.join(str(value).split())}" for key, value in fields
                     if value is not None)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-id", default=DEFAULT_DEVICE)
    commands = parser.add_subparsers(dest="command", required=True)
    update = commands.add_parser("set")
    update.add_argument("--anim", type=int, choices=range(4))
    for name in ("phrase", "detail", "title"):
        update.add_argument(f"--{name}")
    commands.add_parser("poll")
    watch = commands.add_parser("watch")
    watch.add_argument("--interval", type=float, default=2)
    args = parser.parse_args()
    if args.command == "set":
        payload = build_payload(args)
        if not payload:
            parser.error("set needs at least one field")
    with connected_client(args.device_id) as client:
        if args.command == "set":
            print(client.rpc("0x0001", payload))
        elif args.command == "poll":
            print(client.rpc("0x0002"))
        else:
            while True:
                print(client.rpc("0x0002"), flush=True)
                time.sleep(max(0.5, args.interval))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
