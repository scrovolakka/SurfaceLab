# SurfaceLab parameter layout

SurfaceLab v1.8 registers **318** After Effects parameters. Modern After
Effects does not impose a 255-parameter limit; this exact count is documented
for script indexing and saved-project compatibility, not as a capacity ceiling.

| Block | Indices | Count |
|---|---:|---:|
| Input | 0 | 1 |
| Scene | 1–9 | 9 |
| Surfaces topic + eight 26-param Surface blocks | 10–219 | 210 |
| Render and Comp-edit controls | 220–224 | 5 |
| Compact Null Rig Bridge | 225–243 | 19 |
| Point Animation topic, controls, and 32 slot pairs | 244–311 | 68 |
| Null Rig launcher button | 312 | 1 |
| Version button | 313 | 1 |
| Surface Utilities topic, target, action, and end | 314–317 | 4 |

## Compatibility rule

Indices 0–243 are identical to v1.6. Every existing Scene, Surface, bridge,
launcher, and version parameter also keeps its persisted disk ID. The v1.7
Point Animation block is inserted before the two existing buttons in property
order, but After Effects resolves those persisted parameters by their stable
disk IDs.

Retired disk IDs must never be reused:

- `700…708` — v1.4.3 Bridge topic, selectors, ID chunks, and dimensions
- `799` — v1.4.3 Bridge end topic
- `800`, `809` — About topic wrapper
- `720…770` — legacy separate XYZ Bridge sliders

The Version button keeps disk ID `801`. Rig Point disk IDs `780…796` also stay
stable.

## Point Animation bank

- **244** — collapsed Point Animation topic
- **245** — Expose Selected Points button
- **246** — Clear All Point Slots button
- **247…310** — 32 pairs of hidden binding metadata and keyframeable Point3D
  values
- **311** — Point Animation topic end

Binding metadata stores `(surface + 1, row + 1, column + 1)`. A binding outside
the Surface's current divisions remains persisted and is ignored until those
divisions make the point active again.

## Surface Utilities

- **314** — collapsed Surface Utilities topic
- **315** — Target Surface selector
- **316** — Match Source Aspect button
- **317** — Surface Utilities topic end

The block is appended after every v1.7 parameter. Its fresh disk IDs
`900…903` avoid changing the existing Scene, Surface, bridge, Point Animation,
Null launcher, and Version property indices.

## Compact Bridge

- **225 Rig Request** — Point3D `(surface + 1, row, reserved)`
- **226 Rig Metadata** — Point3D `(surface ID high32, low32, Dx * 32 + Dy)`
- **227…243 Rig Points** — seventeen Point3D column values for the requested
  lattice row

All integer metadata is lossless in AE's double-valued Point3D fields.
`SurfaceLabCreateNullRig.jsx`, `SurfaceLabChainEdges.jsx`, and
`SurfaceLabCreateRollControl.jsx` use these exact property indices; matching
`static_assert`s in `SurfaceLab.h` prevent silent layout drift.
