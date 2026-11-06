# SPDX-License-Identifier: Apache-2.0
"""Select the normal application slot for ESP-IDF's generic app size check."""

import subprocess
import sys


def main() -> int:
    checker, *args = sys.argv[1:]
    # The pinned SDK sends --offset OFFSET partition --type app TABLE IMAGE.
    if len(args) == 7 and args[0] == "--offset" and args[2:5] == ["partition", "--type", "app"]:
        args[5:5] = ["--subtype", "ota_0"]
    return subprocess.call([sys.executable, checker, *args])


if __name__ == "__main__":
    sys.exit(main())
