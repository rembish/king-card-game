// The global KING club: games are dealt with a server seed and checked by replaying them with
// the game's own core (core.wasm, the same C as the game), so a score cannot be made up.
// The repository is public: nothing here relies on being secret except KING_SECRET.
export const TOKEN_TTL = 24 * 3600 * 1000; // a game must be submitted within a day
export const POLL_MS = 30;                 // the game polls the keyboard every 30 ms
export const TOP_KEEP = 100;
const TOKENS_PER_HOUR = 60;

const enc = new TextEncoder();
const b64url = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf)))
  .replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');

async function hmac(secret, text) {
  const key = await crypto.subtle.importKey('raw', enc.encode(secret), { name: 'HMAC', hash: 'SHA-256' },
    false, ['sign']);
  return b64url(await crypto.subtle.sign('HMAC', key, enc.encode(text)));
}

export function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' },
  });
}

// seed.issued.nonce.signature
export async function newToken(secret, now = Date.now()) {
  const r = crypto.getRandomValues(new Uint32Array(3));
  const payload = `${r[0]}.${now}.${r[1].toString(36)}${r[2].toString(36)}`;
  return { seed: r[0], token: `${payload}.${await hmac(secret, payload)}` };
}

export async function readToken(secret, token, now = Date.now()) {
  const parts = String(token || '').split('.');
  if (parts.length !== 4) return null;
  const payload = parts.slice(0, 3).join('.');
  if (await hmac(secret, payload) !== parts[3]) return null;
  const seed = Number(parts[0]), issued = Number(parts[1]);
  if (!Number.isInteger(seed) || seed < 0 || seed > 0xffffffff || !Number.isFinite(issued)) return null;
  if (now - issued > TOKEN_TTL || issued > now + 60000) return null;
  return { seed, issued, nonce: parts[2] };
}

// names as the game accepts them: up to 12 Latin or Cyrillic letters, digits, punctuation
export function cleanName(name) {
  const s = String(name || '').normalize('NFC').trim();
  const chars = [...s];
  if (!chars.length || chars.length > 12) return null;
  return chars.every((c) => /[\x20-\x7e]/.test(c) || /[А-яЁё]/.test(c)) ? s : null;
}

export const cleanId = (id) => (/^[a-z0-9-]{8,64}$/.test(String(id || '')) ? id : null);

// coreModule: core.wasm as a WebAssembly.Module (the Pages bundler imports it, tests read it)
const instances = new WeakMap();
export function replay(coreModule, seed, polls) {
  if (!/^[._KMHP?a-n]*$/.test(polls)) return { status: 'bad-polls' };
  let instance = instances.get(coreModule);
  if (!instance) {
    instance = new WebAssembly.Instance(coreModule, {});
    instance.exports._initialize?.();
    instances.set(coreModule, instance);
  }
  const x = instance.exports;
  if (polls.length > x.capacity()) return { status: 'too-long' };
  new Uint8Array(x.memory.buffer, x.buffer(), polls.length).set(enc.encode(polls));
  const r = x.verify(seed >>> 0, polls.length);
  if (r !== 0) return { status: ['ok', 'short', 'bad-polls', 'leftover'][r] || 'error' };
  return { status: 'ok', totals: [1, 2, 3, 4].map((p) => x.total(p)) };
}

// at most TOKENS_PER_HOUR games started per address and hour
export async function allowed(kv, ip, now = Date.now()) {
  const key = `rl:${ip}:${Math.floor(now / 3600000)}`;
  const n = Number(await kv.get(key)) || 0;
  if (n >= TOKENS_PER_HOUR) return false;
  await kv.put(key, String(n + 1), { expirationTtl: 7200 });
  return true;
}

export async function top(kv) {
  return { members: Number(await kv.get('members')) || 0, top: (await kv.get('top', 'json')) || [] };
}

// Adds a checked game to the member's balance and the leaderboard.
export async function record(kv, id, name, total, now = Date.now()) {
  const old = await kv.get(`m:${id}`, 'json');
  const m = old || { balance: 0, games: 0, since: now };
  m.name = name;
  m.balance += total;
  m.games += 1;
  m.last = now;
  await kv.put(`m:${id}`, JSON.stringify(m));
  let members = Number(await kv.get('members')) || 0;
  if (!old) await kv.put('members', String(++members));
  const list = ((await kv.get('top', 'json')) || []).filter((r) => r.id !== id);
  list.push({ id, name, balance: m.balance, games: m.games });
  list.sort((a, b) => b.balance - a.balance);
  await kv.put('top', JSON.stringify(list.slice(0, TOP_KEEP)));
  return { member: m, isNew: !old, members };
}

export const publicTop = (list) => list.slice(0, 9).map(({ name, balance, games }) => ({ name, balance, games }));

// Names are unique in the club, compared as the original did (upper case). A name belongs to
// the first member (browser) that took it; renaming frees the old one.
export const foldName = (name) => name.toLocaleUpperCase('ru-RU');

export async function claim(kv, id, name) {
  const key = `n:${foldName(name)}`;
  const owner = await kv.get(key);
  if (owner && owner !== id) return false;
  if (!owner) {
    const m = await kv.get(`m:${id}`, 'json');
    if (m && m.name && foldName(m.name) !== foldName(name)) await kv.delete(`n:${foldName(m.name)}`);
    await kv.put(key, id);
    if (m) {
      m.name = name;
      await kv.put(`m:${id}`, JSON.stringify(m));
    }
  }
  return true;
}
