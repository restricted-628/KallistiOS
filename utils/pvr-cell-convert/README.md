# Cell-sprite asset compiler

`pvr-cell-convert` turns one strict declarative cell manifest and one atlas
image into a pointer-free `PCA1` animation asset plus a C translation unit
containing the normalized `pvr_sprite_atlas_t` geometry.

```text
pvr-cell-convert --image atlas.png --symbol hero_atlas \
    hero.pcell hero.pca hero_atlas.c
```

The image is inspected for its actual dimensions but is not decoded or copied
into either output. Texture conversion and residency stay with the application.
Pixel rectangles are checked against the image and converted into normalized
UV coordinates. The generated source records no input paths.

## Manifest grammar

Blank lines and `#` comments are ignored. The first declaration is mandatory:

```text
pvr-cell 1
```

Atlas regions are declared before cells. Coordinates, dimensions, and pivots
are pixels; the pivot must lie within the inclusive region bounds.

```text
region X Y WIDTH HEIGHT PIVOT_X PIVOT_Y
```

One `cell` supplies a complete base slot. Its four diffuse colors and four
offset colors use A/B/C/D rectangle order. Unsuffixed integers are decimal;
flags, material identifiers, and colors also accept a `0x` prefix.

```text
cell ATLAS X Y Z ROTATION SCALE_X SCALE_Y PRIORITY FLAGS MATERIAL \
     DIFFUSE_A DIFFUSE_B DIFFUSE_C DIFFUSE_D \
     OFFSET_A OFFSET_B OFFSET_C OFFSET_D
```

Streams are independent and ordered. A repeating stream uses `[0, TIME_MAX)`;
a clamped stream uses `[0, TIME_MAX]`.

```text
stream TIME_OFFSET TIME_MAX repeat|clamp
key-atlas TIME SLOT ATLAS
key-offset TIME SLOT X Y Z
key-rotation TIME SLOT RADIANS
key-scale TIME SLOT X Y
key-priority TIME SLOT PRIORITY
key-flags TIME SLOT FLAGS
key-material TIME SLOT MATERIAL
key-diffuse TIME SLOT A B C D
key-specular TIME SLOT A B C D
end-stream
```

Each key changes one field. Multiple keys at the same timestamp may change
several fields deterministically. The compiler rejects unknown declarations,
overlong lines, out-of-order keys, invalid indices, nonfinite values, atlas
overflow, and every malformed stream before creating output. Temporary files
are renamed only after both outputs are complete.
