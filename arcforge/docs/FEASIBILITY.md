# Feasibility

## Verdict
**Feasible as a bounded DSL + deterministic compiler. Unproven as an art pipeline.**
The engineering is straightforward and already demonstrated. The open question is whether recipe-composed pixel art looks good enough for real games. That is a content/taste risk, not a software risk, and it is cheap to test early.

## What M0 proved (executable, in this repo)
- A strict JSON language with archetype + typed recipe parameters + `.px` escape hatch validates and compiles.
- Deterministic rendering with integer-only raster math (golden hashes stable across runs).
- Structured diagnostics with suggestions and JSON-Patch fixes for unknown recipe, bad parameter, slot conflict, missing anchor, unknown template, unsupported canvas, and rejected free-text fields.
- Rigid-part animation (idle/walk/attack) from archetype templates; recipes follow their anchor's part.
- Structural tests replace most vision-model checks: the compiler knows what it placed.
- Palette budgeting (16 colours here) is enforced mechanically.
- Cost of the spike: roughly one focused session for the whole loop.

## What M0 did NOT prove
- **Aesthetic quality.** The rendered knight is recognisable and readable but crude: arms/shoulders merge into a blob, the cape reads as thin trim, no interior shading logic. Procedural composition at 32×32 needs hand-tuned recipes and an inner-outline pass. Expect the first 10 recipes to take several tune-render-look cycles each.
- **Recipe interaction at scale.** Five recipes on one archetype barely touch. Overlaps, z-order, and anchor shifts grow combinatorially.
- **Animation depth.** Rigid translation only. No rotation, squash, secondary motion (cape lag), or death/hurt.
- **Real AI use.** The discovery/repair loop is specified and the diagnostics exist, but no model has driven it yet.

## Risks, ranked
1. **Looks generic or ugly** (high likelihood, high impact). Mitigation: gate G1 below; recipes are authored by a human with taste (or reviewed against a style bible); style pack carries the look.
2. **Recipe library cost** (certain, medium). Budget 2–4 focused hours per good recipe including tuning. 40 recipes ≈ 80–160 h. The library is the product.
3. **Scope creep into a general sprite editor** (medium). Hold the line: bounded vocabulary, `.px` for the rest.
4. **No consumer** (medium). Build only what a real sprite-based game needs. Vector/code-drawn games do not need ArcForge.
5. **Two implementations drifting** (low). Golden hashes + shared fixtures are the contract.

## Decision gates
- **G1 (end of M1, ~40 h in):** produce 6 different heroes/enemies by changing only JSON (no new recipes). Review blind with the human. Pass = at least 4 of 6 are acceptable as prototype art. Fail = stop recipe work; pivot to a kit-bash model (recipes become parameterised `.px` stamps with palette roles) or hand-drawn `.px` with ArcForge only as palette/anim/export tooling.
- **G2 (mid-M2):** if average recipe cost exceeds ~4 h, cap the library at ~25 recipes and rely on `.px` fragments (including image-model → quantize → `.px` import) for the long tail.
- **G3 (before M4):** do not build the C compiler or Arcade-1 export until a game is actually consuming assets.

## Is it worth doing?
Yes if there is a sprite-based game that needs characters, enemies and effects and no artist: the platformer milestone on the ARCADE-1 roadmap is the natural first customer. No if the games stay vector/code-drawn. Doing M1 first answers the question with a bounded spend.

## Effort (focused hours, rough; ±50%)
| Milestone | Hours | Output |
|---|---|---|
| M0 spec + reference spike | done | this repo |
| M1 vertical slice to taste + gate G1 | 25–40 | hero that looks good; 8–10 recipes; inner outlines |
| M2 breadth | 60–100 | 2nd archetype, 20+ recipes, 2nd style pack, PNG→.px import |
| M3 animation depth | 30–50 | hooks, variants, hurt/death |
| X0 variable-size core (`spec-next`) | 20–30 | metrics, expression anchors, `percent`, size sweep lint |
| X1 language extensions (`spec-next`) | 25–40 | `mirror`/`repeat`/`variants`/`extends`, locals, assertions |
| M4 C compiler + ARCADE-1 export | 90–130 | after asset format exists |
| M5 AI tool interface (MCP) + vision loop | 15–25 | model drives the loop |
| **Total (M1–M5 incl. X0/X1)** | **265–415** | M0 done |
