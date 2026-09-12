// Original vector artwork for the native Qt interface. No filters, raster textures or animation.
// Run from the repository root: node packaging/Generate-VisualAssets.mjs
import fs from 'node:fs';
import path from 'node:path';
const root='engine/obs-studio/frontend/data/themes';
const presets={
  backstage:{accent:'#D46AFF',secondary:'#41DEFF',edge:'#943CDB',base:'#090E1C',top:'#221139',radius:10},
  marquee:{accent:'#FFC16A',secondary:'#FF6748',edge:'#D17F28',base:'#170D14',top:'#492017',radius:14},
  electric:{accent:'#44EAFF',secondary:'#AF68FF',edge:'#188DBE',base:'#061422',top:'#073B55',radius:0},
};
const glyphs={};
for(const file of fs.readdirSync(`${root}/PulseWeaver/icons`)) {
  glyphs[path.basename(file,'.svg')]=fs.readFileSync(`${root}/PulseWeaver/icons/${file}`,'utf8').replace(/^.*?<svg[^>]*>/s,'').replace(/<\/svg>\s*$/,'');
}
Object.assign(glyphs,{
  mixer:'<path d="M5 3v18M12 3v18M19 3v18"/><rect x="3" y="7" width="4" height="4" rx="1"/><rect x="10" y="13" width="4" height="4" rx="1"/><rect x="17" y="5" width="4" height="4" rx="1"/>',
  replay:'<path d="M5 7a8 8 0 1 1-1 9M5 3v5H1M10 8l6 4-6 4z"/>',
  horizontal:'<rect x="2" y="5" width="20" height="14" rx="1"/><path d="M8 12h8m-3-3 3 3-3 3"/>',
  vertical:'<rect x="6" y="2" width="12" height="20" rx="1"/><path d="M12 7v10m-3-3 3 3 3-3"/>',
  overlay:'<rect x="3" y="3" width="14" height="12" rx="1"/><path d="M8 15v6h13V9h-4M6 7h6M6 10h3"/>',
  lock:'<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3M12 14v3"/>',
  reset:'<path d="M4 9a8 8 0 1 1 1 9M4 3v6h6"/>',
  import:'<path d="M4 4h9v5M4 4v16h16v-7M10 12h11m-4-4 4 4-4 4"/>',
  send:'<path d="m3 3 18 9-18 9 3-9zM6 12h15"/>',
  clock:'<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
  stats:'<path d="M3 20h18M5 16v-5h3v5M11 16V7h3v9M17 16V3h3v13"/>',
  collapse:'<path d="m5 14 7-7 7 7M5 19h14"/>',
  add:'<path d="M12 4v16M4 12h16"/>',
  duplicate:'<rect x="8" y="8" width="13" height="13" rx="2"/><path d="M16 8V3H3v13h5"/>',
  delete:'<path d="M3 6h18M9 6V3h6v3M6 6l1 15h10l1-15M10 10v7M14 10v7"/>',
  done:'<path d="m4 12 5 5L20 6"/>',
});
const svg=(w,h,body)=>`<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">${body}</svg>\n`;
function shape(kind,w,h,inset=1){
  if(kind==='electric') return `<path d="M${inset+8} ${inset}H${w-inset}V${h-inset-8}L${w-inset-8} ${h-inset}H${inset}V${inset+8}Z"`;
  return `<rect x="${inset}" y="${inset}" width="${w-2*inset}" height="${h-2*inset}" rx="${kind==='marquee'?14:10}"`;
}
for(const [name,p] of Object.entries(presets)){
  const dir=`${root}/PulseWeaver/${name}`;
  fs.mkdirSync(`${dir}/icons`,{recursive:true});
  fs.mkdirSync(`${dir}/glyphs`,{recursive:true});
  const write=(file,body)=>fs.writeFileSync(`${dir}/${file}.svg`,body);
  for(const [key,glyph] of Object.entries(glyphs)){
    const orange=['action','record','delete'].includes(key);
    const cyan=['camera','horizontal','vertical','mixer','mic','connect','save','send'].includes(key);
    const ink=key==='lights'?'#D46AFF':orange?'#FFAD38':cyan?'#35DEFF':p.accent;
    const second=orange?'#FF4D78':cyan?'#9291FF':p.secondary;
    const outline=name==='marquee'?'<circle cx="32" cy="32" r="26"':shape(name,64,64,6);
    let body=`<defs><radialGradient id="glass" cx=".5" cy=".1" r="1"><stop stop-color="${p.top}"/><stop offset="1" stop-color="${p.base}"/></radialGradient><linearGradient id="neon" x2="1" y2="1"><stop stop-color="${ink}"/><stop offset="1" stop-color="${second}"/></linearGradient></defs>`;
    // Layered static strokes give the reference's light spill without a blur shader.
    body+=outline+` fill="url(#glass)" stroke="${ink}" stroke-width="10" stroke-opacity=".08"/>`;
    body+=outline+` fill="none" stroke="${ink}" stroke-width="6" stroke-opacity=".18"/>`;
    body+=outline+` fill="none" stroke="url(#neon)" stroke-width="2"/>`;
    if(name==='marquee')body+='<circle cx="32" cy="32" r="23" fill="none" stroke="'+ink+'" stroke-opacity=".35" stroke-width=".8"/>';
    for(const [width,opacity] of [[5,.09],[3,.2],[1.6,1]])body+=`<g transform="translate(15 15) scale(1.4167)" fill="none" stroke="url(#neon)" stroke-width="${width}" stroke-opacity="${opacity}" stroke-linecap="round" stroke-linejoin="round">${glyph}</g>`;
    write(`icons/${key}`,svg(64,64,body));
    let bare=`<defs><linearGradient id="neon" x2="1" y2="1"><stop stop-color="${ink}"/><stop offset="1" stop-color="${second}"/></linearGradient></defs>`;
    for(const [width,opacity] of [[4,.08],[2.6,.18],[1.6,1]])bare+=`<g transform="translate(4 4)" fill="none" stroke="url(#neon)" stroke-width="${width}" stroke-opacity="${opacity}" stroke-linecap="round" stroke-linejoin="round">${glyph}</g>`;
    write(`glyphs/${key}`,svg(32,32,bare));
  }
  for(const state of ['button','hover','pressed','live','record','active','panel','camera','lights','action','focus','focus-active']){
    const panel=state==='panel'; const w=panel?96:64,h=panel?96:48;
    const accent=state.startsWith('focus')?'#FFFFFF':state==='lights'?'#D46AFF':['record','action'].includes(state)?'#FF9F35':state==='camera'?'#35DEFF':state==='active'?'#FF3D73':p.accent;
    const edge=panel?p.edge:accent;
    const bottom=['active','focus-active'].includes(state)?'#670E27':state==='record'?'#4F2108':state==='hover'?p.top:state==='live'?(name==='marquee'?'#602A0B':name==='electric'?'#064854':'#4F0B7C'):p.base;
    let body=`<defs><linearGradient id="surface" x2="0" y2="1"><stop stop-color="${panel?bottom:p.top}"/><stop offset="1" stop-color="${bottom}"/></linearGradient></defs>`;
    body+=shape(name,w,h,4)+` fill="url(#surface)" stroke="${edge}" stroke-width="8" stroke-opacity="${panel?.08:.14}"/>`;
    body+=shape(name,w,h,4)+` fill="none" stroke="${edge}" stroke-width="5" stroke-opacity="${panel?.12:.3}"/>`;
    body+=shape(name,w,h,4)+` fill="none" stroke="${edge}" stroke-width="${panel?1:2}"/>`;
    if(name==='backstage'){
      if(panel)body+=`<path d="M5 20V12q0-7 7-7h8M${w-20} ${h-5}h8q7 0 7-7v-8" fill="none" stroke="${p.secondary}" stroke-width="2"/>`;
      else body+=shape(name,w,h,7)+` fill="none" stroke="#F6DEFF" stroke-opacity=".20" stroke-width=".8"/>`;
    }else if(name==='marquee'){
      body+=shape(name,w,h,8)+` fill="none" stroke="${edge}" stroke-opacity=".65" stroke-width="1"/>`;
      body+=`<path d="M4 20h4M20 4v4M${w-20} ${h-4}v-4M${w-4} ${h-20}h-4" stroke="${accent}" stroke-width="2"/>`;
    }else{
      body+=`<path d="M3 15V10l7-7h7M${w-17} ${h-3}h7l7-7v-5" fill="none" stroke="${edge}" stroke-width="2"/>`;
      if(!panel) body+=`<path d="M${w-7} 7h-7" stroke="${p.secondary}"/>`;
    }
    write(state,svg(w,h,body));
  }
  let artwork='';
  if(name==='backstage'){
    artwork=`<defs><linearGradient id="pulse"><stop stop-color="${p.accent}" stop-opacity="0"/><stop offset=".5" stop-color="${p.accent}"/><stop offset="1" stop-color="${p.secondary}" stop-opacity="0"/></linearGradient></defs>`;
    const line='M10 45h275l12-12 10 24 14-41 16 43 12-14h281';
    for(const [width,opacity] of [[14,.05],[8,.1],[4,.22],[1.5,.9]])artwork+=`<path d="${line}" fill="none" stroke="url(#pulse)" stroke-width="${width}" stroke-opacity="${opacity}"/>`;
  }else if(name==='marquee'){
    artwork=`<g fill="none" stroke="${p.edge}"><path d="M10 64h350l8-5h200l8 5h54M375 58V30Q475-25 575 30v28M384 58V34Q475-15 566 34v24M395 58V38Q475-4 555 38v20"/><path d="M425 18v36M450 12v42M475 9v45M500 12v42M525 18v36" stroke-opacity=".5"/></g><path d="m467 59 8-5 8 5-8 5z" fill="${p.accent}"/>`;
  }else{
    artwork=`<path d="M10 64h320l12-12h40l10 10h50l18-18h150l20-20" fill="none" stroke="${p.edge}"/><path d="M380 30h55l8-13 13 29 13-24 9 8h104" fill="none" stroke="${p.accent}" stroke-width="2"/><path d="M520 10h60l18 18M530 6h54l22 22" fill="none" stroke="${p.secondary}" stroke-opacity=".65"/><circle cx="380" cy="30" r="3" fill="${p.accent}"/>`;
  }
  write('banner',svg(640,72,artwork));
  // Platform identity belongs in the artwork: a CSS border colour is hidden
  // when Qt paints a border-image. The existing provider property selects it.
  const platforms={twitch:'#9146FF',youtube:'#FF3B30',kick:'#53FC18',recording:'#FFAD38'};
  for(const [provider,colour] of Object.entries(platforms)){
    const panel=fs.readFileSync(`${dir}/panel.svg`,'utf8');
    write(`panel-${provider}`,panel.replace('</svg>',`<defs><linearGradient id="platform" gradientUnits="userSpaceOnUse" x1="8" y1="0" x2="88" y2="0"><stop stop-color="${colour}" stop-opacity="0"/><stop offset=".14" stop-color="${colour}"/><stop offset=".86" stop-color="${colour}"/><stop offset="1" stop-color="${colour}" stop-opacity="0"/></linearGradient></defs><path d="M8 6H88" stroke="url(#platform)" stroke-width="8" stroke-opacity=".12"/><path d="M8 6H88" stroke="url(#platform)" stroke-width="4"/></svg>`));
  }
  // Section plates are static ornaments, leaving all text live and accessible.
  const selectors='QPushButton#PulseWeaverControl, QPushButton#PulseWeaverUtility, QPushButton#PulseWeaverOverlayButton, QPushButton#PulseWeaverDockLockToggle';
  const nav='QPushButton#PulseWeaverHome, QPushButton#PulseWeaverNav';
  const url=file=>`url(theme:PulseWeaver/${name}/${file}.svg)`;
  // The SVG owns the silhouette. A QSS border-radius clips Qt's border-image edge strips.
  const rule=(sel,file)=>`${sel} { border-image: ${url(file)} 14 14 14 14 stretch stretch; border-width: 7px; border-style: solid; border-color: transparent; border-radius: 0; background-color: transparent; }`;
  const each=(sel,suffix)=>sel.split(', ').map(s=>s+suffix).join(', ');
  let qss=`\n/* Original ${name} vector control surrounds and stage artwork. */\n`;
  qss+=`${selectors} { padding: 1px 6px; }\n`+rule(selectors,'button')+'\n'+rule(each(selectors,':hover'),'hover')+'\n'+rule(each(selectors,':pressed'),'pressed')+'\n'+rule(each(selectors,':checked'),'pressed')+'\n';
  qss+=`${nav} { padding: 6px 10px; border-radius: ${p.radius}px; font-family: "Segoe UI"; font-size: 13px; font-weight: 600; color: var(--text); }\n`+rule(nav,'button')+'\n'+rule(each(nav,'[pulseWorkspaceActive="true"]'),'hover')+'\n';
  qss+=rule('QPushButton#PulseWeaverNav[workspace="camera"]','camera')+'\n'+rule('QPushButton#PulseWeaverNav[workspace="lights"]','lights')+'\n'+rule('QPushButton#PulseWeaverNav[workspace="action"]','action')+'\n';
  qss+=`${each(nav,'[pulseWorkspaceActive="true"]')} { color: var(--text_light); font-weight: 600; }\n`;
  // Keep keyboard focus legible without a second, white frame around the artwork.
  qss+=`${each(nav,':focus')} { text-decoration: underline; }\n`;
  qss+=`QFrame#PulseWeaverBanner { background: var(--pw_header); background-image: ${url('banner')}; background-repeat: no-repeat; background-position: center center; border: none; border-radius: ${p.radius}px; }\n`;
  qss+=rule('QFrame#PulseWeaverStage, QFrame#PulseWeaverStreamStats, QFrame#PulseWeaverCard','panel')+'\n';
  for(const [provider,colour] of Object.entries(platforms)){
    qss+=rule(`QFrame#PulseWeaverStage[pulseWeaverPreviewProvider="${provider}"]`,`panel-${provider}`)+'\n';
    qss+=rule(`QFrame#PulseWeaverStreamStats[pulseWeaverStatsAccent="${colour}"]`,`panel-${provider}`)+'\n';
  }
  qss+=rule('QPushButton#PulseWeaverLive','live')+'\n'+rule('QPushButton#PulseWeaverRecord','record')+'\n';
  qss+=rule('QPushButton#PulseWeaverLive[pulseActive="true"], QPushButton#PulseWeaverRecord[pulseActive="true"]','active')+'\n';
  qss+=`${each(selectors,':disabled')}, QPushButton#PulseWeaverLive:disabled, QPushButton#PulseWeaverRecord:disabled { border-image: none; border: 1px solid var(--pw_rule); background: var(--bg_base); color: var(--text_disabled); }\n`;
  qss+=rule(`${each(selectors,':focus')}, QPushButton#PulseWeaverLive:focus, QPushButton#PulseWeaverRecord:focus`,'focus')+'\n';
  qss+=rule('QPushButton#PulseWeaverLive[pulseActive="true"]:focus, QPushButton#PulseWeaverRecord[pulseActive="true"]:focus','focus-active')+'\n';
  if(name==='marquee')qss+='QLabel#PulseWeaverHeading { font-family: "Georgia"; font-size: 21px; font-weight: 400; }\nQLabel#PulseWeaverKicker { letter-spacing: 1px; }\nQTabBar::tab:selected { border-bottom: 3px double var(--pw_accent); }\nQDockWidget::title { border-bottom: 3px double var(--pw_rule); }\n';
  if(name==='backstage')qss+='QComboBox, QLineEdit, QSpinBox { border-radius: 6px; }\nQDockWidget::title { border-left: 3px solid var(--pw_secondary); }\n';
  if(name==='electric')qss+='QLabel#PulseWeaverHeading { font-family: "Bahnschrift"; font-size: 22px; }\nQComboBox, QLineEdit, QSpinBox { border-radius: 0; border-bottom: 2px solid var(--pw_rule); }\nQTabBar::tab:selected { border-top: 2px solid var(--pw_accent); border-bottom: none; }\nQDockWidget::title { border-bottom: 2px solid var(--pw_secondary); }\n';
  const file=name==='backstage'?'PulseWeaver.ovt':`PulseWeaver_${name[0].toUpperCase()+name.slice(1)}.ovt`;
  const existing=fs.readFileSync(`${root}/${file}`,'utf8').split('\n/* Original ')[0].trimEnd();
  fs.writeFileSync(`${root}/${file}`,existing+'\n'+qss);
}
console.log(`Generated ${Object.keys(glyphs).length} icon tiles, ${Object.keys(glyphs).length} glyphs and seventeen surface assets for each of three presets.`);
