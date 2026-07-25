# SurfaceLab v1 implementation map

The 0.x staged-decomposition note is obsolete. v1 uses the following split:

| Unit | Responsibility |
|---|---|
| `SurfaceLabModel` | Variable lattice, validation, point interpolation, resampling, wire persistence, and shutter-subframe timing |
| `SurfaceLabGeometry` | Catmull–Rom evaluation, normals, affine math, and surface/scene transforms |
| `SurfaceLab` | Effect entry point, lattice arbitrary callbacks, and frame scene capture |
| `SurfaceLabRender` | Per-shutter Smart PreRender snapshots, AE camera/light/Null capture, motion-sample compositing, rasterization, depth buffer, and render views |
| `SurfaceLabUI` | Static v1 parameter declaration, division changes, and Composition gizmo selection/editing |

The old `SceneDataV1`–`SceneDataV13` structs and migration functions are gone.
Every Surface group owns one independent lattice arbitrary stream. This is the
only persisted deformation state.

Foldspace-oriented interaction work lives on `feature/foldspace-ux` and is
tracked in [FOLDSPACE_UX.md](FOLDSPACE_UX.md).

## Invariants

1. Render and gizmo evaluate the same `LatticeData`.
2. Active point count is always `(divisions_x + 1) × (divisions_y + 1)`.
3. Lattice points are finite surface-local 3D vectors.
4. Mesh Quality changes tessellation only; it never changes lattice data.
5. Smart Render performs no AEGP scene queries. Camera/light queries happen in
   Smart PreRender at each shutter subframe and are stored in the render
   snapshot.
6. The host layer remains a 2D, unparented drawing window.
7. Motion blur is active only when both the composition and host-layer Motion
   Blur switches are enabled.

## Verification

Both of these must stay green:

```sh
cmake -S . -B work/build/tests \
  -DSURFACELAB_BUILD_PLUGIN=OFF \
  -DSURFACELAB_BUILD_TESTS=ON
cmake --build work/build/tests
ctest --test-dir work/build/tests --output-on-failure

cmake -S . -B work/build/surfacelab -G Ninja
cmake --build work/build/surfacelab
```
