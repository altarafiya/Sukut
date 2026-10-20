const fs = require('node:fs');
const path = require('node:path');
const {Resvg} = require('../deps/icon-tools/node_modules/@resvg/resvg-js');
const assets = path.resolve(__dirname, '../src/sukut-ui/Assets');
for (const name of ['Sukut','Tray']) {
  const svg=fs.readFileSync(path.join(assets, name+'.svg'));
  fs.writeFileSync(path.join(assets,name+'.png'),new Resvg(svg,{fitTo:{mode:'width',value:256}}).render().asPng());
}
