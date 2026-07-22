# SurfaceLab decomposition — status and hand-off

This note tracks the ongoing break-up of the monolithic `src/SurfaceLab.cpp`
into focused translation units, and gives a local (macOS) session everything it
needs to finish the After Effects-coupled half with real compile verification.

## Why the split is staged

The plug-in is macOS-only and needs the licensed AE SDK, so the AE-coupled code
can only be *built* on a Mac with the SDK in place. The scene model, migration,
and geometry math depend on nothing from the AE SDK, so those were extracted
first and are unit-tested off-host (Linux/CI, any compiler in the clang family).

Guiding rule: every extraction is a **verbatim move** — no behavioral change —
and each step should leave `cmake --build` green.

## Header architecture (established)

```
SurfaceLab.h            public plug-in surface (EffectMain, param enums) — minimal
  └ SurfaceLabModel.h       AE-free: Point types, tuning constants, V1..V13 structs,
                            model-op declarations                (host-independent)
      └ SurfaceLabGeometry.h  AE-free: inline vector ops, EvaluatePatch /
                              ApplySurfaceDeform declarations    (host-independent)
          └ SurfaceLabInternal.h  AE-coupled: GlobalData, A_long constants,
                                  PF_ParamIndex tables, param helpers, render types
```

`SurfaceLab.cpp` includes only `SurfaceLabInternal.h`, which re-exports the rest.
Cross-unit functions get a declaration in the appropriate header and one
definition in one `.cpp`; small pure helpers are `inline` in a header; the
`Pixel`-templated render functions must stay in one TU with their callers.

## Done (verified here)

| Unit | Files | Verification |
|------|-------|--------------|
| Scene model + V1–V13 migration + validation | `SurfaceLabModel.{h,cpp}` | `ctest` — every version has a migration test |
| Geometry + bicubic deformation | `SurfaceLabGeometry.{h,cpp}` | `ctest` — endpoint interpolation, identity, finiteness |

`SurfaceLab.cpp` is down from 8837 to ~6900 lines.

## Build & test

Plug-in (macOS, needs the SDK at
`work/vendor/AfterEffectsSDK/ae25.6_61.64bit.AfterEffectsSDK`):

```sh
cmake -S . -B work/build/surfacelab -G Ninja
cmake --build work/build/surfacelab       # -> work/build/surfacelab/SurfaceLab.plugin
```

Host-independent model/geometry tests (any platform, no SDK):

```sh
cmake -S . -B work/build/tests -DSURFACELAB_BUILD_PLUGIN=OFF -DSURFACELAB_BUILD_TESTS=ON
cmake --build work/build/tests
ctest --test-dir work/build/tests --output-on-failure
```

## Remaining — AE-coupled, do on a Mac with the SDK

Extract in this order, rebuilding after each move:

### 1. Rendering core → `SurfaceLabRender.{h,cpp}`

Move `ProjectVertex`, `Edge`, `MapImageCoordinates`, `ResolveBorderCoordinate`,
the `SampleTexture` / `ApplyLighting` / `ApplyOpacity` / `ClearWorld` /
`RasterizeTriangle` / `DrawLine` / `RasterizeSurface` templates,
`BuildSurfaceEvaluationState`, `BuildCameraState`, `ResolveCompTime`,
`ResolveAfterEffectsCamera`, `ReadLayerStream`, `ResolveAfterEffectsLights`,
`IncludeProjectedVertex`, `AccumulateSurfaceBounds`, `LimitExpandedAxis`,
`FrameSetup`, and `RenderSurface`.

Gotchas:
- The pixel-templated functions are used only render-side; keep them together.
- **Move the `Render()` dispatcher too** so the `RenderSurface<PF_Pixel8/16>`
  instantiations live in this TU; `EffectMain` then calls the non-template
  `Render` / `FrameSetup` via a header declaration.
- `RenderSurface` reaches `ResolveSceneForFrame` (scene resolution from params /
  arbitrary data). Either declare it in a shared header (it stays with the
  arbitrary/scene glue in `SurfaceLab.cpp`) or move that glue first.

### 2. Custom UI + gizmo → `SurfaceLabUI.{h,cpp}`

Move the `Capture*` / `Load*` parameter bridges, `ApplySizeAndPositionUi`,
`IsSurfaceEditorParam`, `ParamsSetup`, `AddSurfaceAnimationBankParams`,
`UserChangedParamV15`, `UpdateParameterUi`, all gizmo `Project*` / `Drag*`
helpers, `DrawSurfaceGizmo`, and `HandleSurfaceGizmoEvent`.

### 3. Leave `SurfaceLab.cpp` thin

`EffectMain` dispatch, the arbitrary-data callbacks
(`CreateSceneHandle` / `CopySceneHandle` / `HandleArbitrary` / scene resolution),
and `PluginDataEntryFunction2`.

## Follow-on refactors (once the split lands)

Verifiable against the model tests:
- Replace `PrimaryAnimationParam`'s switch with a `constexpr` table.
- Factor the shared field-copy boilerplate out of the `MigrateSceneV*` chain.

AE-coupled (verify on Mac):
- Name the magic numbers in the gizmo math (hit radius, falloff, forwarding
  epsilon).
- Extend the `IsValidScene` spirit to gizmo projection failure and texture-border
  edges (see the review findings for the specific spots).
