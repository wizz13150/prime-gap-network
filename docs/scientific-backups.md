# Private scientific backups

Use `tools/export_scientific_results.py` to export explicitly selected completed Horizon tickets. The source folders, active work, wallet keys and blockchain files are never changed. The tool accepts both original completed sessions and verified compact archives.

`--output backup.zip --ticket /absolute/path/tickets/TICKET_HASH --genesis GENESIS_HASH` creates a new ZIP. Repeat `--ticket` for a batch of at most 512 tickets. `--verify backup.zip --genesis GENESIS_HASH` checks it offline without extracting files. Existing output files are never overwritten. Every decoded member is bounded and checked against the inventory; the complete export stays below 64 MiB. Use smaller batches if that budget is reached.

For a complete retained history, use a **backup directory** instead of a single ZIP:

```text
python tools/export_scientific_results.py --output-directory /new/private-backup --tickets-root /absolute/path/tickets --genesis GENESIS_HASH --public-name PGN
python tools/export_scientific_results.py --verify-directory /new/private-backup --genesis GENESIS_HASH
```

The tool enumerates the explicitly chosen `tickets` directory and preserves every recognized completed ticket in numbered ZIP files. It understands compact completed-ticket archives. Recognized unfinished tickets are excluded and counted; unrecognized non-ticket entries are counted separately. A malformed ticket, unknown ticket schema/state, changed source, duplicate ticket, missing shard, extra file, corrupted report or exceeded quota causes failure. No result is silently truncated to the first 512 tickets. For an explicit selection, combine `--output-directory` with repeated `--ticket` instead of `--tickets-root`.

Each ZIP retains the independently verifiable V1 contract and its 64 MiB decoded/storage limit. The set defaults to a **512 MiB aggregate storage and decoded quota**, adjustable explicitly with `--maximum-bytes`; this is a per-export quota, not a lifetime cap on retained tickets. An index is limited to 1 MiB and 4,096 shards. Exact cross-shard deduplication uses a temporary SQLite database capped at 128 MiB. Peak temporary storage can also include one in-progress shard. Verification uses OS temporary scratch, so the backup itself may be on read-only media.

`index.json` is written as the final commit marker in a newly reserved directory. Existing destinations are never replaced, including concurrent export attempts. An interruption before the index is published can leave an incomplete directory; verification rejects it. The index binds every shard's hash, counts, attribution and chain genesis, and hashes the globally sorted unique ticket IDs. It proves internal conservation of the selected local records, **not mathematical completeness of the search or network acceptance**. Selection is observed ticket by ticket while enumerating, rather than an atomic snapshot of a running network.

These are **private backups**: they preserve the computed counts and, when present, the sealed report and its nonce. Do not publish an unaudited report's backup. A local calculation and matching report commitment do not prove independent audit acceptance or payment. `--public-name` is an optional operator label, not proof of a network identity.

Reports containing a public discoverer name retain that field exactly, together with the original report root. Verification checks the committed name using the same canonical name rules as the report producer; changing the name invalidates the root. The optional backup label accepts current profile names up to 48 UTF-16 units, while existing 32-character ASCII backup labels remain readable. The backup label and the discoverer committed inside a report are separate values.

This command does not reset a chain, move a wallet, import results into another network or convert tPGN to PGN. Wallet recovery requires a separate wallet backup. A future network migration must separately account for accepted scientific records, attribution, completed/excluded search intervals and outstanding audit obligations.
