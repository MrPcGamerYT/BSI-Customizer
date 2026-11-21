# Pinned full-UI transaction dependency

`legacy_installer.py` is the unmodified standalone installer from this repository's
`bc4c68522878f7c060e82a3524e127099d4b0c54` full-UI ZIP. `PROVENANCE.json` pins its
SHA256 and archive Git blob. It embeds the established PE/RCC mapper, cache-owner
validation, source contracts, transaction journal and restoration logic. Suite07
wraps this reviewed implementation rather than inventing an unsafe byte scanner.
No historical helper EXE or player binary is loaded from it. Its old asset payload
is overlaid with the current suite, including the strict V3 page, before planning.

`native/` retains the historical source-only fixture snapshot and its original
manifest/provenance. These are used in synthetic PE tests, never executed as a
player. Synthetic fixtures are not emulator compatibility certification.

Updates to this pin require source review and a new provenance digest. Runtime
patches/visual adapters live in `tools/full-emulator-ui/suite.py` and are tested
separately. The legacy protected boot/auth/shared dependency rules remain active.
