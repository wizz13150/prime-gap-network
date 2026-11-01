import assert from 'node:assert/strict';
import fs from 'node:fs';
import { exactScientific, csvCoordinate, fromCsvCoordinate, manualCellStatus, wholeInnerBlockRange } from '../tools/community-registry.mjs';
const registry = JSON.parse(fs.readFileSync(new URL('../data/community/registry.json', import.meta.url), 'utf8'));
const r = registry.manual_reservations[0], a = BigInt(r.start), b = BigInt(r.end), u = 10n ** 15n;
assert.equal(exactScientific(r.source_start), a);
assert.equal(exactScientific(r.source_end), b);
assert.equal(b - a, 2n * u);
assert.equal(a % u, u / 10n);
assert.equal(wholeInnerBlockRange(r).count, 1n);
for (let i = -3n; i < 7n; ++i) {
  const start = a / u * u + i * u, end = start + u;
  assert.equal(manualCellStatus(r, start, end), i === 1n ? 'Reserved' : 'Open');
}
assert.equal(manualCellStatus(r, a / (100n * u) * (100n * u), (a / (100n * u) + 1n) * (100n * u)), 'Open');
for (const value of [0n, a - 1n, a, b, b + 1n, 10n ** 38n + 1n]) assert.equal(fromCsvCoordinate(csvCoordinate(value)), value);
assert.throws(() => exactScientific('1.1e0'));
assert.throws(() => exactScientific('Infinity'));
assert.throws(() => fromCsvCoordinate('7.9e13'));
assert.throws(() => manualCellStatus(r, a, a));
const aligned = { start: '100', end: '300', status: 'Reserved' };
assert.equal(wholeInnerBlockRange(aligned, 100n).count, 2n);
assert.equal(manualCellStatus(aligned, 100n, 200n), 'Reserved');
assert.equal(manualCellStatus(aligned, 200n, 300n), 'Reserved');
assert.equal(manualCellStatus(aligned, 50n, 150n), 'Open');
assert.equal(manualCellStatus(aligned, 300n, 400n), 'Open');
assert.equal(wholeInnerBlockRange({ start: '101', end: '199' }, 100n).count, 0n);
assert.throws(() => wholeInnerBlockRange({ start: '100', end: '100' }, 100n));
console.log('PASS: exact Benxy interval, whole-block-only reservation, Open edges, whole-zone protection, scaled CSV round-trip');
