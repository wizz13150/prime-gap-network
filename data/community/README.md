# Community range register

[community-reservations.csv](community-reservations.csv) extends the historical register. [registry.json](registry.json) defines exact coordinates, provenance and display rules.

The first 1,000 rows and original columns are preserved. [historical-original.csv](historical-original.csv) is an unchanged source snapshot, SHA-256 `65392d0c164412e30c430bd2d11c054ace349dc2c8ae6366f814e0a5fa71cc0a`. This development copy does not replace the original community register or its stewardship.

The 3,022 added round number ranges are declared `Open` at the operator's request. Their historical coverage is **not verified**; this is not a claim that nobody searched them. Each added CSV row covers one round range, with the same 1,000-zone / 100-block logical subdivision. The original source's `Complete` labels are community declarations, not independent proofs of exhaustive coverage.

CSV endpoints retain the original unit of `10^15`. Fractional coordinates are exact decimal strings, not floating-point numbers. For example, Benxy's manual interval is `79000000000000.1` to `79000000000002.1` in CSV units, or `[79000000000000100000000000000, 79000000000002100000000000000)` in whole-number coordinates.

Benxy's original request is preserved as a manual exception. Normal reservations use whole blocks or whole zones only. A block is shown as `Reserved` only when the entire block lies inside the manual interval. On the current `10^15` block grid this reserves one complete inner block; the two incompletely covered edge blocks remain `Open`. A zone must not be shown fully reserved or complete merely because it intersects this interval. No partial-reservation status is introduced, and this entry does not claim that mining has started or any work is complete.

All intervals are start-inclusive and end-exclusive. Import endpoints as text or exact integers/decimals: ordinary spreadsheet floating-point conversion can lose the small differences at these magnitudes. The Benxy interval lies outside the currently qualified navigation/mining range; recording it does not enable mining there automatically.

This publication is a versioned data snapshot. Authenticated live UI updates, simultaneous-action conflict handling and network-wide progress are still under development. Do not treat a local click as public confirmation without a committed, verified update.
