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
// the link their button has given so far (the same on every visit): tried if asking fails
const KNOWN = ['d1', 'd4'].map((h) => `https://${h}.xp.myabandonware.com/t/cddaa63e-2f34-4502-8494-bcb014b53dac/King_DOS_RU.zip`);

// the link their download button gets: a session cookie from the game's page, then the button's
// request
async function currentUrl() {
  const page = await fetch(SITE + GAME, { headers: { 'user-agent': UA } });
  const cookie = (page.headers.get('set-cookie') || '').split(';')[0];
  const r = await fetch(SITE + DOWNLOAD, {
    redirect: 'manual',
    headers: { 'user-agent': UA, cookie, x_requested_with: 'XMLHttpRequest', referer: SITE + GAME },
  });
  const text = await r.text();
  let j = {};
  try {
    j = JSON.parse(text);
  } catch {
    throw new Error(`page ${page.status}${cookie ? ' with cookie' : ''}, link ${r.status}`);
  }
  if (!FILE.test(j.url || '')) throw new Error('unexpected link');
  return j.url;
}

async function zip(url) {
  const r = await fetch(url, { headers: { 'user-agent': UA } });
  const size = Number(r.headers.get('content-length')) || 0;
  if (!r.ok || size > MAX) throw new Error(`${r.status}`);
  const body = await r.arrayBuffer();
  if (body.byteLength > MAX || body.byteLength < 22) throw new Error('not the zip');
  return body;
}

export async function onRequestGet({ request, env }) {
  const ip = request.headers.get('cf-connecting-ip') || 'unknown';
  if (!(await allowed(env.CLUB, ip, Date.now(), 'dl', 20))) return json({ error: 'too many downloads, try again later' }, 429);
  const tried = [];
  let urls = KNOWN;
  try {
    urls = [await currentUrl(), ...KNOWN];
  } catch (e) {
    tried.push(`link: ${e.message}`);
  }
  let body, url;
  for (url of urls) {
    try {
      body = await zip(url);
      break;
    } catch (e) {
      tried.push(`${new URL(url).host}: ${e.message}`);
    }
  }
  if (!body) return json({ error: `MyAbandonware did not give the file (${tried.join('; ')})` }, 502);
  return new Response(body, {
    headers: { 'content-type': 'application/zip', 'cache-control': 'no-store',
      'x-source': url.replace(/\/t\/[^/]+\//, '/t/…/') },
  });
}
