# Product renders

`docs/blog/images/product-*.jpg` are made here with Blender (4.2 or newer, Cycles; a GPU makes it minutes).

```sh
python docs/blog/render/fetch_assets.py          # Poly Haven CC0 HDRIs and wood texture -> render/assets (not in git)
blender -b -P docs/blog/render/render_product.py -- --stl <folder with dock_head_v8.stl and dock_base_v8.stl> \
    --out raw --res 1600x1200 --samples 512 --blend monitor-buddy-hero.blend
blender -b -P docs/blog/render/render_product.py -- --stl <same> --out raw-sq --res 2000x2000 --samples 384 --suffix -sq
python docs/blog/render/label.py raw docs/blog/images       # adds the "3D render" corner mark
python docs/blog/render/label.py raw-sq docs/blog/images
```

`--shots hero,front,three-quarter,side,back,exploded,out-of-dock,flat-lay,night,screen-closeup` picks single shots.
The script builds every scene from nothing, so it is the scene file. `--blend` also saves the first shot as a
`.blend` to open in Blender. That file and the dock's STL files are not in this repository: the dock's model
is under the MakerWorld Exclusive License and a `.blend` contains its mesh.

What is real and what is modelled: the dock head and base are the published model files. The board (Waveshare
ESP32-C6-Touch-LCD-1.47, 44.5 x 24.55 x 10.6 mm), the USB-C cable, the keyboard, the monitor and the desk are
modelled in the script. Screen pictures are the firmware screenshots in `docs/guide/shots`, with a faint blue
backlight glow added to match the real panel.

## B-roll clips

The B-roll clips attached to the release (`broll-*.mp4`, 1080p, 30 fps, H.264) come from the same script:

```sh
blender -b -P docs/blog/render/render_product.py -- --stl <folder> --out clips --res 1920x1080 --samples 160 \
    --clips turntable,dolly-in,rack-focus,exploded,night      # numbered JPEG frames per clip; finished frames are skipped
sh docs/blog/render/encode.sh clips docs/blog/images          # grain, slight vignette, "3D render" mark, MP4
```

About 10 s a frame on an RTX 3080 (900 frames, 2.5 hours).
