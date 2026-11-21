#!/bin/sh
# encode.sh <input> <output folder>
# <input> is a folder holding either one subfolder of numbered JPEG frames per clip (as render_product.py --clips
# writes them) or near-lossless raw-<clip>.mp4 files made from those frames with
#   ffmpeg -framerate 30 -i <clip>/%04d.jpg -c:v libx264 -crf 10 -pix_fmt yuv420p raw-<clip>.mp4
# Writes broll-<clip>.mp4: H.264, 1080p, 30 fps, fine grain, slight vignette, "3D render" corner mark.
src=$1; dst=$2; mkdir -p "$dst"
font=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
vf="noise=alls=2:allf=t,vignette=PI/11,drawtext=fontfile=$font:text='3D render':fontsize=24:fontcolor=white@0.6:borderw=1:bordercolor=black@0.25:x=w-tw-28:y=h-th-26,format=yuv420p"
enc() { ffmpeg -y -loglevel error "$@" -vf "$vf" -c:v libx264 -preset slow -crf 19 -movflags +faststart "$out" && echo "$out"; }
for f in "$src"/raw-*.mp4; do [ -f "$f" ] || continue; n=$(basename "$f" .mp4); out="$dst/broll-${n#raw-}.mp4"; enc -i "$f"; done
for d in "$src"/*/; do [ -f "$d/0000.jpg" ] || continue; out="$dst/broll-$(basename "$d").mp4"; enc -framerate 30 -i "$d/%04d.jpg"; done
