#!/bin/sh
# Monitor Buddy: set up this computer (Linux). Safe to run again at any time:
# it skips what is already done. See "Set up" in README.md.
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo "Monitor Buddy needs Python 3. On Ubuntu or Debian: sudo apt install python3"
  exit 1
fi
exec python3 tools/buddy_setup.py "$@"
