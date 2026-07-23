# SurfaceLab

Independent After Effects native effect prototype for editable bicubic surfaces.

Version 0.15.13 applies the Composition World host-transform cancel for every
camera source (the internal camera included), so a camera-less comp keeps the
render and the Null rig registered. Version 0.15.12 unified the per-surface scale and rotation pivots on the
Rotation Origin ("hinge" semantics) and makes the controller rig's surface Root
null that hinge: the Root is created on the origin and the origin follows the
Root thereafter, for every origin mode. Version 0.15.11 added a shared Scene
Transform and reorganized the effect UI into
five collapsed top-level groups. `Finish`, `Depth`, `UV`, and view-space
`Normals` render views, a fixed Surface List, and a hierarchical 3D Null
controller rig build on the SmartFX, After Effects active-camera, and
composition-light support. Up to
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
- shared Scene Position, XYZ Rotation, and XYZ Scale parent transform
- collapsed Scene Transform, Surfaces, Camera, Lights, and Render Settings
  top-level groups
- Control Points, Depth, and per-surface Transform grouped under
  `Surfaces > Selected Surface`
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
- fixed eight-row Surface List with direct selection and per-surface
  visibility toggles (no wheel scrolling required)
- Render View output for Finish, auto-normalized Depth, UV color, and
  view-space Normals in 8-, 16-, and 32-bpc SmartFX rendering
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
- companion script for one shared Scene Root plus each selected surface's
  Root + 16 point 3D Null rig
- Apple Silicon plug-in bundle

It does not yet implement native layer attachments or Metal. The composition-panel
surface gizmo now uses the same resolved camera snapshot as rendering. The selected
surface uses the currently visible animation bank; unselected surfaces continue
to evaluate their own hidden banks. Scene data from the 0.3 through 0.14.1 series
is migrated automatically. The previously selected surface keeps the original
animation streams and the remaining surfaces receive separate initialized banks.

## 3D Null controller prototype

`scripts/SurfaceLabCreateControllers.jsx` creates or reuses one
`SurfaceLab - Scene Root`, then creates a child 3D Root Null and sixteen
parented 3D point Nulls for the surface currently selected in the effect. Move a
point Null to edit that control's X, Y, and Depth together. Move, rotate, or
scale a surface Root to transform only that surface; transform the Scene Root
to move, rotate, or scale every SurfaceLab surface together.
Creating the rig switches `Camera Source` to the After Effects active camera and
`Coordinate Space` to `Composition World`. In that mode the renderer cancels
the unparented 2D host layer's affine transform before compositing, so the
rendered mesh, SurfaceLab gizmo, and native 3D Nulls stay registered while the
camera or host layer moves. After Effects always draws 3D Nulls through the
comp's active camera, so keep a camera layer in the comp: with `Camera Source`
on the internal camera (or no camera layer at all) the Null overlay and the
render are projected differently and cannot line up exactly.

The Scene Root drives the top-level Scene Transform streams. Each surface Root
drives only its persistent surface animation bank, so the shared parent does not
rewrite individual control cages. The surface Root null IS the surface's
transform hinge: rig creation seats the Root on the current Rotation Origin
(Center, an edge, or Custom), switches the origin to Custom, and binds the
origin percentages to follow the Root. Rotation and Scale both pivot on that
point, in the renderer and in the Null hierarchy alike. To move the hinge
relative to the surface, move the point Nulls (the cage) rather than the Root;
moving the Root carries surface and hinge together. The binding uses the surface's persistent ID
and animation bank rather than its visible page number, so adding, deleting, or
reordering other surfaces does not retarget an existing rig. SurfaceLab still
stores the canonical cage in effect coordinates; the Null children expose local
offsets from the surface Root. The renderer and controller foundation share the
same Local/Cage/Scene/World transform contract.

To create a rig:

1. Select the layer containing SurfaceLab and choose the intended surface in
   `Selected Surface`.
2. Run `File > Scripts > Run Script File...` and select
   `scripts/SurfaceLabCreateControllers.jsx`.
3. Animate `SurfaceLab - Scene Root`, the generated `SurfaceLab S… - Root`, or
   any of its sixteen `SurfaceLab S… - Control row,column` Nulls.

To return one surface to its flat control cage, select either its SurfaceLab
source layer or any generated controller Null, then run
`scripts/SurfaceLabResetSelectedSurface.jsx`. The reset clears point, depth,
position, rotation, rotation-origin, and scale animation for that surface,
restores the Center origin, and removes only the controller Nulls carrying the
matching SurfaceLab surface/layer marker. Source
assignment, material, deformation, camera, and lighting settings are preserved.
The complete operation is one After Effects undo step.

This first prototype intentionally refuses to overwrite existing expressions or
keyframes on the bound SurfaceLab streams. Renaming
or deleting generated layers breaks their expressions. Composition World rigs
require SurfaceLab to remain on an unparented 2D host layer; a 3D or parented host
would add another camera projection that cannot represent the deformed mesh.

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
