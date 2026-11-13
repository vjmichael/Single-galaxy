# ACTIVE STAGE 6 FEATURE-CLOSEOUT PLAN — 2026-11-12

## 2026-11-13 evidence update (supersedes the earlier provisional native status)

The original run [#53](https://github.com/vjmichael/Single-galaxy/actions/runs/43455824013) on source `64f3c6a` compiled on Windows and Linux; each ran **38/39 native tests successfully**. The sole failure was D7's positive freight scenario before mining completed: original command ID 1, tick 1, `InvalidArgument`. It was a legacy 80-tonne `Mine` request against snapshot v126's mandatory one-tonne mining, not a missing receipt. The **current repair** is committed on the same Stage 6 branch at `b1272a2`, with finite fitted drill and onboard pilot and unique one-tonne requests. D7 new native outcome remains pending; independent receipt-contract CTest is mandatory in consolidated batch. Original historical v85 SQLite, E6, F, G and native worker custody gates passed at the prior SHA but **must reconcile at a single new SHA**. Installed-human and physical GPU gates remain separate; no repeated Radeon check blocks feature work.


**Sequence: S6-D/E → S6-F → S6-G → functional closeout.** The user explicitly prioritizes completing gameplay features before another physical PC validation. Preserve older Godot/GPU gameplay evidence. Physical Radeon retests are **not a gate** unless a changed graphics/hardware path actually demonstrates a regression. Hosted NSIS checks are manually opt-in after feature implementation, and no release or main merge is authorized.

## Work still outstanding
- **D6 regional water/soil:** Original physical upstream/downstream transfer exists. A new source-backed native test checks positive 20-tick watershed transfer between funded adjacent regions, original resource conservation, canonical reparse and SQLite reopen. This new test is pending native Windows/Linux validation.
- **D7 geology freight:** The original extractor and v136 freight schema exist. Still integrate one *positive* native sequence from a genuinely trained Shape worker and commissioned, powered site through original regional source extraction, landed shuttle load, genuine ascent, ship cargo credit, replay/rollback and journal readback. Do **not** seed fake extracted goods in tests.
- **E6 ecology:** Seasonal field/controlled crop budgets, hazard/soil/rest and power constraints exist. Still exercise a complete original physical farm with real resources, qualified deployed workers, actual cycles, ecological shortages, and durable recovery. Complete missing ecological behavior demonstrated by that case, rather than inventing an unlimited ecosystem scope.
- **F planetary gameplay:** Original native terrain, gravity/survival, role recovery and NPC movement implementations exist. Extend F5-D regression with oversized NPC move, forged named-NPC ID and replay denial in addition to verified movement/restart. Reconcile source-authoritative Godot packet/mesh and native medical/suit/atmosphere integration; no broad GPU recheck for unchanged renderer.
- **G consolidation:** Historical exact-head 8ee71f0 had Windows/Linux 35/35. New heads require exact-head native acceptance. Current SGDX supplemental Linux test showed a real protocol-test mismatch (extra ROCKNONE before NAV); correlate telemetry by session and request without dropping navigation verification. Continue independent multi-cell process-custody and actual 1,024+ tick worker load/restart. Installed/physical Radeon remains separately deferred, not a Stage 6 feature blocker.

**Closeout means completed D/E/F function, repaired native acceptance at one Windows/Linux SHA, and genuine original worker/journal custody. It does not mean repeatedly revalidating proven GPU hardware.** Stage 7 remains separate.

---

# S6-G functional closeout: D7 freight, E6 ecology, F5-D named-NPC movement

Date: 2026-11-12. Branch:
`sg/stage6f5e-installed-terrain-recovery-20261112`.

**Status: source staged; native compiler, acceptance matrix, installed
Windows/Godot, physical GPU and human acceptance NOT YET VERIFIED.**
No PR merge or release authorized. A staged CTest is not a PASS receipt.

## D6 — Actual cross-region physical runoff and erosion

- The existing v127 original genesis and v128 exact physical subaccounts are
  the only physical sources. `process_physical_planetary_environment` uses
  authoritative watershed drainage edges at committed 20-tick boundaries.
- Bounded surface-liquid, aquifer and retained-sediment kg travel only to
  already funded adjacent physical source cells. Region `stock_kg` debits and
  credits are made with corresponding phase account updates.
- Candidate flux plans reserve downstream capacity before mutations.
  `total_ore()` must remain identical. No rain forecast, noise source,
  unregistered surface or presumed new lake is converted into material.
- Test: `galaxy_stage06_physical_environment` (positive/negative flux),
  `galaxy_stage06g_consolidated_migration` (1,024 committed source ticks).
  Multi-region allocation and negative power-loss cases still need actual run.

## D7 — Source-backed orbital geological freight (snapshot v136)

- The existing v129 commissioned source extractor and named worker must first
  move original geology into the `PlanetarySiteFacility.regional_geology_kg`
  subinventory. D7 **cannot** independently mine or conjure any of it.
- An authenticated, owner-approved `PLANET_GEO_FREIGHT_LOAD` only moves 1–1000
  kg of SOIL, MINERAL or VOLATILE from that same original source site to the
  one real **landed** shuttle with genuine descent receipt and a 1,000-kg
  combined cargo limit.
- A separate authenticated `PLANET_GEO_FREIGHT_UNLOAD` can move that already
  carried material to the **original orbiting source ship** only after the
  shuttle has physically completed ascent and has an original ascent receipt.
  The ship's actual inventory volume and item catalogue determine admission.
- Both legs are original `PersistentWorld` candidate-and-commit transactions
  with unchanged resource origin, SQLite generation, failure injection and
  distinct persistent globally protected request IDs.
- Canonical original-world v136 adds 3 geological hold subaccounts to
  `planetshuttle` and exactly 3 recognized `good/planetary_*` ship codes.
  V135 and earlier remain readable. The v136 header, decoder, field counts,
  physical hold cap and total-material accounting must agree exactly.
- The authenticated native colony host, `interactive_relay.py`, and
  `viewer/colony.js` now allow explicit source-approved load and unload
  decisions, displaying a strict receipt without claiming an instant delivery.
- Tests: `galaxy_stage06d7_geological_freight` (genuine original constructed
  shuttle, no synthetic positive cargo; schema/recovery and negative gating),
  `PLAY13D_PlanetSurveyNodeTests.js` (strict receipt and relay integration).
- **Still open:** actual full positive qualified-worker extraction -> landed
  loading -> real ascent -> original ship unloading -> restart/rollback in one
  true end-to-end native case. It must not be substituted with fake test mass.

## E6 — Seasonal ecological source accounting

- The existing v130 physical growing facility, native biosphere, real topsoil,
  actual water, nutrients and powered controlled farm remain authoritative.
- Field crops have a bounded rest interval, seasonal variation and conservative
  erosion/fatigue penalties. Reduced edible output stays in existing
  wastewater; exactly 15 kg of original inputs remains 15 kg of output.
- This is crop-ecology expansion, **not** a complete food web, biodiversity,
  soil chemistry or multi-species ecological succession simulation.
- Test: `galaxy_stage06_ecology_industry` with real/cold/hot/winter/fallow,
  no native biosphere, erosion and conservation cases.

## F5-D — Original named-NPC on-foot native/installed bridge

- The existing `planetary_surface_move(owner,ship,key,npc,dx,dz,receipt)`
  remains the **only** authority for bodies, collision, physical location,
  canonical planet tiles, 250-mm NPC steps, replay and source state.
- The existing supervised `galaxy_desktop_player_host --planet` emits
  `NROSTER` only for actual original named residents on the same owned site.
  `NMOVE` requests an actual bounded original SQLite transfer; `NACK`
  contains only the committed NPC, x/z, tick, generation and receipt.
- Installed Godot accepts only the native roster and committed results.
  The on-foot operator selects via **N** and proposes a 200-mm step via
  **Shift+arrow**. Godot cannot create NPCs or author absolute positions.
- Test: `galaxy_stage06g_original_named_npc_movement` on actual original
  landed SQLite fixture, wrong-epoch refusal, successful native movement,
  same-original-world restart and rejected stale session. Existing F5E host
  recovery fixture is updated to account for NROSTER.

## One consolidated native acceptance pass

Use `tests/Stage06GNativeAcceptanceBatch.py` against a clean exact source
HEAD and a preconfigured Windows or Linux native CMake build. The suite
requires the D7, D6, E6, F4–F6, G, WO2–WO5 and DOCK integration tests.
Failpoints, deterministic mass, original SQLite recovery, exact source SHA,
host auth, and test-discovery failures are strict failure conditions. Receipts
show the exact SHA and retain compiler/test stdout/stderr tails.

**A runner not executed is NOT accepted.** A pass on hosted Windows/Linux
workers demonstrates native acceptance only; it cannot establish physical
Windows SCM/AppContainer/Job Objects, attached GPU performance, or a human
clicking through a genuinely installed game.

## Separate Windows installed acceptance, operator gate

Perform on a real Windows installation of the compiled exact source, with
the original player world saved and a verified SGDX supervisor and authentic
installed Godot content:

1. Verify compiled executable/process hashes, installed build SHA, native
   supervisor admission and durable process-tree custody; prohibit two
   original SQLite writers.
2. Load an existing genuine save. Observe actual GFRAME state, finite
   genesis, original cargo, original named NPC and native planet custody.
3. Execute a qualified source extraction followed by physically landed
   geological load, shuttle ascent, original ship unload and native readback.
   Reopen; assert precise kg, original source debit, generation and receipts.
4. As an actually landed player, press N and Shift+arrow with a real present
   named NPC. Check changed native C++/SQLite coordinates versus Godot view,
   refusal for stale/foreign IDs, restart persistence, and player/NPC
   collision constraints. Test Godot script imports before real play.
5. Disconnect/kill/restart mid-transfer and while walking. Inspect the
   original journal and uncertain actions; never replay unapproved freight or
   spawn replacement NPCs or supplies.
6. Run sustained long-world multiple-cell/environment cycles and record
   physical GPU frame pacing, load responsiveness and operator observations.
   Preserve the original work under test; never auto-reset the save.

No installed/human/physical Radeon evidence has been supplied for this head.
These six gates remain **explicitly open**; acceptance must include exact
installed version, measured outcomes, operator confirmation and receipts.
