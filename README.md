# SurfaceLab

Independent After Effects native effect prototype for editable bicubic surfaces.

Version 0.15.7 adds optional After Effects active-camera and composition-light
linking while preserving the existing internal camera and light controls. Up to
eight pages can now keep and render independent point, depth, transform,
deformation, source, image, and material streams. Selecting a surface changes
which bank is shown without changing the other pages' animation. All four
Corner Curl and all four Edge Twist streams remain visible simultaneously.

The current milestone implements a CPU SmartFX-rendered 4 x 4 bicubic Bezier
surface:

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
- switchable Internal or After Effects Active Camera projection
- switchable Internal or After Effects Comp Lights shading
- AE ambient, parallel, point, and spot lights with color, intensity, and spot cone
- nearest and bilinear texture filtering plus optional backface culling
- whole-surface Bend X/Y and edge-selectable partial Roll deformation
- simultaneous Top Left, Top Right, Bottom Right, and Bottom Left corner curls
- per-corner Amount, Radius, Direction, and Length controls
- simultaneous four-edge Twist with per-edge Angle and Falloff controls
- independent AE animation streams for all four Corner Curl and Edge Twist targets
- independent AE animation banks for up to eight simultaneously animated surfaces
- projected 4 x 4 surface cage and outline in the Composition panel
- direct dragging of all 16 surface control points
- drag the blue axis endpoint beside any control point to edit its Depth directly
- Option-drag (Alt-drag) on a control point remains available as a shortcut
- direct whole-surface dragging from the rendered polygon area
- direct custom Rotation Origin dragging from an on-surface pivot handle
- direct local XYZ Scale dragging from red, green, and blue square handles
- direct XYZ Rotation dragging from the matching outer diamond handles
- Interaction Mode filtering for All, Surface, Control Points, and Deform
- Gizmo Tool filtering for All, Position, Rotation, and Scale
- four pink Curl handle pairs: tip Amount/Radius and fold Length/Direction
- one yellow Roll handle pair on the selected edge: Angle and Length
- four purple Twist handle pairs: per-edge Angle and Falloff
- perspective-, rotation-, and deformation-aware screen-to-parameter dragging
- Source, Auto, and Fixed Padding output-bounds modes
- automatic projected-mesh bounds including thickness and a configurable margin
- project-save, copy/paste, and Undo/Redo-ready arbitrary scene data
- 8-bpc, 16-bpc, and 32-bpc float rendering
- camera/light snapshots resolved during Smart PreRender, outside Smart Render
- legacy 8/16-bpc render fallback for hosts that do not use SmartFX
- Apple Silicon plug-in bundle

It does not yet implement layer attachments or Metal. The composition-panel
surface gizmo continues to use SurfaceLab's internal camera in this milestone;
the After Effects camera source controls the rendered result. The selected
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

## Model unit tests

The scene data model and its V1–V13 on-disk migration chain live in
`src/SurfaceLabModel.{h,cpp}` and depend on nothing from the After Effects SDK,
so they can be built and run on any platform without Adobe headers:

```sh
cmake -S . -B work/build/tests -DSURFACELAB_BUILD_PLUGIN=OFF -DSURFACELAB_BUILD_TESTS=ON
cmake --build work/build/tests
ctest --test-dir work/build/tests --output-on-failure
```

The tests exercise initialization, validation, and the migration paths that are
hard to observe through the plug-in UI.
