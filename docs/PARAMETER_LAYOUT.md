# SurfaceLab parameter layout

SurfaceLab v1.6 registers **246** After Effects parameters. Modern After
Effects does not impose a 255-parameter limit; this exact count is documented
for script indexing and saved-project compatibility, not as a capacity ceiling.

| Block | Indices | Count |
|---|---:|---:|
| Input | 0 | 1 |
| Scene | 1–9 | 9 |
| Surfaces topic + eight 26-param Surface blocks | 10–219 | 210 |
| Render and Comp-edit controls | 220–224 | 5 |
| Compact Null Rig Bridge | 225–243 | 19 |
| Null Rig launcher button | 244 | 1 |
| Version button | 245 | 1 |

## Compatibility rule

Indices 0–224 are identical to v1.4.3. Every authored Scene and Surface
parameter also keeps its persisted disk ID. The only removed streams are
internal Bridge selectors/metadata and decorative topic wrappers; they contain
no artist-authored geometry or animation.

Retired disk IDs must never be reused:

- `700…708` — v1.4.3 Bridge topic, selectors, ID chunks, and dimensions
- `799` — v1.4.3 Bridge end topic
- `800`, `809` — About topic wrapper
- `720…770` — legacy separate XYZ Bridge sliders

The Version button keeps disk ID `801`. Rig Point disk IDs `780…796` also stay
stable.

## Compact Bridge

- **225 Rig Request** — Point3D `(surface + 1, row, reserved)`
- **226 Rig Metadata** — Point3D `(surface ID high32, low32, Dx * 32 + Dy)`
- **227…243 Rig Points** — seventeen Point3D column values for the requested
  lattice row

All integer metadata is lossless in AE's double-valued Point3D fields.
`SurfaceLabCreateNullRig.jsx`, `SurfaceLabChainEdges.jsx`, and
`SurfaceLabCreateRollControl.jsx` use these exact property indices; matching
`static_assert`s in `SurfaceLab.h` prevent silent layout drift.
