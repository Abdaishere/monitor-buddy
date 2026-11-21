#!/usr/bin/env python3
"""Print the full guide: guide.html with technical.html added at its end, to
~/Downloads/Monitor-Buddy-Guide.pdf and ../Monitor-Buddy-Guide.pdf.

    python docs/guide/make_pdf.py
"""
import os
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
OUTPUTS = [os.path.expanduser('~/Downloads/Monitor-Buddy-Guide.pdf'), os.path.join(HERE, '..', 'Monitor-Buddy-Guide.pdf')]


def main():
    with open(os.path.join(HERE, 'guide.html')) as file:
        guide = file.read()
    with open(os.path.join(HERE, 'technical.html')) as file:
        technical = file.read()
    full = os.path.join(HERE, 'full.html')   # beside guide.html, so shots/ resolves
    with open(full, 'w') as file:
        file.write(guide.replace('</body>', technical + '\n</body>', 1))
    subprocess.run(['google-chrome', '--headless=new', '--disable-gpu', '--no-pdf-header-footer',
                    f'--print-to-pdf={OUTPUTS[0]}', f'file://{full}'], check=True, capture_output=True)
    os.remove(full)
    shutil.copyfile(OUTPUTS[0], OUTPUTS[1])
    print('printed', OUTPUTS[0], 'and', os.path.normpath(OUTPUTS[1]))


if __name__ == '__main__':
    main()
