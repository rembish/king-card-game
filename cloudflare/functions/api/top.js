// GET /api/top: the club's richest members.
import { json, publicTop, top } from '../../lib/club.js';

export async function onRequestGet({ env }) {
  const t = await top(env.CLUB);
  return json({ members: t.members, top: publicTop(t.top) });
}
