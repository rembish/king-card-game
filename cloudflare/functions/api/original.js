// GET /api/original: the game's zip from MyAbandonware, passed through for the page to unzip
// in the browser (their server does not let other sites fetch it). Fetched only when a player
// asks for it; nothing is kept here.
import { allowed, json } from '../../lib/club.js';

const SITE = 'https://www.myabandonware.com';
const GAME = '/game/king-c1a';
const DOWNLOAD = '/download/m0mz-king';
const FILE = /^https:\/\/d\d+\.xp\.myabandonware\.com\/t\/[0-9a-f-]{36}\/King_DOS_RU\.zip$/;
const UA = 'Mozilla/5.0 (compatible; king.rembi.sh; +https://github.com/rembish/king-card-game)';
const MAX = 1 << 20;

// the link their download button gets: a session cookie from the game's page, then the button's
// request
async function currentUrl() {
  const page = await fetch(SITE + GAME, { headers: { 'user-agent': UA } });
  const cookie = (page.headers.get('set-cookie') || '').split(';')[0];
  const r = await fetch(SITE + DOWNLOAD, {
    redirect: 'manual',
    headers: { 'user-agent': UA, cookie, x_requested_with: 'XMLHttpRequest', referer: SITE + GAME },
  });
  const j = await r.json();
  if (!FILE.test(j.url || '')) throw new Error('unexpected link');
  return j.url;
}

export async function onRequestGet({ request, env }) {
  const ip = request.headers.get('cf-connecting-ip') || 'unknown';
  if (!(await allowed(env.CLUB, ip, Date.now(), 'dl', 20))) return json({ error: 'too many downloads, try again later' }, 429);
  let url;
  try {
    url = await currentUrl();
  } catch (e) {
    return json({ error: `MyAbandonware did not give the link (${e.message})` }, 502);
  }
  const r = await fetch(url, { headers: { 'user-agent': UA } });
  const size = Number(r.headers.get('content-length')) || 0;
  if (!r.ok || size > MAX) return json({ error: `MyAbandonware answered ${r.status}` }, 502);
  const body = await r.arrayBuffer();
  if (body.byteLength > MAX) return json({ error: 'the file is too big' }, 502);
  return new Response(body, {
    headers: { 'content-type': 'application/zip', 'cache-control': 'no-store',
      'x-source': url.replace(/\/t\/[^/]+\//, '/t/…/') },
  });
}
