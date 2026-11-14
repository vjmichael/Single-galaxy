# DISPOSITION-03C — Territorial Travel and Facility Clearance

**Source state (2026-11-06):** native authorization adapter and regression committed on top of DISPOSITION-03B draft #77. CI pending. Production route/docking/Fold/Hyperlane entry command integration NOT complete.

## Player behavior
Territorial hailing is customary, defaults to automatic outbound hail (DISPOSITION-03B), and can be disabled. Failure to initiate or answer a hail may block use of *colony-controlled* service infrastructure, deny a colony approach/docking request, or block a managed route reservation; these are administrative restrictions, not an invented crime verdict.

A territorial restriction **must not** disable normal manual thrust, warp breakaway, emergency rescue or physics. A colony cannot veto another operator's public Fold Gate or hyperlane just because the gate lies nearby.

## Native authorization contract
`TerritorialTravelClearance.hpp` adds `territorial_service_clearance`. It takes the original authenticated player/NPC owner and ship, the controlling colony, an asset-control proof supplied by the trusted host, service type, and invitation-required designation. Incorrect owner or absent infrastructure-control proof yields `NotAuthorizedToDecide`, **never** a presumed denial.

For a verified colony-controlled asset, genuine current expulsion/trespass denies, invitation-required routes demand actual invitation, and unverified/unanswered customs hails block ordinary services unless the visitor is invited. A valid visitor acknowledgement permits non-invitation services, but acknowledging a warning does not create access rights to private assets. The result has no power to block free flight.

Both NPC and player route executors must invoke the same gate before a controlled facility operation. This policy is not yet called by the existing GateTransit engine, which lacks a per-gate original territory-ownership and authenticated host binding. That integration must prove its owner and invoke this decision **inside** the authoritative transition function before it mutates ship state. Future hyperlane and Fold implementations must likewise apply at actual controlled entries and not at arbitrary waypoints or emergency exits.

## Testing
`DISPOSITION03C_TravelClearanceTests.cpp` exercises authentic colony and foreign ship, no-service-before-hail, owner mismatch rejection, false asset-control rejection, physical flight unaffected, acknowledged warning, invitation-required service, invitation and revocation. CI includes Windows/Linux native regression via DISPOSITION-03B workflow.

## Outstanding
Integrate verified asset ownership and control (gate, lane, colony services), host-authenticated player commands, stateful boundary-entry/counterparty communications, persisted preference/UI, and complete route executor checks. Hailing alone is not verified receipt, and a travel restriction is not a combat license.
