#!/usr/bin/env python3
"""Claude Code hook: notes what a Claude Code session is doing, for the Buddy.

    buddy_claude_hook.py needs|done|prompt|tool|end      (hook input on stdin)

tools/buddy_setup.py puts these calls into ~/.claude/settings.json:
    Notification (permission_prompt|elicitation_dialog), PreToolUse (AskUserQuestion)   needs
    Stop                                                                                done
    UserPromptSubmit                                                                    prompt
    PostToolUse                                                                         tool
    SessionEnd                                                                          end

It writes "needs FOLDER" or "done FOLDER" to CLAUDE_FILE, and "STATE FOLDER" to
SESSIONS_DIR/SESSION_ID (STATE: working, needs or done), which buddy_link.py
reads. It never prints and always exits 0, so it cannot get in Claude's way.
Only the standard library, so it starts fast.
"""
import json
import os
import re
import sys

# The same two paths as in buddy_link.py (a test keeps them equal).
CLAUDE_FILE = os.path.expanduser('~/.cache/monitor-buddy/claude')
SESSIONS_DIR = os.path.expanduser('~/.cache/monitor-buddy/sessions')


def _write(path, text):
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    with open(path, 'w') as file:
        file.write(text + '\n')


def _read(path):
    try:
        with open(path) as file:
            return file.read()
    except OSError:
        return ''


def _remove(path):
    try:
        os.remove(path)
    except OSError:
        pass


def note(event, data, folder):
    session = re.sub(r'[^A-Za-z0-9-]', '', str(data.get('session_id') or ''))
    own = os.path.join(SESSIONS_DIR, session) if session else None
    if event in ('needs', 'done'):
        _write(CLAUDE_FILE, f'{event} {folder}')
        if own:
            _write(own, f'{event} {folder}')
    elif event == 'prompt':
        _remove(CLAUDE_FILE)
        if own:
            _write(own, f'working {folder}')
    elif event == 'tool':   # a tool ran, so the question was answered
        if 'needs' in _read(CLAUDE_FILE):
            _remove(CLAUDE_FILE)
        if own and _read(own).startswith('needs'):
            _write(own, f'working {folder}')
    elif event == 'end' and own:
        _remove(own)


def main():
    try:
        try:
            data = json.load(sys.stdin)
        except ValueError:
            data = {}
        if not isinstance(data, dict):
            data = {}
        folder = os.path.basename((os.environ.get('CLAUDE_PROJECT_DIR') or str(data.get('cwd') or '')).rstrip('/'))
        note(sys.argv[1] if len(sys.argv) > 1 else '', data, folder)
    except Exception:   # a hook must never break the session it runs in
        pass


if __name__ == '__main__':
    main()
