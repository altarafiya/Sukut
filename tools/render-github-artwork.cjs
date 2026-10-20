// Optional documentation artwork renderer. Does not touch the app's assets.
// npm install --no-save @resvg/resvg-js in a separate tool directory, then set
// SUKUT_RESVG_MODULE to that module's absolute path if it is not on NODE_PATH.
const fs = require('node:fs');
const path = require('node:path');
const { Resvg } = require(process.env.SUKUT_RESVG_MODULE || '@resvg/resvg-js');
const root = path.resolve(__dirname, '..');
const logo = fs.readFileSync(path.join(root, 'docs/artwork/logo.png')).toString('base64');
const bars = [16, 30, 51, 77, 114, 162, 114, 77, 51, 30, 16].map((height, i) =>
  `<rect x="${1008 + i * 17}" y="${290 - height / 2}" width="7" height="${height}" rx="3.5"/>`).join('');
const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="580" viewBox="0 0 1400 580">
<defs><linearGradient id="bg" x2="1" y2="1"><stop stop-color="#fcf9f3"/><stop offset="1" stop-color="#efe3d0"/></linearGradient></defs>
<rect width="1400" height="580" rx="30" fill="url(#bg)"/>
<circle cx="1205" cy="65" r="204" fill="#e8d7ba" opacity=".28"/>
<circle cx="1310" cy="570" r="250" fill="#d8bc94" opacity=".18"/>
<image href="data:image/png;base64,${logo}" x="72" y="92" width="168" height="168"/>
<g font-family="Readex Pro, sans-serif"><text x="285" y="180" font-size="78" font-weight="600" fill="#332a22">Sukut</text>
<text x="78" y="346" font-size="44" fill="#43362b">Enjoy the internet.</text>
<text x="78" y="405" font-size="44" fill="#9c7446">Without the music.</text>
<g font-size="20" fill="#65584a"><text x="78" y="500">Windows + Android</text><text x="330" y="500" fill="#918371">Linux coming soon</text></g></g>
<rect x="838" y="122" width="442" height="337" rx="40" fill="#fffdf8" stroke="#ddc9ac" stroke-width="2"/>
<g fill="#c5a16c">${bars}</g>
<circle cx="932" cy="290" r="54" fill="#4b3a2c"/>
<g transform="translate(897 255) scale(2.9)" fill="#f7f0e4"><path d="M4.8 7.86C2.9 8.4 1.8 10.11 1.6 11.91C1 14.88 3.5 16.95 7.4 16.95C10.6 16.95 12.1 15.51 12.9 13.35C14.2 14.34 16.1 14.34 17.6 13.08C19.3 14.7 21.7 14.43 22.3 12.63C22.9 11.28 22.9 9.12 22.5 7.68C22.3 6.87 21.7 6.6 21.1 6.78C20.3 6.96 20.3 7.59 20.4 8.4L20.4 9.93C20.4 10.83 20.1 11.46 19.3 11.46C18.2 11.46 17.9 10.65 17.9 9.66L17.9 8.76C17.9 7.95 17.4 7.68 16.8 7.68C16.1 7.68 15.8 8.13 15.8 8.85L15.8 9.75C15.8 10.83 15.2 11.46 14.4 11.46C13.5 11.46 13.1 10.74 13.1 9.75L13.1 8.94C13.1 8.13 12.7 7.77 12 7.77C11.3 7.77 10.9 8.22 10.9 9.03L10.9 10.83C10.9 13.17 8.8 14.52 6.4 14.52C4 14.52 2.9 13.62 3 12.09C3.1 10.56 3.8 9.03 4.8 7.86Z"/></g>
<g stroke="#ba945b" stroke-width="3" stroke-linecap="round"><path d="M790 126v20m-10-10h20M1295 475v14m-7-7h14"/></g>
<circle cx="807" cy="455" r="4" fill="#b79a71"/>
</svg>`;
const art = path.join(root, 'docs/artwork');
fs.writeFileSync(path.join(art, 'hero.svg'), svg);
const fonts = path.join(root, 'Windows/src/sukut-ui/Assets');
fs.writeFileSync(path.join(art, 'hero.png'), new Resvg(svg, {
  font: {fontFiles: [path.join(fonts, 'ReadexPro.ttf')], loadSystemFonts: true},
}).render().asPng());
