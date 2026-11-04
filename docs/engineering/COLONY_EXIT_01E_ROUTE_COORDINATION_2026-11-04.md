# COLONY-EXIT-01E.A — Host route coordination preflight (2026-11-04)

This is the **first, deliberately bounded implementation slice**, stacked on EXIT-01D PR #30. It introduces `galaxy::colony_routes::Coordinator` as a standard-library-only route selection boundary, with deterministic candidate ordering, stale-generation/expiry/owner and capacity refusal, duplicate route/identifier suppression, a hard 64-offer budget, and three separate host callbacks: authentic receiving-owner approval, physical/policy preflight, and authoritative native admission/boarding. It does not use invented routes, public-client approvals or an NPC election.

The coordinator is **not yet wired into the desktop host** because the existing host has no authenticated receiving-owner permit provider. Installing this component without that provider would either silently yield no offers or manufacture foreign-colony consent. The existing `PersistentWorld::supervise_voluntary_migration` remains the sole path for actual durable EXIT-01B admission and world-command boarding, with ART02/ECON03 custody at settlement. The dispatcher must forward the exact `HostVoluntaryImmigrationOffer`, verify the returned status, and never treat a queued boarding as settled migration.

## Next integration gates

1. Native owner-authenticated immigration policy/permit provider with durable grant/revocation epochs (Open/Closed, filters); broker must check receiving-player identity rather than trust arbitrary supplied owner IDs.
2. Read-only host inventory of actual known stargates/ferries, finite free seats, real route reachability and immutable per-generation reservations; atomic allocation and CAS reuse under the world SQLite writer.
3. Wire the native desktop host to the provider and coordinator; repair route failures and cancellations without changing established historical permissions.
4. Full lifecycle supervisor: real boarding, journey, arrival and atomic settlement; dependent families stay blocked until complete legal guardian and physical custody guarantees are available.

## Verification limits

`tests/COLONY_EXIT01E_RouteCoordinatorTests.cpp` provides native pure-coordinator acceptance; CMake registers it under BUILD_TESTING. The tests do not claim an actual world voyage, durable route lease, authenticated permit backend, or end-to-end automation. Do not merge this branch as finished EXIT-01E until those gates and CI pass.
