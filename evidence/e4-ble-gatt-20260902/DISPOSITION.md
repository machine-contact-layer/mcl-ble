# Owner disposition: `board-serial.log`

**Decision: publish as-is, with the discrepancy preserved and disclosed.**

## The discrepancy

The digest originally recorded for `board-serial.log` in this directory
matches neither the committed bytes nor any line-ending variant of them.

The CRLF normalisation that explains every other corrected digest in this
repository does **not** explain this one. It was tested and ruled out. The
recorded value and the committed file disagree for a reason no surviving
record establishes.

`SHA256SUMS.txt` carries both values: the digest of the bytes actually
committed, and, in the trailing comment block, the digest as first recorded.
Neither has been removed.

## Disposition

The historical provenance of `board-serial.log` is **unresolved**.

This file **MUST NOT** be treated as independently integrity-verified
evidence. Its chain of custody from capture to commit cannot be demonstrated,
and no claim in this release may rest on it.

It is retained and published because deleting an imperfect artifact, or
regenerating its digest so the chain appears to verify, would be worse than
disclosing that it does not. A reader can see exactly what is known and what
is not.

## What carries the release's claims instead

No claim in `mcl-core/releases/v1.0.0/EVIDENCE_INDEX.json` cites this file.
That was verified against the index, not assumed. The release's
physical-carriage claims rest on:

  - `mcl-sdk/evidence/e4-dual-transport-migration-20260903/` — continuity
    across 104 migrations, both radios live on both peers
  - `mcl-ap/experiments/008-embedded-node/` — Wire 1 in Link 1 decoded over
    air with no host in the loop
  - `mcl-sdk/hardware/dfr1154-autonomous-node/runs/20260909-contention-closure/`
    — the three-party contention campaign

Each of those verifies against its own recorded digests under
`mcl-core/tools/check-evidence-digests.sh`.

## What is not affected

`host-output.txt` in this directory verifies normally, and the BLE-GATT
result this directory records is not withdrawn. The unresolved item is the
integrity chain of one serial log, not the experiment.

The raw log itself is unmodified and will stay that way. This record exists
so that the discrepancy travels with the evidence rather than being settled
quietly.
