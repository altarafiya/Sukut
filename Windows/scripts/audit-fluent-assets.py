"""Render icon and Fluent glyph audit sheets; validate PNG-backed ICO frames."""
from pathlib import Path
import re, struct, json
from PIL import Image, ImageDraw, ImageFont

root=Path(__file__).resolve().parents[1]
assets=root/'src/sukut-ui/Assets'
proof=root/'docs/redesign/icon-assets'
proof.mkdir(parents=True,exist_ok=True)
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',18)
icons=ImageFont.truetype('C:/Windows/Fonts/SegoeIcons.ttf',28)
sheet=Image.new('RGB',(1000,560),'#777777'); d=ImageDraw.Draw(sheet)
for row,bg in enumerate(['#fafafa','#202020','#343039']):
    d.rectangle((0,row*180,1000,(row+1)*180),fill=bg)
    d.text((16,row*180+8),['Light','Dark','Mica-like proof (static)'][row],font=font,fill='#777777')
    x=16
    for size in [16,20,24,32,48,256]:
        icon=Image.open(proof/f'Sukut-{size}.png').convert('RGBA')
        if size==256:icon=icon.resize((128,128))
        sheet.paste(icon,(x,row*180+40),icon); d.text((x,row*180+147),str(size),font=font,fill='#777777');x+=max(size,48)+20
    x=570
    for state in ['off','on']:
        for theme in ['light','dark']:
            ico=Image.open(proof/f'Tray-{state}-{theme}-16.png').convert('RGBA')
            sheet.paste(ico,(x,row*180+50),ico)
            d.text((x,row*180+80),state+'\n'+theme,font=font,fill='#777777');x+=100
sheet.save(proof/'icon-proof.png')
layers={}
for file in assets.glob('*.ico'):
    b=file.read_bytes(); count=struct.unpack_from('<H',b,4)[0]; sizes=[]
    for n in range(count):
        w,h,_,_,planes,bpp,length,offset=struct.unpack_from('<BBBBHHII',b,6+n*16)
        w=w or 256;h=h or 256
        assert b[offset:offset+8]==b'\x89PNG\r\n\x1a\n'
        assert planes==1 and bpp==32 and w==h
        assert struct.unpack_from('>II',b,offset+16)==(w,h)
        sizes.append(w)
    assert sizes==([16,20,24,32,40,48,64,128,256] if file.stem=='Sukut' else [16,20,24,32,48])
    layers[file.name]=sizes
(proof/'ico-layers.json').write_text(json.dumps(layers,indent=2))
sources='\n'.join(p.read_text(encoding='utf-8-sig') for p in (root/'src/sukut-ui').glob('*') if p.suffix in ['.cs','.xaml'])
codes=sorted(set(int(x,16) for x in re.findall(r'(?:&#x|\\u)([EeFf][0-9a-fA-F]{3})',sources)))
names={0xe80f:'Home',0xe946:'Info',0xe713:'Settings',0xe7e8:'PowerButton',0xe74f:'Mute',0xe9e9:'Equalizer',0xec4f:'MusicNote',0xe7f5:'Speakers',0xe720:'Microphone',0xe774:'Globe',0xe790:'Color',0xe73f:'BackToWindow',0xe95f:'Wire',0xe72c:'Refresh',0xe95b:'Headset',0xe7f6:'Headphone',0xe7f4:'TVMonitor',0xe767:'Volume'}
glyphs=Image.new('RGB',(800,len(codes)*48+32),'#fafafa');g=ImageDraw.Draw(glyphs)
for i,code in enumerate(codes):
    y=16+i*48;g.text((16,y),chr(code),font=icons,fill='#222222');g.text((80,y+5),f'{code:04X}  {names.get(code,"REVIEW")}',font=font,fill='#222222')
glyphs.save(proof/'fluent-glyph-audit.png')
print(json.dumps({'ico_layers':layers,'glyph_codes':[f'{c:04X}' for c in codes]},indent=2))
