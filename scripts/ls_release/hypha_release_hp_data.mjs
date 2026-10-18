// HP source templates and its central integrity record are one publication contract.
import path from 'node:path';
import crypto from 'node:crypto';
import { Checkpoint } from './hypha_release_contract.mjs';

// The pages and HP tests read the central record; a release changes only that record and adds its notice.
export const HP_FILES = ['hypha.html', 'ja/hypha.html', 'updates/hypha.json', 'data/news.json'];
const RECORD = HP_FILES.indexOf('updates/hypha.json');
const NEWS = HP_FILES.indexOf('data/news.json');
export const HP_DOWNLOAD_BASE = 'https://github.com/heyalohaloha/kirin_hypha/releases';
export const HP_TARGETS = ['macos', 'windows'];
const TOKENS = {
  version: '{{KIRIN_HYPHA_LATEST_VERSION}}', date: '{{KIRIN_HYPHA_PUBLISHED_AT}}',
  notes: '{{KIRIN_HYPHA_RELEASE_NOTES_URL}}',
};

function exactKeys(value, keys) {
  return value && typeof value === 'object' && !Array.isArray(value)
    && Object.keys(value).sort().join(',') === [...keys].sort().join(',');
}
function validVersion(version) {
  return /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(version || '')
    && version.split('.').every(n => Number.isSafeInteger(Number(n)));
}
function validDate(date) {
  return /^\d{4}-\d{2}-\d{2}$/.test(date || '') && Number.isFinite(Date.parse(date))
    && new Date(date).toISOString().slice(0, 10) === date;
}
export function installerFilename(version, target) {
  if (!validVersion(version) || !HP_TARGETS.includes(target)) throw new Error('Invalid HP installer target/version');
  return `Kirin-Hypha-${version}-${target === 'macos' ? 'macOS-Universal.pkg' : 'Windows-x64-Setup.exe'}`;
}
export function validateHpRecord(record) {
  if (!exactKeys(record, ['schema', 'version', 'published_at', 'release_notes_url', 'downloads'])
      || record.schema !== 'kirin-hypha-release-v1' || !validVersion(record.version)
      || !validDate(record.published_at)
      || record.release_notes_url !== `${HP_DOWNLOAD_BASE}/tag/v${record.version}`
      || !exactKeys(record.downloads, HP_TARGETS)) throw new Error('Invalid HP release record');
  for (const target of HP_TARGETS) {
    const asset = record.downloads[target]; const name = installerFilename(record.version, target);
    const urls = [`${HP_DOWNLOAD_BASE}/download/v${record.version}/${name}`,
      `https://kirinmastering.com/downloads/releases/kirin-hypha/${record.version}/${name}`];
    if (!exactKeys(asset, ['url', 'bytes', 'sha256']) || !urls.includes(asset.url)
        || !Number.isSafeInteger(asset.bytes) || asset.bytes <= 0
        || !/^[a-f0-9]{64}$/.test(asset.sha256 || '')) throw new Error(`Invalid HP ${target} integrity record`);
  }
  return record;
}
export function makeHpRecord(version, date, facts) {
  if (!validVersion(version) || !validDate(date) || !Array.isArray(facts)) throw new Error('Invalid release version/date/package facts');
  const downloads = {};
  for (const target of HP_TARGETS) {
    const name = installerFilename(version, target);
    const rows = facts.filter(f => typeof f?.path === 'string' && path.basename(f.path) === name);
    if (rows.length !== 1) throw new Error(`Exactly one frozen HP ${target} installer fact required`);
    const { bytes, sha256 } = rows[0];
    downloads[target] = { url: `${HP_DOWNLOAD_BASE}/download/v${version}/${name}`, bytes, sha256 };
  }
  return validateHpRecord({ schema: 'kirin-hypha-release-v1', version, published_at: date,
    release_notes_url: `${HP_DOWNLOAD_BASE}/tag/v${version}`, downloads });
}
export function renderHpPage(html, record) {
  return html.replaceAll(TOKENS.version, record.version).replaceAll(TOKENS.date, record.published_at)
    .replaceAll(TOKENS.notes, record.release_notes_url);
}
export function assertHpPage(html, lang, record, { source = false } = {}) {
  const version = source ? TOKENS.version : record.version;
  const date = source ? TOKENS.date : record.published_at;
  const notes = source ? TOKENS.notes : record.release_notes_url;
  if (!html.includes(`<html lang="${lang}">`)
      || !new RegExp(`id="hypha-download-title"[^>]*>Hypha ${version.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}<`).test(html)
      || !html.includes(`<time datetime="${date}">${date}</time>`)
      || !html.includes(`href="${notes}"`)) throw new Error('HP language/version/date/notes markup mismatch');
  for (const target of HP_TARGETS) {
    if ([...html.matchAll(new RegExp(`href="/downloads/kirin-hypha/${target}"`, 'g'))].length !== 1) {
      throw new Error('HP must retain both central PKG + signed Windows EXE download routes');
    }
  }
  if (html.includes(`${HP_DOWNLOAD_BASE}/download/`)) throw new Error('HP direct installer links bypass its integrity record');
  if (!source && Object.values(TOKENS).some(token => html.includes(token))) throw new Error('HP unresolved release token');
}
// HP's publication day (Asia/Tokyo), the same rule its News build applies to confirmed notices.
export function hpPublicationDay(now = new Date()) {
  const parts = new Intl.DateTimeFormat('en', { timeZone: 'Asia/Tokyo', year: 'numeric', month: '2-digit', day: '2-digit' })
    .formatToParts(now);
  const part = type => parts.find(item => item.type === type)?.value;
  return `${part('year')}-${part('month')}-${part('day')}`;
}
// HP rejects a confirmed notice dated after its publication day; wait for the date, never publish early.
export function assertHpPublicationDate(date, now = new Date()) {
  if (!validDate(date) || date > hpPublicationDay(now)) {
    throw new Checkpoint('HP release date is after today in Tokyo; publish on or after that date');
  }
}
// The standard release notice, as in earlier releases; reviewed wording can follow in a later HP change.
export function hpReleaseNotice(record) {
  validateHpRecord(record);
  const { version, published_at: date } = record;
  return { id: `hypha-${version.replaceAll('.', '-')}-release-${date.replaceAll('-', '')}`, category: 'update',
    target: 'product', products: ['hypha'], date, status: 'confirmed', version,
    ja: { title: `Kirin Hypha ${version}を公開`, titlePhrases: ['Kirin Hypha ', `${version}を`, '公開'],
      summary: '配布ファイルと更新情報を製品ページで確認できます。', url: '/ja/hypha' },
    en: { title: `Kirin Hypha ${version} released`,
      summary: 'Find the download files and release information on the product page.', url: '/hypha' },
    evidence: { source: 'updates/hypha.json', published_at: date, url: record.release_notes_url } };
}
function updateNews(text, record, previous) {
  const news = JSON.parse(text);
  if (news?.schema !== 'kirin-news-v1' || !Array.isArray(news.items)) throw new Error('HP news manifest schema changed; review required');
  const hypha = item => Array.isArray(item?.products) && item.products.includes('hypha');
  if (!news.items.some(item => hypha(item) && item.version === previous.version)) {
    throw new Error('HP news no longer carries the current release notice; review required');
  }
  const notice = hpReleaseNotice(record);
  const same = news.items.filter(item => hypha(item) && (item.version === record.version || item.id === notice.id));
  if (same.length > 1 || (same.length === 1 && JSON.stringify(same[0]) !== JSON.stringify(notice))) {
    throw new Error('HP already has a different Hypha notice for this release; review required');
  }
  if (!same.length) news.items.unshift(notice);
  return `${JSON.stringify(news, null, 2)}\n`;
}
export function updateHpDocuments(documents, version, date, facts) {
  if (!Array.isArray(documents) || documents.length !== HP_FILES.length) throw new Error('HP central release source set required');
  const previous = validateHpRecord(JSON.parse(documents[RECORD]));
  const record = makeHpRecord(version, date, facts);
  const priorParts = previous.version.split('.').map(Number); const nextParts = version.split('.').map(Number);
  const changedPart = priorParts.findIndex((n, i) => n !== nextParts[i]);
  if (changedPart >= 0 && priorParts[changedPart] > nextParts[changedPart]) throw new Error('HP release downgrade requires review');
  // HP lists notices newest date first (ties by id ascending), so a new release must be dated later.
  if (previous.version !== version && date <= previous.published_at) {
    throw new Error('HP release date must be later than the current release notice');
  }
  if (previous.version === version && (previous.published_at !== date
      || HP_TARGETS.some(target => ['url', 'bytes', 'sha256']
        .some(key => previous.downloads[target][key] !== record.downloads[target][key])))) {
    throw new Error('HP existing same-version release data differs from frozen facts');
  }
  for (const [i, lang] of ['en', 'ja'].entries()) {
    assertHpPage(documents[i], lang, previous, { source: true });
    if (documents[i].includes(previous.version) || documents[i].includes(version)) {
      throw new Error('HP template carries a literal release version; review required');
    }
    const rendered = renderHpPage(documents[i], record); assertHpPage(rendered, lang, record);
    if (!rendered.includes('Audio Unit + AAX') || !rendered.includes('Windows 10/11 (VST3 + AAX)')) {
      throw new Error('HP format copy changed; review required');
    }
  }
  const result = [...documents];
  result[RECORD] = `${JSON.stringify(record, null, 2)}\n`;
  result[NEWS] = updateNews(documents[NEWS], record, previous);
  return result;
}
export async function verifyHpAsset(fetcher, url, fact) {
  const response = await fetcher(url, { redirect: 'follow', signal: AbortSignal.timeout(120000) });
  if (!response.ok || !response.body) throw new Error('Public artifact download failed');
  const sha = crypto.createHash('sha256'); let bytes = 0;
  for await (const chunk of response.body) {
    sha.update(chunk); bytes += chunk.length;
    if (bytes > fact.bytes) throw new Error('Public artifact exceeded frozen size');
  }
  if (bytes !== fact.bytes || sha.digest('hex') !== fact.sha256) throw new Error('Public artifact bytes/hash mismatch');
  return { url, bytes, sha256: fact.sha256 };
}
