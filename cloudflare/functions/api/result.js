// POST /api/result {token, id, name, polls}: replays the game with the core and books the
// human's total to the member's balance.
import coreModule from '../../lib/core.wasm';
import { POLL_MS, claim, cleanId, cleanName, json, publicTop, readToken, record, replay, top } from '../../lib/club.js';

export async function onRequestPost({ request, env }) {
  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: 'bad request' }, 400);
  }
  const t = await readToken(env.KING_SECRET, body.token);
  if (!t) return json({ error: 'unknown or expired game' }, 403);
  const id = cleanId(body.id), name = cleanName(body.name), polls = String(body.polls || '');
  if (!id || !name) return json({ error: 'bad member' }, 400);
  if (!(await claim(env.CLUB, id, name))) return json({ error: 'taken' }, 409);
  if (await env.CLUB.get(`used:${t.nonce}`)) return json({ error: 'this game was already booked' }, 409);
  // the game paces its polls 30 ms apart: a game cannot have more polls than its time allows
  if (polls.length * POLL_MS * 0.9 > Date.now() - t.issued) return json({ error: 'too fast' }, 422);
  const r = replay(coreModule, t.seed, polls);
  if (r.status !== 'ok') return json({ error: `the game does not replay (${r.status})` }, 422);
  await env.CLUB.put(`used:${t.nonce}`, '1', { expirationTtl: 2 * 24 * 3600 });
  const total = r.totals[3];
  const { member, isNew, members } = await record(env.CLUB, id, name, total);
  return json({ total, balance: member.balance, games: member.games, isNew, members,
    top: publicTop((await top(env.CLUB)).top) });
}
