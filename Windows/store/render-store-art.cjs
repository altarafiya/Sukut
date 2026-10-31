// Deterministic Store artwork: real, unretouched interface captures + native HTML/SVG.
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('C:/Users/osama/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const kit = process.argv[2];
if (!kit) throw new Error('Pass the Store kit directory');
const project = path.resolve(kit, '..', '..');
const fonts = path.join(project, 'Windows/src/sukut-ui/Assets');
const data = (file, type='image/png') => `data:${type};base64,${fs.readFileSync(file).toString('base64')}`;
const logo = data(path.join(fonts,'Sukut.png'));
const font = (file) => data(path.join(fonts,file),'font/ttf');
const css = `
@font-face{font-family:Readex;src:url('${font('ReadexPro.ttf')}');font-weight:100 900}
@font-face{font-family:IBM;src:url('${font('IBMPlexSansArabic-Regular.ttf')}');font-weight:400}
@font-face{font-family:IBM;src:url('${font('IBMPlexSansArabic-SemiBold.ttf')}');font-weight:600 900}
*{box-sizing:border-box}html,body{margin:0;width:1920px;height:1080px;overflow:hidden}
body{font-family:Readex,IBM,sans-serif;color:#30261d;background:#faf6ed}
body.ar{font-family:IBM,Readex,sans-serif;direction:rtl}
.canvas{position:relative;width:1920px;height:1080px;padding:150px 100px 100px;isolation:isolate}
.wash{position:absolute;inset:0;z-index:-2;background:radial-gradient(ellipse at 88% 54%,#ddc39854 0,transparent 54%)}
.ar .wash{background:radial-gradient(ellipse at 12% 54%,#ddc39854 0,transparent 54%)}
header{position:absolute;top:42px;inset-inline:100px;display:flex;align-items:center;justify-content:space-between}
.brand{display:flex;align-items:center;gap:16px;font-size:30px;font-weight:600}.brand img{width:56px;height:56px}
.topline{color:#88765e;font-size:20px;letter-spacing:.02em}.ar .topline{letter-spacing:0;font-size:23px}
.main{height:100%;display:grid;grid-template-columns:700px 1fr;gap:80px;align-items:center}
.copy{align-self:center;padding-bottom:50px}.eyebrow{font-size:20px;color:#926e34;letter-spacing:.1em;font-weight:600;margin-bottom:26px}.ar .eyebrow{font-size:26px;letter-spacing:0}
h1{font-size:76px;font-weight:650;letter-spacing:-3.5px;line-height:1.19;margin:0 0 36px;white-space:pre-line}.ar h1{font-size:86px;letter-spacing:0;line-height:1.48;margin-bottom:22px}
.body{font-size:27px;line-height:1.7;max-width:650px;white-space:pre-line;color:#76644f}.ar .body{font-size:31px;line-height:1.85}
.tags{display:flex;flex-wrap:wrap;gap:14px;margin-top:36px}.tag{padding:12px 20px;border:1px solid #dacbb4;border-radius:30px;font-size:19px;color:#6e5839;background:#fffaf2a6}.ar .tag{font-size:23px}
.shot-wrap{display:flex;align-items:center;justify-content:center;position:relative}
.shot{width:870px;height:auto;border-radius:13px;box-shadow:0 28px 65px #3b2c252f,0 0 0 1px #3d312331;display:block}
footer{position:absolute;bottom:34px;inset-inline:100px;display:flex;justify-content:space-between;color:#9a886f;font-size:17px}.ar footer{font-size:21px}
.mode-list{display:grid;gap:20px;margin-top:40px}.mode{display:grid;grid-template-columns:14px 1fr;gap:18px;align-items:start}.mode i{width:10px;height:54px;border-radius:10px;background:var(--c)}.mode strong{font-size:26px;display:block;margin-bottom:7px}.mode p{font-size:20px;line-height:1.7;color:#796a56;margin:0}.ar .mode strong{font-size:31px}.ar .mode p{font-size:25px}
.modes h1{font-size:67px}.ar .modes h1{font-size:77px}
.modes .copy{padding-bottom:0}.mode-visual{position:relative;height:610px;display:flex;align-items:center;justify-content:center}
.wave{position:absolute;top:0;inset-inline:90px;height:210px;display:flex;gap:14px;align-items:center;justify-content:center;opacity:.78}
.wave span{width:13px;height:var(--h);background:#b58a4c;border-radius:15px}.wave:after{content:'';position:absolute;inset-inline:30px;top:104px;height:1px;background:#d5bea055}
.detail-crop{width:940px;height:365px;overflow:hidden;border-radius:28px;box-shadow:0 25px 55px #3b2c2529;position:relative;background:#333;margin-top:150px;direction:ltr}
.detail-crop img{width:1084px;position:absolute;left:-103px;top:-176px}.ar .detail-crop img{left:-38px;top:-189px}
.local .main{grid-template-columns:1fr;align-items:stretch}.local .copy{text-align:center;align-self:start;padding:0}.local h1{font-size:77px;line-height:1.3;margin-bottom:14px}.ar .local h1{font-size:90px}.local .body{max-width:none;margin:0 auto;font-size:25px}.ar .local .body{font-size:30px}
.flow{display:flex;align-items:center;justify-content:center;gap:55px;margin:68px auto 46px;direction:ltr}
.flow-card{width:320px;height:230px;border-radius:36px;border:1px solid #dccab0;background:#fffaf3;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:24px;box-shadow:0 16px 40px #68513a0b}
.flow-card.center{background:#f0dfc1;border-color:#c9a875}.flow-card svg{width:70px;height:70px;stroke:#8c6a38;fill:none;stroke-width:1.5;stroke-linecap:round;stroke-linejoin:round}.flow-card img{width:88px;height:88px}.flow-card span{font-size:25px}.ar .flow-card span{font-size:29px;direction:rtl}.arrow{color:#b49361;font-size:42px}
.output-crop{margin:0 auto;width:1265px;height:164px;border-radius:22px;overflow:hidden;position:relative;direction:ltr;box-shadow:0 16px 34px #3b2c251a}
.output-crop img{position:absolute;width:1430px;left:-134px;top:-737px}.ar .output-crop img{left:-49px;top:-801px}
.ar .flow{direction:rtl}.ar .arrow{transform:rotate(180deg)}
`;
const entries = {
 en: [
  {id:'01-home',eyebrow:'SUKUT FOR WINDOWS',title:'Enjoy the internet.\nWithout the music.',body:'Choose your audio mode.\nFiltering happens on your PC.',tags:['English + العربية','On-device audio'],image:'home'},
  {id:'02-modes',kind:'modes',eyebrow:'YOUR SOUND, YOUR CHOICE',title:'Choose how\nyou listen.',modes:[['Music Filter','Remove music. Keep speech and background.','#5d8e72'],['Optimized','Mute music. Adjust background sound.','#b18a4b'],['Strict','Focus on speech. Background effects may be removed.','#b77c7e']],image:'home'},
  {id:'03-settings',eyebrow:'MAKE IT YOURS',title:'A familiar interface.\nYour preferences.',body:'English or Arabic. Light or dark.\nSimple controls, all in one place.',tags:['Language & themes','Audio controls'],image:'settings'},
  {id:'04-phone',eyebrow:'OPTIONAL ANDROID COMPANION',title:'Your phone.\nA simpler setup.',body:'Install, update, or follow the USB guide.\nYou stay in control of permissions.',tags:['Guided USB setup','Shizuku assistance'],image:'phone'},
  {id:'05-local',kind:'local',eyebrow:'LOCAL AUDIO PROCESSING',title:'Your audio stays on your device.',body:'No audio upload. No account needed for filtering.',flow:['Your apps','Sukut','Your output'],image:'home'}
 ],
 ar: [
  {id:'01-home',eyebrow:'سُكوت لويندوز',title:'إنترنت\nبلا موسيقى.',body:'اختر وضع الصوت المناسب.\nالمعالجة على حاسوبك.',tags:['العربية والإنجليزية','معالجة محلية'],image:'home'},
  {id:'02-modes',kind:'modes',eyebrow:'صوت يناسبك',title:'اختر ما\nتريد سماعه.',modes:[['مرشح الموسيقى','أزل الموسيقى وأبقِ الكلام وأصوات الخلفية.','#5d8e72'],['الوضع المحسّن','اكتم الموسيقى واضبط أصوات الخلفية.','#b18a4b'],['الوضع الصارم','ركز على الكلام. قد تختفي المؤثرات أيضًا.','#b77c7e']],image:'home'},
  {id:'03-settings',eyebrow:'على ذوقك',title:'واجهة مألوفة.\nوخياراتك أنت.',body:'عربية أو إنجليزية. فاتحة أو داكنة.\nتحكم بسيط في مكان واحد.',tags:['اللغة والمظهر','التحكم بالصوت'],image:'settings'},
  {id:'04-phone',eyebrow:'نسخة أندرويد اختيارية',title:'هاتفك.\nبإعداد أسهل.',body:'ثبّت أو حدّث أو اتبع دليل USB.\nالأذونات تبقى تحت تحكمك.',tags:['دليل USB مبسّط','المساعدة في إعداد Shizuku'],image:'phone'},
  {id:'05-local',kind:'local',eyebrow:'المعالجة على جهازك',title:'صوتك يبقى على جهازك.',body:'لا يُرسل صوتك إلى الإنترنت. لا تحتاج إلى حساب.',flow:['تطبيقاتك','سُكوت','سماعاتك'],image:'home'}
 ]
};
function esc(s){return s.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;')}
function imageFor(lang,id){return data(path.join(kit,'screenshots',`raw-${lang}-${id}.png`))}
function documentFor(lang,d){
 const image=imageFor(lang,d.image), title=esc(d.title), ar=lang==='ar';
 let visual=`<div class="shot-wrap"><img class="shot" src="${image}"></div>`;
 if(d.kind==='modes') visual=`<div class="mode-visual"><div class="wave">${[28,52,84,122,168,202,168,122,84,52,28].map(h=>`<span style="--h:${h}px"></span>`).join('')}</div><div class="detail-crop"><img src="${image}"></div></div>`;
 let copy=`<div class="copy"><div class="eyebrow">${esc(d.eyebrow)}</div><h1>${title}</h1>${d.body?`<div class="body">${esc(d.body)}</div>`:''}${d.tags?`<div class="tags">${d.tags.map(t=>`<span class="tag">${esc(t)}</span>`).join('')}</div>`:''}${d.modes?`<div class="mode-list">${d.modes.map(m=>`<div class="mode" style="--c:${m[2]}"><i></i><div><strong>${esc(m[0])}</strong><p>${esc(m[1])}</p></div></div>`).join('')}</div>`:''}</div>`;
 if(d.kind==='local') visual=`<div><div class="flow"><div class="flow-card"><svg viewBox="0 0 64 64"><rect x="7" y="8" width="50" height="38" rx="6"/><path d="M21 56h22M32 46v10M24 19v16l15-8z"/></svg><span>${esc(d.flow[0])}</span></div><span class="arrow">→</span><div class="flow-card center"><img src="${logo}"><span>${esc(d.flow[1])}</span></div><span class="arrow">→</span><div class="flow-card"><svg viewBox="0 0 64 64"><path d="M10 36v-9a22 22 0 0 1 44 0v9"/><rect x="7" y="29" width="12" height="23" rx="5"/><rect x="45" y="29" width="12" height="23" rx="5"/></svg><span>${esc(d.flow[2])}</span></div></div><div class="output-crop"><img src="${image}"></div></div>`;
 return `<!doctype html><html lang="${lang}"><meta charset="utf-8"><style>${css}</style><body class="${lang}"><div class="canvas ${d.kind||''}"><div class="wash"></div><header><div class="brand"><img src="${logo}"><span>${ar?'سُكوت':'Sukut'}</span></div><div class="topline">${ar?'إنترنت بلا موسيقى':'Enjoy the internet. Without the music.'}</div></header><div class="main">${copy}${visual}</div><footer><span>${ar?'يتطلب برنامج التشغيل المنفصل VB-CABLE.':'Requires the separate VB-CABLE driver.'}</span><span>${ar?'تطوير: الطرفية':'Developed by Altarafiyah'}</span></footer></div></body></html>`;
}
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 try{
  const page=await browser.newPage({viewport:{width:1920,height:1080},deviceScaleFactor:1});
  await page.route('**/*',r=>r.abort());
  const inventory=[];
  for(const lang of ['en','ar']){
   const out=path.join(kit,'artwork',lang);fs.mkdirSync(out,{recursive:true});
   for(const d of entries[lang]){
    const html=documentFor(lang,d);fs.writeFileSync(path.join(out,d.id+'.html'),html);
    await page.setContent(html,{waitUntil:'load'});await page.evaluate(()=>document.fonts.ready);
    const bounds=await page.evaluate(()=>({width:document.documentElement.scrollWidth,height:document.documentElement.scrollHeight,fonts:document.fonts.status}));
    if(bounds.width!==1920||bounds.height!==1080||bounds.fonts!=='loaded')throw new Error('Unexpected artwork layout '+JSON.stringify(bounds));
    await page.screenshot({path:path.join(out,d.id+'.png')});
    inventory.push({language:lang,file:`artwork/${lang}/${d.id}.png`,width:1920,height:1080,caption:d.title.replaceAll('\n',' '),raw_capture:`screenshots/raw-${lang}-${d.image}.png`,generated_ui:false});
   }
  }
  fs.writeFileSync(path.join(kit,'artwork','inventory.json'),JSON.stringify(inventory,null,2)+'\n');
  const cards=inventory.map(x=>`<div><img src="${data(path.join(kit,x.file))}"><p>${x.language} · ${path.basename(x.file)}</p></div>`).join('');
  await page.setViewportSize({width:1920,height:2460});
  await page.setContent(`<html><style>body{margin:0;padding:24px;background:#e8e2d8;font:18px sans-serif;display:grid;grid-template-columns:1fr 1fr;gap:20px}div{min-width:0}img{width:100%;display:block}p{margin:5px 0}</style>${cards}</html>`);
  await page.screenshot({path:path.join(kit,'artwork','contact-sheet.png'),fullPage:true});
  console.log(JSON.stringify({artwork_images:inventory.length,languages:['en','ar'],dimensions:'1920x1080',real_screenshots:6}));
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
