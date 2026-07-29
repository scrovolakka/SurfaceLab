# SurfaceLab

SurfaceLab is an After Effects native effect for placing and deforming flat
sources in 3D with an interpolating control-point lattice.

Current version: **1.8.0** (v1 architecture).
Match name: `XPK SurfaceLab` · Effect menu: **SurfaceLab > SurfaceLab**.
The installed build is shown in Effect Controls under **About → SurfaceLab Version**.

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

Use **Null Controllers → Create Null Rig…** in Effect Controls to issue 3D
Null point controllers for a surface. The same workflow remains available by
running [scripts/SurfaceLabCreateNullRig.jsx](scripts/SurfaceLabCreateNullRig.jsx)
manually.

To animate points without Null layers, select free lattice vertices in the
Composition panel and click **Point Animation → Expose Selected Points**.
SurfaceLab reveals one keyframeable 3D Point property per selected vertex
(up to 32). Reducing Divisions may temporarily mark a binding as inactive;
restoring the divisions reactivates the same property and its keyframes.
**Clear All Point Slots** removes the bindings but does not rewrite the
lattice.

When a Surface must inherit the proportions of its footage or precomp, open
**Surface Utilities**, choose the Target Surface, and click
**Match Source Aspect**. The operation preserves the lattice centre, height,
deformation, and Point Animation bindings while scaling its local X extent to
the Source Layer's display aspect ratio.

Additional workflow scripts:

- [scripts/SurfaceLabCreateRollControl.jsx](scripts/SurfaceLabCreateRollControl.jsx)
  creates an ordinary AE Null for animating Roll Angle, Tilt, Radius, and
  Expand/Turn.
- [scripts/SurfaceLabChainEdges.jsx](scripts/SurfaceLabChainEdges.jsx) merges
  two previously issued edge-Null rows into shared controllers.

## Architecture

- **Scene** — shared Position, Rotation XYZ, and Scale XYZ for the whole effect.
- **Eight static Surface groups**. Empty slots show only
  **Source Layer (enable)**; assigning a source reveals that Surface's full
  controls. Each enabled group has:
  - Front Source layer and optional Back Source layer
  - Image Size (Stretch, Fill, Fit)
  - Image Transform (position X/Y and rotation Z) plus Image Scale
  - Specular, Roughness, and Metalness
  - Optional Thickness with front, back, and side faces
  - Off, 2-sample, and 4-sample spatial antialiasing
  - Position, Rotation XYZ, Scale XYZ
  - Divisions X/Y (1–16 each)
  - Mesh Quality (1–8)
  - Lattice (`PF_Param_ARBITRARY_DATA`)
- The lattice is the canonical geometry: `(Dx + 1) × (Dy + 1)` surface-local
  3D points. Tensor-product Catmull–Rom evaluation keeps every control point
  on the surface; a one-division axis reduces to exact linear interpolation.
- Lattice keyframes interpolate point-by-point linearly. Flattening uses an
  explicit big-endian, active-points-only wire format (`SLV1` schema).
- A fixed bank of 32 Point Animation bindings exposes selected lattice
  vertices as ordinary keyframeable 3D Point properties. Fixed slots keep the
  Effect Controls schema stable while divisions change.
- Mesh Quality only changes tessellation density per lattice interval; it never
  rewrites lattice data.
- Each Surface has a procedural **Roll** layer (Angle, Tilt, Radius,
  Expand/Turn) applied at evaluation time. Angle `0` is flat; animate Angle for
  unroll-style motion without rewriting lattice keyframes.
- Rendering uses the CPU SmartFX path for 8/16/32-bpc output, perspective-
  correct bilinear sampling, per-pixel depth, and **Render View** modes:
  Finish, Depth, UV, Normal.
- On macOS, AE's Metal device setup now compiles and dispatch-validates a
  device-local SurfaceLab pipeline. Frame rasterization intentionally remains
  on the CPU SmartFX path until the Metal renderer reaches visual parity; AE
  therefore keeps the existing CPU fallback for every frame.
  Developers can configure `SURFACELAB_METAL_DIAGNOSTIC_COPY=ON` to route
  GPU SmartFX through a visible copy/tint kernel that verifies GPU-world
  buffers and padded `rowbytes`; this diagnostic is off in release builds.
- Projection uses AE’s active camera layer, or AE’s implicit default camera
  when none exists. Lighting reads composition lights (up to 8). A light with
  **Casts Shadows** enabled produces self-shadows and shadows between
  SurfaceLab surfaces. AE Shadow Darkness and Shadow Diffusion are respected;
  zero diffusion keeps the hard-shadow path.
- Motion blur samples lattice parameters, marker-linked Nulls, camera, lights,
  and sources at AE shutter subframes (capped at 32 samples).
- Output is always clipped to the 2D host layer rectangle.
- The Composition panel draws the lattice. **Edit Mode** selects a Vertex,
  one Edge segment, one Face cell, or the whole Surface. Shift-click toggles
  entities; Edge double-click expands to the full row/column. Cmd/Ctrl-drag
  box-selects free vertices (Shift+Cmd/Ctrl adds). **Transform Tool** provides
  Move, Rotate, and Scale gizmos at the resolved selection centroid, while
  **Transform Space** switches between the cage’s Local axes and AE World XYZ.
  Shared vertices are transformed once. In Move mode, direct drag follows the
  chosen space’s X/Y plane and Option/Alt-drag follows its Z axis.
  Move axes use high-contrast arrowheads and a centre hub; Move, Rotate, and
  Scale handles grow brighter and thicker on hover and show a crosshair cursor
  before dragging.
  Null-controlled points appear orange and remain read-only. Once an entity
  is selected, its Surface cage is drawn brighter and above overlapping
  inactive cages; other Surface cages are dimmed until the selection clears.
- Foldspace-oriented interaction work is tracked in
  [docs/FOLDSPACE_UX.md](docs/FOLDSPACE_UX.md).

Removed from 0.x: internal camera/light, surface list, bend/curl/twist,
border modes, rotation-origin modes, output-bounds modes,
migration chain, and hidden animation banks.

Module boundaries are documented in [docs/DECOMPOSITION.md](docs/DECOMPOSITION.md).
The persisted parameter layout and reserved IDs are documented in
[docs/PARAMETER_LAYOUT.md](docs/PARAMETER_LAYOUT.md).

## Null point controllers

Click **Null Controllers → Create Null Rig…** in Effect Controls (or run
[scripts/SurfaceLabCreateNullRig.jsx](scripts/SurfaceLabCreateNullRig.jsx)
manually). Choose a surface and issue all points, the perimeter, one row, one
column, or one point. Choose **All Enabled Surfaces** to apply the same range
to Surface 1 and every Surface whose Source Layer is assigned.

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
- The compact **Null Rig Bridge** is fully hidden internal script transport.
  v1.5 packs its request and metadata into two 3D Point streams, reducing the
  internal schema from 255 to 245 transport parameters. v1.7 adds the fixed
  Point Animation bank and keeps every existing authored Surface and bridge
  disk ID stable. Modern After Effects does not impose a 255-parameter limit;
  the compact bridge remains simpler to maintain.

### Roll controller

Run
[scripts/SurfaceLabCreateRollControl.jsx](scripts/SurfaceLabCreateRollControl.jsx)
and choose a Surface. The script creates or reuses **SL S# Roll**, adds
standard AE Angle/Slider controls, copies existing Roll key values on first
creation, and connects the SurfaceLab Roll parameters through a host Layer
Control. Renaming or reordering the controller does not break the connection.

### Shared edges

First issue the desired row or column on both surfaces with
[scripts/SurfaceLabCreateNullRig.jsx](scripts/SurfaceLabCreateNullRig.jsx).
Then run [scripts/SurfaceLabChainEdges.jsx](scripts/SurfaceLabChainEdges.jsx)
and choose the two matching edges. Each Surface A Null receives both point
identities, so one animated Null drives the paired vertices on both surfaces.
The optional quick UV crop sets complementary two-panel offsets; pre-sliced
source precomps remain the exact uncropped solution because v1 image scale is
uniform rather than independent X/Y.

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
