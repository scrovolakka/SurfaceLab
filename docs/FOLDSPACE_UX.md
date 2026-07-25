# Foldspace-oriented UX plan

Goal: raise SurfaceLab’s **interaction quality** toward Foldspace-style
workflows without cloning product identity or abandoning the v1 lattice model.

Primary success metric: artists can build **unroll**, **page-flip-like point
rigs**, and **simple multi-strip layouts** with fewer steps and less guesswork.

This is a UX program, not a full feature-parity checklist.

## Principles

1. Prefer workflow parity over UI cloning.
2. Keep the lattice as the canonical geometry (Catmull–Rom, variable Dx/Dy).
3. Add procedural helpers (Roll) as evaluation layers; do not replace lattice
   keyframes.
4. Ship in thin vertical slices that stay demoable in AE.
5. Preserve v1 invariants in [DECOMPOSITION.md](DECOMPOSITION.md).

## Gap matrix

Status key:

- **Have** — usable today in v1.1.6
- **Partial** — present in limited form
- **Missing** — not available

Priority key:

- **Must** — needed for Foldspace-like feel
- **Nice** — strong quality-of-life
- **Skip** — defer or intentionally diverge

| Capability | Foldspace | SurfaceLab today | Priority | Phase |
|---|---|---|---|---|
| Apply effect → 3D surface in one host | Yes | Host solid + effect script | Nice | A |
| Multiple surfaces in one effect | Dynamic create/delete | Fixed 8 groups | Nice | D |
| Comp point edit (X/Y) | Yes | Yes | Have | — |
| Comp depth edit | Free 3D | Option/Alt-drag Z | Partial | A |
| Multi-select points | Shift / box | Shift + multi-drag | **Must** | A (done) |
| Box select | Cmd/Ctrl drag | Cmd/Ctrl marquee | **Must** | A (done) |
| Line selection mode | Explicit mode | Click lattice line | **Must** | A (done) |
| Object/surface selection mode | Yes | No | Nice | A |
| PRS transform gizmos | Pos/Rot/Scale | Translate axes (local) | **Must** | A (partial) |
| Gizmo local / world space | Yes | No | Nice | A |
| Active-surface-driven UI | Selected surface params | All 8 always visible | Nice | A |
| Point → Null attach (relative) | Parent-like attach | Root-relative marker rig | **Must** | B (done) |
| Multi-point → one Null | Line attach | Script row/col issue | Partial | A |
| Surface Root rigid motion | Surface attach | Surface Root bind v4 | Partial | — |
| Attach time offset | Yes | No | Nice | B |
| Roll / spiral procedural bend | Core demo feature | Per-surface Angle/Tilt/Radius/Expand | **Must** | B (done core) |
| Roll control Null animation | Create control layer | No (keyframe params directly) | Nice | B later |
| Front/back materials | Yes | Single source | **Must** | C |
| Thickness + side faces | Yes | Fields unused | Nice | C |
| Image fit / UV offset | Fit/Fill/Original + transform | Stretch only path | **Must** | C |
| Texture time modes | Still / layer / source | Host time only | Nice | C |
| Ribbon chaining via shared Nulls | Explicit workflow | Manual possible | Nice | D |
| AE camera + lights | Default + optional internal | AE only | Have / Skip internal | E |
| Composition-space window host | Toggle | Host is 2D window | Partial / Have | — |
| Motion blur | Unknown / limited | Strong | **Keep (diverge)** | — |
| Render debug views | Limited | Finish/Depth/UV/Normal | **Keep** | — |
| GPU raster + AA | GPU + SS/MS | CPU SmartFX | Nice | E |
| Variable lattice density | Fixed-feel grid | Dx/Dy 1–16 + Mesh Quality | **Keep (diverge)** | — |

## Target golden scenes

These scenes define “close enough”:

1. **Unroll banner** — strip surface, Roll animated via control Null, optional
   line Nulls for secondary push.
2. **Point-rig flip** — edge/corner points attached to Nulls and keyframed in
   AE.
3. **Two-strip join** — two surfaces share an edge Null and UV offset so the
   texture continues.

## Phased delivery

### Phase A — Comp interaction (current branch focus)

Make the lattice feel editable like a real tool.

1. Multi-select points (Shift toggle; clear on empty click) — **done (A1)**
2. Box select (Cmd/Ctrl-drag marquee) — **done (A2)**
3. Line hit (click grid line → select free row/column) — **done (A2)**
4. Multi-point drag (same local delta; Option/Alt still depth) — **done (A1)**
5. Selection drawing (selected / Null-controlled / idle / marquee) — **done (A2)**
6. Translate gizmo at selection centroid (local X/Y/Z) — **done (A3 partial)**
7. Later A: rotate/scale gizmos; local/world toggle
8. Later A: active surface highlight + optional param focus

**Exit criteria**

- Select a vertical line of points and move them together in Comp.
- Shift-select corners and Option-drag depth as a group.
- Cmd/Ctrl-drag a marquee over free points and move the boxed set.

### Phase B — Attach semantics + Roll

1. Treat Null links as attach frames (relative offset), not only world snap
2. Script: “create parent” / line attach with optional move-to-layer
3. Roll evaluation layer on surface local frame:
   - angle, tilt, radius, radius expand per turn
4. Roll control Null bridge (animatable params living on a Null)
5. Unit tests for roll sampling and attach deltas

**Exit criteria**

- Unroll golden scene without hand-sculpting every lattice keyframe.

### Phase C — Materials / presentation

1. Back source (layer; use an AE Solid for color) — done
2. Image size modes — done; UV/image transform remains
3. Optional thickness (front/back first; sides next)
4. Specular + roughness + metalness controls — done
5. AE-light hard shadows (self + cross-surface) — done
6. Shadow diffusion / soft sampling — done

### Phase D — Surface operations

1. Better empty-slot workflow / hide unused surfaces
2. Chain helper script for shared edge Nulls + UV offset suggestion

### Phase E — Performance / polish

1. AA options
2. Optional GPU path
3. Internal camera/light only if AE defaults prove insufficient

## PR plan (branch `feature/foldspace-ux`)

| PR | Scope | Risk |
|---|---|---|
| **A1** | Docs + multi-select / multi-drag / selection draw | Done |
| **A2** | Box select + line hit | Done |
| **A3** | Translate gizmo at selection centroid | Done (rotate/scale later) |
| **B1** | Attach relative offsets in render/gizmo | Done |
| **B2** | Roll model + tests | Done |
| **B3** | Roll UI params (control Null later) | Done (params) |
| **C1** | Back material + image modes + surface response | Done |

Do not land B/C on top of half-finished A gizmos without golden-scene smoke.

## Non-goals (near term)

- Bezier control mesh replacement
- Full dynamic surface list schema rewrite
- 0.x migration revival
- Pixel-identical Foldspace UI chrome
- GPU rewrite before unroll works on CPU

## Implementation notes for A1

Current gizmo state is a single `GizmoDragState` in `SurfaceLabUI.cpp`.
A1 replaces that with:

- `SelectionState` holding many lattice point refs (same-surface first)
- click: replace selection; Shift-click: toggle
- drag: apply the primary hit’s screen→local Jacobian delta to every selected
  free point
- Null-controlled points remain non-editable and draw orange
- selected free points draw with a distinct highlight

Process-local selection is acceptable for A1 (matches current drag memory).
Persisted selection is not required.

## Verification

- `ctest` model suite remains green after any Model/Geometry change
- AE 2026 manual: multi-select move, Option depth group move, Null point still
  locked
- AE 2026 manual: move/rotate Surface Root, then issue another Point Null; it
  appears on the current mesh with its tangent orientation and remains rigidly
  attached when the Root moves again
- After B2: scripted unroll smoke in `work/`
