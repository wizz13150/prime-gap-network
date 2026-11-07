# Exact local gap observations

Completed scientific tickets now conserve their local gap-index rows in
`scientific-gaps.jsonl.gz` before the work index or acknowledged owner output
can be retired. The completed-ticket archive binds the file's SHA-256, size,
record count and full configuration fingerprint.

The first JSON line identifies the ticket, integer interval and collection
policy. Each subsequent row follows the header's column list:

`lower_prime, upper_prime, gap, sequence, centile, visible, count_ok, detail_ok`

Positions are exact decimal strings, including beyond 64 bits. The final line
contains the record count. Rows stream from a read-only SQLite snapshot in
increasing lower-position order; they are not limited to the Best 100. The
source flags are preserved, including unknown flags in older index formats.

These are **local observations before scientific finalization**, not a
primality certificate or proof of network acceptance. When a ticket also has
a checked `source-finalization.json`, its added/removed pairs describe the
correction from these observations to the finalized result. Keep those files
together. Counts below the original collection cutoff cannot be recovered.

Exact position and gap size allow later merit calculations using
`merit = gap / ln(lower_prime)` at the analyst's chosen precision. Histogram
widths are not fixed by this storage format.

The archive uses bounded streaming compression and is fully verified before
source cleanup. Failure, corruption or a storage-limit refusal retains the
remaining source files. Presentation readers make a lightweight availability
check; that check is not full scientific verification.

Older compact archives containing only counts and selected results do not
acquire missing observations retroactively. Further source retirement is held
for scientific tickets lacking the exact-pair archive.

These files may contain unrevealed audit information. They stay local and are
not included in the application package.

## Private analysis export

`tools/export_gap_observations.py` exports explicitly selected, completed search
tickets without modifying their source files. It requires Python's standard
library; it does not require a GPU, wallet access, or a network connection.

```text
python tools/export_gap_observations.py --genesis <64-character-source-genesis> --ticket <completed-ticket-folder> --output observations.zip
python tools/export_gap_observations.py --genesis <64-character-source-genesis> --verify observations.zip
```

Repeat `--ticket` for disjoint ranges. Duplicate or overlapping search intervals
are refused, as is audit work presented as another search population. Older
archives lacking exact observations are refused rather than reconstructed from
their Best rows. The output path must be new; prior backups are never replaced.

The ZIP contains the exact compressed observations, checked computed/sealed
reports, an existing finalization correction when present, and three CSV files:

- `observations.csv`: exact decimal endpoints, source flags, and derived merit;
- `gap-sizes.csv`: observed counts by integer gap size, with no upper merit cap;
- `merit-bands.csv`: derived counts in half-open bands of width 0.1.

**These tables describe raw local observations before scientific finalization.**
If a finalization delta removed or added pairs, retain that delta with the
observations and apply it before analysing the corrected result. For example,
111 raw pairs can describe 110 finalized pairs after two removed pairs are
replaced by one. The export does not disguise that difference. Local
finalization and independent network acceptance are separate states.

Merit is calculated at Decimal precision 50; its derived value and bin boundaries
are approximate. Exact integer endpoints remain authoritative for recalculation
at another precision. Import endpoint columns as text in spreadsheet software
to preserve integers beyond that program's numeric precision.

The manifest retains collection cutoffs, interval exposure and source hashes.
An explicit genesis argument is a source label; a ticket with a checked sealed
report additionally binds that label to its report. Neither the local checksums
nor `--verify` establish primality, completeness below the collection cutoff,
third-party authenticity, or blockchain acceptance. `--verify` checks the ZIP's
declared byte inventory and integrity.

Exports are private and can contain unrevealed audit information. Only fixed
scientific file names are copied; wallets, cookies, RPC credentials and arbitrary
work-folder files are excluded. Limits are 64 tickets, two million observations,
128 MiB compressed, 512 MiB expanded and 65,536 histogram keys. A refusal leaves
the source files intact and publishes no partial ZIP.
