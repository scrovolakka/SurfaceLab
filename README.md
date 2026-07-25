# SurfaceLab v1

SurfaceLab is an After Effects native effect for placing and deforming flat
sources in 3D with an interpolating control-point lattice. v1 is a clean break:
projects created by the 0.x prototype are intentionally not migrated.

## v1 architecture

- Eight static `Surface` groups, each with one source layer, Position,
  Rotation XYZ, Scale XYZ, Divisions XY, Mesh Quality, and one Lattice value.
- The lattice is the canonical geometry. It contains `(Dx + 1) × (Dy + 1)`
  3D points with independent `Dx` and `Dy` in the range 1–16.
- Tensor-product Catmull–Rom evaluation keeps every control point on the
  surface. A one-division axis reduces to exact linear interpolation.
- Each lattice is one keyframeable `PF_Param_ARBITRARY_DATA` stream.
  Interpolation is point-by-point linear interpolation.
- Flattening uses an explicit big-endian, active-points-only wire format.
- Mesh Quality is independent from control resolution and ranges from 1–8
  subdivisions per lattice interval.
- Rendering reuses the CPU SmartFX rasterizer for 8/16/32-bpc output,
  perspective-correct bilinear texture sampling, per-pixel depth, and
  Finish/Depth/UV/Normal views.
- Projection uses AE's current camera geometry directly: the active camera
  layer when one exists, otherwise AE's own implicit default camera. Lighting
  reads AE composition lights.
- Motion blur evaluates lattice parameters, marker-linked Nulls, camera,
  lights, and source layers at AE shutter subframes, then composites the
  samples in the active 8/16/32-bpc format.
- Output is always clipped to the 2D host layer rectangle.
- The Composition panel draws the variable lattice. Drag a point to edit local
  X/Y; Option/Alt-drag the same point to edit Z. There is no separate depth
  handle.

The 0.x internal camera, internal light, surface list, material/thickness
controls, bend/roll/curl/twist parameters, image fitting/border controls,
rotation-origin modes, output-bounds modes, migration chain, and hidden
animation banks have been removed.

## Setup

Run [SurfaceLabSetup.jsx](scripts/SurfaceLabSetup.jsx) from
`File > Scripts > Run Script File…`. It creates:

1. A composition-sized, unparented 2D `SurfaceLab Host` solid.
2. The SurfaceLab effect.

Surface 1 initially samples the host input. Assign a source layer in any
Surface group to use actual footage or a precomp.

## Null point controllers

Run [SurfaceLabCreateNullRig.jsx](scripts/SurfaceLabCreateNullRig.jsx) from
`File > Scripts > Run Script File…`. Choose a surface and issue all points,
the perimeter, one row, one column, or one point. The optional Surface Root is
created at the evaluated cage center and aligned to the surface's current
world-space frame. Its complete bind transform is stored so the initial
orientation is neutral and later Root motion remains a rigid whole-surface
transform. SurfaceLab does not create a Scene Root.

Each issued 3D Null is linked by a layer marker containing the host layer ID,
the persistent 64-bit Surface ID, and its lattice row and column. Layer names
and timeline order are only for readability and do not affect the link.
New Point Nulls are aligned from the lattice's local U/V tangents and normal,
so their local axes follow the surface at issuance. Re-running the script
reuses matching Point Nulls without overwriting their transforms or animation.
Legacy roots are upgraded while preserving existing Point Null world
positions. After issuance the data flow is intentionally one-way: Null world
position overrides its linked lattice point at the current frame.

Controlled points remain visible in the Composition panel as orange,
read-only lattice points. Move or animate their Nulls to deform the surface.
The collapsed `Null Rig Bridge` effect group is internal script transport and
does not need manual editing.

## Motion blur

Enable Motion Blur for both the composition and the `SurfaceLab Host` layer.
SurfaceLab uses the composition shutter angle, shutter phase, and suggested
samples-per-frame value, capped at 32 samples. Lattice keyframes and animated
Null controllers are evaluated at each shutter subframe; no additional
SurfaceLab control is required.

## Build

The plug-in requires macOS and the After Effects SDK at the path configured in
`CMakeLists.txt`.

```sh
cmake -S . -B work/build/surfacelab -G Ninja
cmake --build work/build/surfacelab
```

Output:

```text
work/build/surfacelab/SurfaceLab.plugin
```

Host-independent model and geometry tests:

```sh
cmake -S . -B work/build/tests \
  -DSURFACELAB_BUILD_PLUGIN=OFF \
  -DSURFACELAB_BUILD_TESTS=ON
cmake --build work/build/tests
ctest --test-dir work/build/tests --output-on-failure
```

The tests cover control-point interpolation, local support, degenerate axes,
resampling, keyframe interpolation, endian-independent persistence, normals,
transform inversion, and shutter-subframe timing.

## Host validation

Marker-identified Null issuance and live 3D point overrides have been
integration-tested in After Effects 2026. Motion blur was verified with a
marker-linked Null animated across two frames at a 360-degree shutter and 16
samples per frame, including an explicit Motion Blur OFF/ON comparison.
