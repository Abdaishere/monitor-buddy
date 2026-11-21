@echo off
rem Monitor Buddy: open the app on Windows (the editor with the live preview).
rem Double-click this file. It needs Python 3 (python.org or the Microsoft Store)
rem and, to talk to the Buddy on its COM port, pyserial:  python -m pip install pyserial
cd /d "%~dp0"
where pythonw >nul 2>nul
if not errorlevel 1 (
  start "" pythonw tools\buddy_app.py %*
  exit /b 0
)
where python >nul 2>nul
if not errorlevel 1 (
  start "Monitor Buddy" /min python tools\buddy_app.py %*
  exit /b 0
)
where py >nul 2>nul
if not errorlevel 1 (
  start "Monitor Buddy" /min py -3 tools\buddy_app.py %*
  exit /b 0
)
echo Monitor Buddy needs Python 3. Get it from https://www.python.org/downloads/ and open this file again.
pause
