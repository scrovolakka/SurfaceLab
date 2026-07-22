# SurfaceLab

Independent After Effects native effect prototype for editable bicubic surfaces.

Version 0.15.0 gives every surface its own After Effects animation bank. Up to
eight pages can now keep and render independent point, depth, transform,
deformation, source, image, and material streams. Selecting a surface changes
which bank is shown without changing the other pages' animation. All four
Corner Curl and all four Edge Twist streams remain visible simultaneously.

The current milestone implements a CPU-rendered 4 x 4 bicubic Bezier surface:

- 16 editable After Effects point parameters
- legacy-compatible global tessellation
- optional wireframe overlay
- per-control-point depth values
- internal perspective camera and XYZ surface rotation
- depth-buffered, perspective-correct texture rendering
- up to 8 surfaces in one effect instance
- add, duplicate, delete, and selected-surface controls
- per-surface Size, Position, Rotation, and XYZ Scale controls
- Center, four-edge, and relative Custom X/Y Rotation Origin controls
- keyframeable selected-surface transform, deformation, and material controls
- independent per-surface X/Y Divisions controls
- eight After Effects-managed source-layer slots assignable independently to
  each surface's front and back
- per-surface Stretch, Fill, and Fit image sizing
- Clamp, Repeat, Mirror, and Transparent image-border modes
- per-surface material opacity
- per-surface normal-based thickness with textured front, back, and side walls
- camera XYZ offset and rotation controls
- directional lighting with ambient, diffuse, specular, and shininess controls
- nearest and bilinear texture filtering plus optional backface culling
- whole-surface Bend X/Y and edge-selectable partial Roll deformation
- simultaneous Top Left, Top Right, Bottom Right, and Bottom Left corner curls
- per-corner Amount, Radius, Direction, and Length controls
- simultaneous four-edge Twist with per-edge Angle and Falloff controls
- independent AE animation streams for all four Corner Curl and Edge Twist targets
- independent AE animation banks for up to eight simultaneously animated surfaces
- Source, Auto, and Fixed Padding output-bounds modes
- automatic projected-mesh bounds including thickness and a configurable margin
- project-save, copy/paste, and Undo/Redo-ready arbitrary scene data
- 8-bpc and 16-bpc rendering
- Multi-Frame Rendering-safe render state
- Apple Silicon plug-in bundle

It does not yet implement custom composition UI for depth editing,
After Effects camera integration,
layer attachments, Metal, custom gizmos, or
custom composition-panel gizmos. In this milestone, the selected
surface uses the currently visible animation bank; unselected surfaces continue
to evaluate their own hidden banks. Scene data from the 0.3 through 0.14.1 series
is migrated automatically. The previously selected surface keeps the original
animation streams and the remaining surfaces receive separate initialized banks.

## Build on macOS

```sh
cmake -S . -B work/build/surfacelab -G Ninja
cmake --build work/build/surfacelab
```

The resulting plug-in is written to:

```text
work/build/surfacelab/SurfaceLab.plugin
```

For manual testing, copy the bundle to an After Effects plug-ins directory while
After Effects is closed, then start After Effects and apply `SurfaceLab > SurfaceLab`.
