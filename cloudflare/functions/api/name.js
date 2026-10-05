// POST /api/name {id, name}: take a name for this member, or 409 if another member has it.
import { claim, cleanId, cleanName, json } from '../../lib/club.js';

export async function onRequestPost({ request, env }) {
  let body;
  try {
    body = await request.json();
  } catch {
    return json({ error: 'bad request' }, 400);
  }
  const id = cleanId(body.id), name = cleanName(body.name);
  if (!id || !name) return json({ error: 'bad name' }, 400);
  if (!(await claim(env.CLUB, id, name))) return json({ error: 'taken' }, 409);
  const m = await env.CLUB.get(`m:${id}`, 'json');
  return json({ name, balance: m ? m.balance : 0, games: m ? m.games : 0 });
}
