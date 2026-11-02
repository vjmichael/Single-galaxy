# ECON-03c10 — Active economic region reconstruction (November 1, 2026)

Status: **locally implemented and tested** against the ECON-03c9 source tree. Source patch and full source ZIP delivered as conversation artifacts; this GitHub milestone document **is not a claim that the source patch has been committed**.

## What actually works
- Durable materialization of bounded facility and named-carrier records from actual regional SQLite site, population and fleet data. Eight-colony scenario: 36 active records per visited pair and two million residents represented as aggregate cohorts.
- Warm economic regions remain Detailed for the full player visit interval. While detailed, mining, farming, recycling, smelting, fabrication, habitat supply, carrier departures and arrivals execute through the **same authoritative finite-stock native economic ledger** as the cold scheduler.
- Owner checks require stable mode, current generation, matching durable warm identity manifest and host policy. No duplicated independently spendable inventory exists in the warm projection.
- Carrier position is reconstructed from durable flight endpoints and departure/arrival/return clocks (approximate interpolation, not physics or rendered motion).
- Cold→Detailed stages the identities atomically with the owner flip. Detailed→Cold validates identity/epoch, journals the handoff and removes the nonspendable active projection in the same SQLite transaction.
- Forced SQL failure during materialization leaves the original Cold owner and inventory intact; abort and retry recover. Three restart points reopen active warm state.

## Eight-hour accelerated tests
Travel: A at hour 0, B at 1, C at 2, D at 3, C at 4, B at 5, A at 6 through 8. Regions contain two colonies of one million represented residents each.

| Metric | Paired-only trade | All-to-all trade |
| --- | ---: | ---: |
| Total native work batches | 256 | 256 |
| Native batches executed under Detailed authority | 64 | 64 |
| Durable warm reconstructions | 7 | 7 |
| Owner flips | 13 | 13 |
| Freight dispatches | 56 | 112 |
| Freight deliveries | 56 | 111 |
| Still in-transit at 8h | 0 | 1 |
| Moving carrier samples reconstructed | 0 | 78 |
| Material conserved | 1,536,000,000 kg | 1,536,000,000 kg |
| Stale writer rejections | 13 | 13 |
| Database reopen cycles | 3 | 3 |

Both scenarios pass in Release, and two ASan/UBSan regional runs pass using the current regional sources linked against the previously sanitized unchanged game kernel. All 8 focused native regional regressions pass. Byte-identical hourly snapshots match the earlier cold-only eight-hour scenarios and reproduce on repeat, proving neutral visitation does not alter the economy. Full sanitizer CMake build was interrupted and is not counted as green. The two known legacy B65 live-relay QA failures remain unaddressed.

## What does NOT yet work
This is **active economic reconstruction**, not a complete warm visual/playable `PersistentWorld` reconstruction. There is no rendered 3D scene loading, no one-million-individual NPC activation, no actual player ship flight between pairs, and no separately attested `PersistentWorld` player/NPC activity influencing the regional economy. This is a critical acceptance gate, not a completed feature. The next integration must bind selected genuine detailed NPC and player activity receipts to one physical inventory truth while retaining the million-person cohort model.

## Artifact provenance
Full source ZIP SHA-256 `24bb7cbf7c3ab5821a51c9650027c1b2e9ff6ae617501b029157b0a435558fdd`.
Patch SHA-256 `17314ab1181b2f86e24d36034c4f500a21182f0cb460a3fdf951189c8ff3e1e3`; checked to apply to the exact local ECON-03c9 baseline.
Evidence ZIP SHA-256 `649fe987d65490b3c99e961054a6a06548c3645a28691746ca22c3bf7aa70a9d`.

**Repository policy:** GitHub is repository and milestone record only. Development/testing are local.
