# SurfaceLab

SurfaceLab is an After Effects native effect for placing and deforming flat
sources in 3D with an interpolating control-point lattice.

Current version: **1.2.0** (v1 architecture).
Match name: `XPK SurfaceLab` · Effect menu: **SurfaceLab > SurfaceLab**.

v1 is a clean break from the 0.x prototype. Older projects are not migrated.

## Requirements

- macOS 11+ (current build is macOS-only)
- After Effects with the licensed AE SDK (build path in `CMakeLists.txt`)
- Xcode command-line tools (clang, Rez, codesign) and CMake + Ninja
- Host validation below was done in After Effects 2026

## Quick start

1. Build and install the plug-in (see [Build](#build)), then restart AE.
2. Open a composition and run
   [scripts/SurfaceLabSetup.jsx](scripts/SurfaceLabSetup.jsx) via
   `File > Scripts > Run Script File…`.
3. The script creates a composition-sized, unparented 2D solid
   **SurfaceLab Host** and applies the effect.
4. In any **Surface N** group, set **Source** to a footage or precomp layer.
   Surface 1 initially samples the host input itself.

Optional: run
[scripts/SurfaceLabCreateNullRig.jsx](scripts/SurfaceLabCreateNullRig.jsx)
to issue 3D Null point controllers for a surface.

## Architecture

- **Scene** — shared Position, Rotation XYZ, and Scale XYZ for the whole effect.
- **Eight static Surface groups**, each with:
  - Source layer
  - Position, Rotation XYZ, Scale XYZ
  - Divisions X/Y (1–16 each)
  - Mesh Quality (1–8)
  - Lattice (`PF_Param_ARBITRARY_DATA`)
- The lattice is the canonical geometry: `(Dx + 1) × (Dy + 1)` surface-local
  3D points. Tensor-product Catmull–Rom evaluation keeps every control point
  on the surface; a one-division axis reduces to exact linear interpolation.
- Lattice keyframes interpolate point-by-point linearly. Flattening uses an
  explicit big-endian, active-points-only wire format (`SLV1` schema).
- Mesh Quality only changes tessellation density per lattice interval; it never
  rewrites lattice data.
- Each Surface has a procedural **Roll** layer (Angle, Tilt, Radius,
  Expand/Turn) applied at evaluation time. Angle `0` is flat; animate Angle for
  unroll-style motion without rewriting lattice keyframes.
- Rendering uses the CPU SmartFX path for 8/16/32-bpc output, perspective-
  correct bilinear sampling, per-pixel depth, and **Render View** modes:
  Finish, Depth, UV, Normal.
- Projection uses AE’s active camera layer, or AE’s implicit default camera
  when none exists. Lighting reads composition lights (up to 8).
- Motion blur samples lattice parameters, marker-linked Nulls, camera, lights,
  and sources at AE shutter subframes (capped at 32 samples).
- Output is always clipped to the 2D host layer rectangle.
- The Composition panel draws the lattice. Drag a point for local X/Y;
  Option/Alt-drag for Z. **Shift-click** toggles points; click a grid **line**
  to select a whole free row/column; **Cmd/Ctrl-drag** box-selects free points
  on one surface (Shift+Cmd/Ctrl adds). A selected set shows an RGB
  **translate gizmo** at its cage centroid (local X/Y/Z). Null-controlled
  points appear orange and are read-only in the gizmo.
- Foldspace-oriented interaction work is tracked in
  [docs/FOLDSPACE_UX.md](docs/FOLDSPACE_UX.md).

Removed from 0.x: internal camera/light, surface list, material/thickness,
bend/roll/curl/twist, image fit/border modes, rotation-origin modes,
output-bounds modes, migration chain, and hidden animation banks.

Module boundaries are documented in [docs/DECOMPOSITION.md](docs/DECOMPOSITION.md).

## Null point controllers

Run [scripts/SurfaceLabCreateNullRig.jsx](scripts/SurfaceLabCreateNullRig.jsx)
from `File > Scripts > Run Script File…`. Choose a surface and issue all
points, the perimeter, one row, one column, or one point.

- Optional **Surface Root** is created at the evaluated cage center and aligned
  to the surface’s current world-space frame. Its exact AE world transform is
  sampled and stored as the bind frame, so the initial orientation is neutral
  and later Root motion stays a rigid whole-surface transform. SurfaceLab does
  not create a Scene Root.
- Each 3D Point Null is linked by a **layer marker** containing the host layer
  ID, the persistent 64-bit Surface ID, and the lattice row/column. Layer names
  and timeline order are for readability only.
- New Point Nulls are oriented from the lattice U/V tangents and normal at
  issuance. Re-running the script reuses matching Nulls without overwriting
  transforms or animation. Legacy roots are upgraded while preserving Point
  Null world positions.
- After issuance the data flow is one-way: Null world position overrides its
  linked lattice point at the current frame.
- The collapsed **Null Rig Bridge** effect group is internal script transport
  and does not need manual editing.

## Motion blur

Enable Motion Blur on both the composition and the **SurfaceLab Host** layer.
SurfaceLab uses the composition shutter angle, shutter phase, and suggested
samples-per-frame value (max 32). Lattice keyframes and animated Nulls are
evaluated at each shutter subframe; no extra SurfaceLab control is required.

## Build

Configure the After Effects SDK path in `CMakeLists.txt` if it differs from the
default under `work/vendor/AfterEffectsSDK/…`.

```sh
cmake -S . -B work/build/surfacelab -G Ninja
cmake --build work/build/surfacelab
```

Output:

```text
work/build/surfacelab/SurfaceLab.plugin
```

Clean build + install into the shared MediaCore folder (AE must be quit):

```sh
scripts/build_and_install.sh
# optional:
# SURFACELAB_INSTALL_DIR="/path/to/Plug-ins" scripts/build_and_install.sh
```

Default install location:

```text
/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore
```

### Tests

Host-independent model and geometry tests (no AE SDK required):

```sh
cmake -S . -B work/build/tests \
  -DSURFACELAB_BUILD_PLUGIN=OFF \
  -DSURFACELAB_BUILD_TESTS=ON
cmake --build work/build/tests
ctest --test-dir work/build/tests --output-on-failure
```

Coverage includes control-point interpolation, local support, degenerate axes,
resampling, keyframe interpolation, endian-independent persistence, normals,
transform inversion, and shutter-subframe timing.

## Repository layout

| Path | Role |
|---|---|
| `src/` | Effect entry, model, geometry, render, UI |
| `scripts/` | Setup, Null-rig issuer, build-and-install |
| `resources/` | PiPL and bundle Info.plist |
| `tests/` | Model/geometry unit tests |
| `docs/` | Implementation map |
| `work/` | Local build, SDK, smoke tests (gitignored) |
| `outputs/` | Packaged release zips (gitignored) |

## Host validation

Marker-identified Null issuance and live 3D point overrides have been
integration-tested in After Effects 2026. Motion blur was verified with a
marker-linked Null animated across two frames at a 360° shutter and 16 samples
per frame, including an explicit Motion Blur OFF/ON comparison. Oriented Point
Null and Surface Root issuance, Scene Root removal, and rigid Root
translation/rotation were also verified in AE 2026.
