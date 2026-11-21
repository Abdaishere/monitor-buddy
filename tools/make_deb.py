#!/usr/bin/env python3
"""Build the laptop app as a .deb for Ubuntu and Debian.

    python3 tools/make_deb.py VERSION [OUT_DIR]      (needs dpkg-deb)

The package puts the tools in /usr/share/monitor-buddy, "Monitor Buddy" in
the app menu, and three commands on the PATH: monitor-buddy (the app),
buddy-say and monitor-buddy-setup. It names python3 and the three Python
libraries the tools need, so the system installs them with it. It installs
nothing into a home folder and starts nothing: the app's Set up does that,
per user, and `monitor-buddy-setup --undo` takes it away again.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SHARE = 'usr/share/monitor-buddy'
COMMANDS = {'monitor-buddy': 'buddy_app.py', 'buddy-say': 'buddy_say.py', 'monitor-buddy-setup': 'buddy_setup.py'}
CONTROL = '''Package: monitor-buddy
Version: {version}
Section: utils
Priority: optional
Architecture: all
Depends: python3 (>= 3.9), python3-cryptography, python3-serial, python3-dateutil
Recommends: xdg-utils
Installed-Size: {size}
Maintainer: Abdaishere <59124608+Abdaishere@users.noreply.github.com>
Homepage: https://github.com/Abdaishere/monitor-buddy
Description: Connect this computer to a Monitor Buddy desk screen
 Monitor Buddy is a small ESP32 screen with a face that follows your day.
 This package holds the computer's side: an app to pair the Buddy over USB,
 change its settings and show a line on it, and the helper that tells it
 about the screen lock, calls, pull requests and meetings. A free hobby
 project.
'''
DESKTOP = '''[Desktop Entry]
Type=Application
Name=Monitor Buddy
Comment=Set up your Monitor Buddy and show a line on it
Exec=monitor-buddy
Icon=monitor-buddy
Terminal=false
Categories=Utility;
Actions=say;

[Desktop Action say]
Name=Say something
Exec=monitor-buddy --say
'''


def write(path, text, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as file:
        file.write(text)
    os.chmod(path, mode)


def build(version, out_dir):
    with tempfile.TemporaryDirectory() as stage:
        os.chmod(stage, 0o755)
        share = os.path.join(stage, SHARE)
        tools = os.path.join(share, 'tools')
        os.makedirs(tools)
        for name in sorted(os.listdir(os.path.join(ROOT, 'tools'))):
            if name.endswith('.py') and not name.startswith('test_') and name != 'make_deb.py':
                shutil.copy(os.path.join(ROOT, 'tools', name), tools)
        for folder in ('app', 'chrome-odoo'):
            shutil.copytree(os.path.join(ROOT, 'tools', folder), os.path.join(tools, folder))
        zones = os.path.join('web', 'vendor', 'posix_tz_db', 'zones.json')   # the app's city search reads it
        os.makedirs(os.path.dirname(os.path.join(share, zones)))
        shutil.copy(os.path.join(ROOT, zones), os.path.join(share, zones))
        write(os.path.join(share, 'PACKAGED'), 'Installed from the monitor-buddy package.\n')
        for folder, _, files in os.walk(stage):
            os.chmod(folder, 0o755)
            for name in files:
                os.chmod(os.path.join(folder, name), 0o755 if name.endswith('.py') and folder == tools else 0o644)
        for command, script in COMMANDS.items():
            write(os.path.join(stage, 'usr/bin', command), f'#!/bin/sh\nexec python3 /{SHARE}/tools/{script} "$@"\n', 0o755)
        write(os.path.join(stage, 'usr/share/applications/monitor-buddy.desktop'), DESKTOP)
        icon = os.path.join(stage, 'usr/share/icons/hicolor/scalable/apps/monitor-buddy.svg')
        os.makedirs(os.path.dirname(icon))
        shutil.copy(os.path.join(ROOT, 'tools', 'app', 'icon.svg'), icon)
        os.chmod(icon, 0o644)
        with open(os.path.join(ROOT, 'LICENSE')) as file:
            notice = file.read()
        with open(os.path.join(ROOT, 'web', 'vendor', 'posix_tz_db', 'LICENSE')) as file:   # zones.json ships in the package
            notice += '\n\nweb/vendor/posix_tz_db/zones.json: posix_tz_db by Nayar Systems, under this licence:\n\n' + file.read()
        for extra in ('NOTICE', 'THIRD-PARTY-LICENSES.md'):   # the live preview holds the firmware's libraries
            with open(os.path.join(ROOT, extra), encoding='utf-8') as file:
                notice += '\n\n' + '=' * 60 + '\n' + extra + '\n' + '=' * 60 + '\n\n' + file.read()
        write(os.path.join(stage, 'usr/share/doc/monitor-buddy/copyright'), notice)
        size = sum(os.path.getsize(os.path.join(folder, name)) for folder, _, files in os.walk(stage) for name in files) // 1024 + 1
        write(os.path.join(stage, 'DEBIAN', 'control'), CONTROL.format(version=version, size=size))
        for folder, _, _ in os.walk(stage):
            os.chmod(folder, 0o755)
        os.makedirs(out_dir, exist_ok=True)
        out = os.path.join(out_dir, f'monitor-buddy_{version}_all.deb')
        subprocess.run(['dpkg-deb', '--root-owner-group', '-Zxz', '--build', stage, out], check=True, stdout=subprocess.DEVNULL)
    return out


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    print(build(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else '.'))
