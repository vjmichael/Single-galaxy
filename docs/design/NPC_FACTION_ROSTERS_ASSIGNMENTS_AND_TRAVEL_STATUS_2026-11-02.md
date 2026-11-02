# Single Galaxy — NPC faction rosters, identity, assignments and travel status

**Date:** November 2, 2026
**Status:** Player-approved design direction. Implementation pending after DEMOG-04b1.

## Principle

A faction/corporation/colony roster is a **queryable membership and personnel index**, not a separate copy of the NPC. A globally unique stable NPC ID links to one authoritative identity and its family, appearance, occupation, personality and history. One NPC can belong to multiple organizations, but physical custody (where that NPC is) has one authoritative owner at a time.

## Records

- **NPC identity:** immutable ID, persistent family links, appearance/genome, learned occupations and personality. Never copy an NPC to a destination to simulate travel.
- **Roster memberships:** faction/organization ID, role, membership start/end, reputation/standing and optional authority/availability constraints. Many-to-many membership, no ownership of physical location.
- **Current custody:** one world/colony/ship/transit/lost/deceased custody owner; active movement reference and last committed event receipt. Do not use faction membership as location.
- **Assignment:** requested role, mission, origin, destination, host authorization/delegation and status, with conflict checks across missions.
- **Trip:** expedition ID, vessel ID, route, origin, destination, manifest ID, timestamps/ticks, supplies, receipt IDs and durable status.

## Status transitions

Eligible at origin → reserved for mission → boarded → in transit → arrived at destination (or canceled before launch / returned after safe abort / missing / deceased if lost). Treat reservation, boarding, departure and arrival as **distinct atomic and replay-safe events**. Derive user-facing status from canonical trip and custody records rather than maintaining contradictory free-form status strings.

## Selection and authorization

A planner may request skill slots (pilot, engineer, medic, construction, farmer, miner, etc.) and optional household grouping. Select from indexed available roster members, then atomically verify eligibility, policy permissions, ship capacity, physical stock, departure site, and source staffing minima. Roster selection is **advice**, not permission to move people. Selection respects actual family links and voluntary migration preferences under delegated NPC authority. Players can inspect and adjust policies and expedition manifests.

## Data integrity and scale

Enforce uniqueness of an NPC's active custody and exclusive voyage assignment; preserve original identity across regions and save/restart. All departures/arrivals are idempotent with receipts. Distant population simulation uses cohorts, with selectively activated named individuals and a cohort-to-identity accounting reconciliation: named records cannot be double counted on top of aggregate population. A roster may display only activated named NPCs plus aggregate workforce counts; do not materialize millions of detailed individuals for a menu.

## Next integration work

1. DEMOG-04b2: bind the expedition manifest to canonical `PersistentWorld` identity records, actual ship/cargo inventory, and ART-02b1 family records, with one authority for headcount and materials.
2. Add read-only faction membership/eligibility queries and deterministic crew slot matching, keeping selection separate from host-authorized boarding.
3. Add durable event-derived travel statuses, destination and ETA in the NPC roster UI, with exact receipts and recovery.
4. Test concurrent roster selection, mixed named/cohort residents, household travel, cold/warm regional transitions, save/restart, ship loss and multi-faction memberships.

**Current reality:** DEMOG-04b1 has named custody inside an isolated SQLite expedition prototype. There is no verified native faction roster implementation or gameplay UI from this document alone.
