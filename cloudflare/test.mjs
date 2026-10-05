// node --test cloudflare/test.mjs (after cloudflare/build.sh has built lib/core.wasm)
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { allowed, claim, cleanName, newToken, readToken, record, replay, top, TOKEN_TTL } from './lib/club.js';

const here = new URL('.', import.meta.url);
const core = new WebAssembly.Module(readFileSync(new URL('lib/core.wasm', here)));
// three games recorded from the original KING2.EXE (re/emu/record.py): the seed, the human's
// keyboard polls, and the original's final totals
const games = JSON.parse(readFileSync(new URL('../tests/fixtures/games.json', here)));

test('the core replays recorded games to the original totals', () => {
  for (const g of games) assert.deepEqual(replay(core, g.seed, g.polls), { status: 'ok', totals: g.totals });
});

test('a changed game does not replay', () => {
  const g = games[0];
  assert.equal(replay(core, g.seed, g.polls.slice(0, -1)).status, 'short');
  assert.equal(replay(core, g.seed, g.polls + '.').status, 'leftover');
  assert.equal(replay(core, g.seed, g.polls.replace('.', 'x')).status, 'bad-polls');
  const other = replay(core, g.seed + 1, g.polls);
  assert.ok(other.status !== 'ok' || other.totals.join() !== g.totals.join());
});

test('tokens are signed, expire, and carry the seed', async () => {
  const { seed, token } = await newToken('s3cret', 1000);
  assert.equal((await readToken('s3cret', token, 2000)).seed, seed);
  assert.equal(await readToken('other', token, 2000), null);
  assert.equal(await readToken('s3cret', token, 1000 + TOKEN_TTL + 1), null);
  const [s, i, n, sig] = token.split('.');
  assert.equal(await readToken('s3cret', [String((+s + 1) >>> 0), i, n, sig].join('.'), 2000), null);
});

test('names', () => {
  assert.equal(cleanName(' Саша '), 'Саша');
  assert.equal(cleanName('Ёжик-123'), 'Ёжик-123');
  assert.equal(cleanName(''), null);
  assert.equal(cleanName('x'.repeat(13)), null);
  assert.equal(cleanName('日本'), null);
});

function memoryKv(store) {
  return { get: async (k, t) => (store.has(k) ? (t === 'json' ? JSON.parse(store.get(k)) : store.get(k)) : null),
           put: async (k, v) => { store.set(k, v); }, delete: async (k) => { store.delete(k); } };
}

test('names are unique, case-insensitively, and renaming frees the old one', async () => {
  const kv = memoryKv(new Map());
  assert.equal(await claim(kv, 'aaaaaaaa', 'Саша'), true);
  assert.equal(await claim(kv, 'aaaaaaaa', 'саша'), true);
  assert.equal(await claim(kv, 'bbbbbbbb', 'САША'), false);
  await record(kv, 'aaaaaaaa', 'Саша', 0);
  assert.equal(await claim(kv, 'aaaaaaaa', 'Александр'), true);
  assert.equal(await claim(kv, 'bbbbbbbb', 'Саша'), true);
  assert.equal(await claim(kv, 'aaaaaaaa', 'Саша'), false);
});

test('balances accumulate and the top is sorted', async () => {
  const store = new Map();
  const kv = memoryKv(store);
  await record(kv, 'aaaaaaaa', 'Саша', -300);
  await record(kv, 'bbbbbbbb', 'Petya', 500);
  const r = await record(kv, 'aaaaaaaa', 'Саша', 1000);
  assert.equal(r.member.balance, 700);
  assert.equal(r.member.games, 2);
  const t = await top(kv);
  assert.equal(t.members, 2);
  assert.deepEqual(t.top.map((x) => x.name), ['Саша', 'Petya']);
});

test('rate limits count per kind, address and hour', async () => {
  const kv = memoryKv(new Map());
  for (let i = 0; i < 30; i++) assert.equal(await allowed(kv, '1.2.3.4', 0, 'nl', 30), true);
  assert.equal(await allowed(kv, '1.2.3.4', 0, 'nl', 30), false);
  assert.equal(await allowed(kv, '1.2.3.4', 0, 'rl', 30), true);
  assert.equal(await allowed(kv, '5.6.7.8', 0, 'nl', 30), true);
  assert.equal(await allowed(kv, '1.2.3.4', 3600000, 'nl', 30), true);
});
