// Optional documentation artwork renderer. Does not touch the app's assets.
// Install @resvg/resvg-js in a separate directory and set SUKUT_RESVG_MODULE
// to its absolute path if it is not available through Node's module search.
const fs = require('node:fs');
const path = require('node:path');
const { Resvg } = require(process.env.SUKUT_RESVG_MODULE || '@resvg/resvg-js');

const root = path.resolve(__dirname, '..');
const artwork = path.join(root, 'docs/artwork');
const font = path.join(root, 'Windows/src/sukut-ui/Assets/ReadexPro.ttf');
const svg = fs.readFileSync(path.join(artwork, 'hero.svg'));
const png = new Resvg(svg, {
  font: { fontFiles: [font], loadSystemFonts: false },
}).render().asPng();
fs.writeFileSync(path.join(artwork, 'hero.png'), png);
