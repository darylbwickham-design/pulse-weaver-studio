// Local browser fixture: production C++ emits HTML; this server only supplies
// synthetic authenticated transport/events. It never opens installed profiles.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const output = path.join(root, 'artifacts/overlay-browser-repair');
fs.mkdirSync(output, {recursive:true});
const executable = path.join(root, 'tests/OverlayAlerts/build/Release/PulseOverlayAlertTests.exe');
const runtime = path.join(root, 'engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo/bin/64bit');
const env = {...process.env, PATH: runtime + path.delimiter + process.env.PATH};
function emit(args) { const r=cp.spawnSync(executable,args,{env,encoding:'utf8'}); if(r.status!==0)throw Error(r.stderr||r.error||'Renderer exporter failed'); }
const probe = `
const stats={plays:0,pauses:0,active:0,maxActive:0,speech:0,cancels:0,messages:0,bridge:'waiting',parentAccess:'blocked'};
const output=document.createElement('pre');output.id='diagnostics';output.style.cssText='position:fixed;left:0;top:0;color:white;background:black;z-index:999';document.body.append(output);
function report(){output.textContent=JSON.stringify(stats)}
try{void parent.location.hash;stats.parentAccess='ESCAPED'}catch(e){}
window.Audio=class{constructor(){this.playing=false}play(){this.playing=true;stats.plays++;stats.active++;stats.maxActive=Math.max(stats.maxActive,stats.active);report();return Promise.resolve()}pause(){if(this.playing){this.playing=false;stats.pauses++;stats.active--}report()}removeAttribute(){}load(){}};
Object.defineProperty(window,'speechSynthesis',{value:{cancel(){stats.cancels++;report()},speak(){stats.speech++;report()}}});
const customEvents=new EventSource('/overlay/fixture/events');customEvents.onmessage=e=>{const data=JSON.parse(e.data).data;stats.messages++;stats.bridge='connected';document.getElementById('legacy').textContent='Legacy received '+(data.user||'snapshot');report()};
stage.dataset.legacy='accessible';report();
`;
for(const revision of [1,2]){
 const fixture={id:'fixture',name:'Browser regression',width:960,height:540,durationMs:1800,eventDriven:true,
  fontFamily:"Segoe UI</style><script>parent.document.body.dataset.fontEscape='yes'</script>",
  customCss:".e{color:white} </style><script>parent.document.body.dataset.cssEscape='yes'</script>",
  customHtml:'<p id="legacy" style="color:white">Legacy awaiting event</p>',customJs:probe,
  elements:[{id:'headline',type:'text',text:'Revision '+revision,binding:'',x:50,y:150,width:800,height:100,fontSize:40,color:'#ffffff',visible:true},
  {id:'user',type:'text',text:'Waiting',binding:'user',x:50,y:250,width:800,height:100,fontSize:40,color:'#ffffff',visible:true}]};
 const input=path.join(output,'revision-'+revision+'.json');fs.writeFileSync(input,JSON.stringify(fixture));
 emit(['--render',input,'landscape','',path.join(output,'revision-'+revision+'.html')]);
 if(revision===1)emit(['--render',input,'portrait','/preview/test/fixture/events',path.join(output,'preview.html')]);
}
emit(['--bootstrap',path.join(output,'bootstrap.html')]);
let revision=1,renderRequests=0,snapshot=null;
const streams=new Map();
const token='synthetic-browser-test-token';
const send=(res,event)=>res.write('data: '+JSON.stringify(event)+'\n\n');
const server=http.createServer((req,res)=>{
 res.setHeader('Cache-Control','no-store');
 if(req.url==='/state'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({revision,renderRequests,connections:streams.size}));return;}
 if(req.method==='POST'&&req.url.startsWith('/control/')){
  let body='';req.on('data',chunk=>body+=chunk);req.on('end',()=>{
   const data=body?JSON.parse(body):{};
   if(req.url==='/control/publish'){revision=2;for(const [stream,route]of streams)if(route==='live')send(stream,{data:{__pwControl:'reload'}});}
   if(req.url==='/control/event'){snapshot={event:'fixture.event',data};for(const [stream,route]of streams)if(route==='live')send(stream,snapshot);}
   if(req.url==='/control/preview')for(const [stream,route]of streams)if(route==='preview')send(stream,{event:'fixture.event',data});
   if(req.url==='/control/reconnect')for(const stream of streams.keys())stream.end();
   res.end('ok');
  });return;
 }
 if(req.url.startsWith('/overlay/fixture/events')||req.url.startsWith('/preview/test/fixture/events')){
  const route=req.url.startsWith('/preview/')?'preview':'live';
  if(req.headers.authorization!=='Bearer '+(route==='live'?token:'preview-test-token')){res.writeHead(401);res.end();return;}
  res.writeHead(200,{'Content-Type':'text/event-stream'});res.write(': connected\n\n');streams.set(res,route);
  res.on('close',()=>streams.delete(res));
  if(route==='live'&&snapshot?.data.__pwAlert)send(res,{...snapshot,data:{...snapshot.data,__pwAlert:{...snapshot.data.__pwAlert,muted:true,remainingMs:500}}});return;
 }
 let file;
 if(req.url.startsWith('/overlay/fixture/render')){if(req.headers.authorization!=='Bearer '+token){res.writeHead(401);res.end();return;}renderRequests++;file='revision-'+revision+'.html';}
 else if(req.url.startsWith('/overlay/fixture'))file='bootstrap.html';
 else if(req.url.startsWith('/preview/test/fixture'))file='preview.html';
 if(!file){res.writeHead(404);res.end();return;}
 res.setHeader('Content-Type','text/html; charset=utf-8');res.end(fs.readFileSync(path.join(output,file)));
});
server.listen(0,'127.0.0.1',()=>console.log(JSON.stringify({url:'http://127.0.0.1:'+server.address().port,token})));
process.on('SIGINT',()=>{for(const stream of streams.keys())stream.end();server.closeAllConnections();server.close(()=>process.exit(0));});
