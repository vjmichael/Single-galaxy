# Single Galaxy — B7.0m Policy Execution Capacity, First Native Slice

**Date:** 2026-10-31  
**Scope:** Existing deterministic Linux 2D Single Galaxy simulation from B7.0l; source, live host, authenticated relay, read-only dashboard and tests.  
**Status:** Implemented and tested locally on Linux. Windows-native / graphical Chromium not verified.

## Player-facing capability

The player can set a standing **assurance mandate** (`None`, `Lax`, `Medium`, `Strict`) independently for maintenance, healthcare, community services, and waterworks at a player-built, player-owned inhabited outpost. They can separately assign zero to three **oversight workers** to each area from the actual on-site residents. No administration, population or supplies are created by turning on a policy. The same resident cannot simultaneously be an inspector and a maintenance technician, medic or community attendant: cross-role capacity is checked on new assignments and resident departures. The existing native utility/clinic/community/waterworks actions still control the actual services.

A new `POLICY_AUDIT|site|domain|declared|inspectors|required|operational|staffed|supported` frame exposes **three different things**: the player's standing demand; available administrative headcount; and observed physical service conditions. `assurance_supported` can be true only when the mandate is active, enough inspectors are assigned and the service has relevant sustained operating evidence. `None` never counts as verified enforcement. Enforcement *capacity* and successful operating *evidence* are reported separately, not averaged into a vague colony fitness score.

The exact staffing requirement, one inspector per assurance step, is **temporary game balance**. A highly trained formal government or schedule system has not yet been implemented. No claim is made that an inspector actively catches a policy violation, handles immigration, polices citizens, or gives the player a universal success bonus.

## Implemented architecture

- **C++ simulation:** `PolicySite` and `PolicyAudit` records, four extensible domain enums, two commands `SetPolicyAssurance` and `AssignPolicyInspectors`. Existing colony-owner, same-system, physical distance, and active-ship permissions apply. Issuer spoofing, invalid domain/value, and overstaffing reject. The `assess_policies()` readout is computed from authoritative service state rather than client flags.
- **Labor allocation:** Oversight staff are reserved within housed resident counts; utility-technician, clinic-medic, community-attendant and habitat-departure commands cannot oversubscribe existing inspectors. Inspectors consume labor capacity, not physical matter. Migration/individual skills remain future work.
- **Persistence:** Canonical v19 snapshot stores each site policy, v1–v18 read compatibility, queued new actions, and deterministic journal replay. Recovery validates domain, staffing, ownership, policy sites and command envelopes. No extra resource-ledger receipt is invented because policy changes transfer no material mass.
- **Host/relay/UI:** Local host `POLICY_SET`/`POLICY_STAFF`, authenticated exact-schema JSON `colonyPolicySet`/`colonyPolicyStaff`; browser has a policy editor and audit text, with strict frame validation in `viewer/colony.js`. No privileged admin escape hatch or auto-growth control.

## Test evidence

- New B7.0m native integration test: authenticated owner versus forged issuer, strict policy with no staff, staffing with no service, sufficient staffed operational utility, cross-role overcommitment, protection against resident withdrawal, mass and population conservation, queued snapshot v19, snapshot tamper rejection, out-of-range command, SQLite original and independent copy recovery.
- New B7.0m live-host + local HTTP relay test: real persisted host command/telemetry/restart, exact allowlist, rejects malformed and untrusted origin, invalid session token, extra JSON fields and wrong types.
- Three new JavaScript tests: valid and contradictory telemetry decoding, UI/relay/host wiring.
- **53/53 native/integration tests passed** across four bounded CTest groups (1–13, 14–26, 27–40, 41–53; not one monolithic run).
- **84/84 Node JavaScript tests passed**.
- **Production-style Linux build passed** with `BUILD_TESTING=OFF`, `SG_QA_AUTOMATION=OFF`.
- Clean-source extraction manifest verified **379 files**; clean-copy C++ compile, **2/2** B7.0m native/live tests, and **3/3** B7.0m JavaScript tests passed. Supporting logs are bundled as `docs/B70m_Clean_*`.

## Non-claims / next milestones

B7.0m is the **first policy execution-capacity slice**, not full governance. It does **not** yet implement actual violation detection, sanctions, inspectors' schedules or job-specific XP, collective workforce proficiency, organization maturation, immigration permits, migration policy enforcement, independent NPC settlements, individual satisfaction or personalities, indefinite water/oxygen renewal, private-server catch-up, genuine 3D flight, autonomous growth, Windows build, or verified live Chromium clicks. `assurance_supported` is not a declaration of overall self-sufficiency.

**Next B7.0m subgate:** individual/collective workforce proficiency, staged adoption and measurable oversight workloads; make policy audits influence only outcomes with actually implemented causal mechanisms. Immigration/departure rules remain a later feature, once independent populations exist.

**Continuity contract:** one finite-crewed player-founded ship in an otherwise uninhabited starting world; the player keeps ultimate authority over player-founded colonies even after long absence. NPC work/settlement migration and governance may emerge later, using real people, stock and travel. No artificial happy-NPC spawning, arbitrary inactivity punishment, or instant colonization because of a single genius manager.
