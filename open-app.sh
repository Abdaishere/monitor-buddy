#!/bin/sh
# Monitor Buddy: open the app (setup and "say" in your browser, no terminal needed).
# In the Files app: right-click this file and choose "Run as a Program".
# After the first setup it is in your app menu as "Monitor Buddy".
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  msg="Monitor Buddy needs Python 3. On Ubuntu or Debian: sudo apt install python3"
  command -v zenity >/dev/null 2>&1 && zenity --error --text="$msg" || echo "$msg"
  exit 1
fi
exec python3 tools/buddy_app.py "$@"
