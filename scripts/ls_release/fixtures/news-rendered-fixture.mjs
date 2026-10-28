// News markup snapshots were rendered from the HP news renderer; selections follow its public top-three rule.
import fs from 'node:fs';
const escape = value => String(value).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const names = { os: 'Kirin OS', hypha: 'Kirin Hypha', sense: 'Kirin Sense' };
export function renderedNews(items, lang) {
  const html = fs.readFileSync(new URL(`./news-rendered-${lang}.html`, import.meta.url), 'utf8');
  const template = html.match(/<li class="news__item"[\s\S]*?<\/li>/)[0];
  const selected = items.filter(item => item.status === 'confirmed')
    .sort((a, b) => b.date.localeCompare(a.date) || (a.id < b.id ? -1 : a.id > b.id ? 1 : 0)).slice(0, 3);
  const entries = selected.map(item => {
    const copy = item[lang];
    const title = lang === 'ja' ? (copy.titlePhrases || [copy.title]).map(phrase => {
      const leading = phrase.match(/^\s*/u)[0], trailing = phrase.match(/\s*$/u)[0];
      return `${escape(leading)}<span class="r1-phrase-unit">${escape(phrase.slice(leading.length, phrase.length - trailing.length))}</span>${escape(trailing)}`;
    }).join('<wbr>') : escape(copy.title);
    return template.replace(/data-news-id="[^"]+"/, `data-news-id="${escape(item.id)}"`)
      .replace(/<time datetime="[^"]+">[^<]*<\/time>/, `<time datetime="${escape(item.date)}">${escape(item.date.replaceAll('-', '.'))}</time>`)
      .replace(/<span class="news__products">[^<]*<\/span>/, `<span class="news__products">${escape(item.products.map(product => names[product]).join(' / '))}</span>`)
      .replace(/<a href="[^"]+">[\s\S]*?<\/a>/, `<a href="${escape(copy.url)}">${title}</a>`)
      .replace(/<p class="news__summary">[\s\S]*?<\/p>/, `<p class="news__summary">${escape(copy.summary)}</p>`);
  });
  return html.replace(template, entries.join('\n'));
}
