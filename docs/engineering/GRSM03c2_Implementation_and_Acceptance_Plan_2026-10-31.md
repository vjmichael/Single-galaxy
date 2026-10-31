# GRSM-03c.2 — Bounded NPC Encounters and Provenance-Safe Information Routes

**October 31, 2026 — engineering plan; implementation not yet accepted**

## Baseline as of GRSM-03c.1

- SQLite v10 records append-only group membership transitions with historical epochs. Host-attested founding/member contact and claim relays are implemented; no automatic meetings or gossip.
- GRSM-03b implements directed trust founded on actual co-location. The physical event ledger and NPC claims remain distinct.
- The final GRSM-03c.1 Linux native build passed. Targeted 5/5 native suites, 84/84 JavaScript and 3/3 Python design tests passed.
- **Full native suite remains open:** one pre-final-guard 60/60 pass, but subsequent final-code runs reported 59/60 (-j4), 59/60 (-j2), and 58/60 (serial) due to varying older live-relay readiness/state-delivery failures; failed cases passed in isolation. Cause is not yet established. Preserve logs rather than misrepresent the full gate as green.

## 03c.2 implementation boundary

1. **Opportunity, not omniscience:** An encounter is eligible only when both NPCs exist, survive, and have demonstrable co-location in the authoritative world. An encounter alone must not copy knowledge or trust, change group membership, move cargo, or invent witnesses.
2. **Host-supervised bounded scheduler:** Process a capped subset of living NPC pairs on an explicitly invoked host cycle. Use stable identity ordering, a bounded work budget, reproducible cooldown windows, and replay-safe duplicate suppression. Do not sweep all galaxy NPCs every tick.
3. **Durable receipts:** Record scheduled encounter contacts with tick/generation, NPC identities, and an append-only integrity chain. Validate row order, provenance, temporal monotonicity, cooldown, digest tips and migration boundaries on restore; do not manufacture historical meetings during migration.
4. **Selective effect:** Any resulting trust/contact update must route through already validated GRSM-03b APIs or an equivalent transaction-safe operation. No implicit secondhand testimony, universal group consensus, or automatic historic-fact changes.
5. **Travel/network isolation:** In the absence of a real route or configured communications infrastructure, no intercolony encounter, membership sponsorship, or testimony transit. Real communications routing is deferred unless proven with connection/capacity/authorization receipts.
6. **Respect autonomy:** No game requirement for daily logins. No always-running population-wide scheduler; event-driven and locally bounded work.

## Acceptance gates

- **03c.2-A:** deterministic capped contact opportunities and idempotent invocation; enforce real survivor/co-location; no duplicate or spurious contacts.
- **03c.2-B:** durable restore/migration/recovery/tamper tests; physically impossible and remote contacts rejected; world resource/casualty invariants retained.
- **03c.2-C:** stable/traceable legacy live-relay test readiness diagnosis; distinguish port collision, process startup, and destination-state timing. Full final-code native CTest pass required for complete green designation.
- **03c.2-D:** native, Node, and design test evidence preserved locally; source committed only after clear test qualification; README and next-gates documents updated.

## Deferred intentionally

Actual autonomous NPC transport, cross-colony messaging, institution-wide reputation consensus, AP​​S galaxy-scale sampling, fully dynamic factions, spontaneous dialogue, and folklore are separate gates. No external EVEmu code is introduced; original Single Galaxy remains isolated from Lu and Cognitum.

Development is local-first. GitHub records verified source, test evidence and open issues, not the runtime development hub.
