'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const http = require('http');
const root = path.resolve(__dirname, '../integrations/lumia-pulseweaver');
const manifest = JSON.parse(fs.readFileSync(path.join(root,'manifest.json')));
const sleep = ms => new Promise(resolve=>setTimeout(resolve,ms));
async function until(fn) { for(let n=0;n<100;n++){ if(fn())return; await sleep(10); } throw Error('Timed out'); }
function load(lumia,port) {
 const sandbox = {module:{exports:{}},exports:{},require:id=>id==='@lumiastream/plugin'?{Plugin:class {constructor(m){this.manifest=m;this.lumia=lumia;this.settings={port,apiToken:'test-token'};}}}:require(id),process,Buffer,URLSearchParams,AbortController,fetch,setTimeout,clearTimeout,console};
 vm.runInNewContext(fs.readFileSync(path.join(root,'main.js'),'utf8'),sandbox,{filename:'main.js'});
 return new sandbox.module.exports(manifest,{});
}
async function fixture() {
 const alerts=[],variables=new Map(),options=[],requests=[],toasts=[],sockets=new Set();
 const state={operatorApi:2,activeStage:'Starting',stages:['Starting','Gaming'],destinations:{twitch:'horizontal',kick:'horizontal',youtube:'dual'},outputs:{},motion:{active:false,status:'READY',execution:null},motionActions:[{id:'motion-1',name:'Camera close-up',stage:'Gaming'}],sources:[{id:'mic',name:'Microphone',audio:true},{id:'media',name:'Clip',media:true},{id:'scene',name:'Main',items:[{itemId:'22',name:'Camera',visible:true}]}]};
 let subscriber; const outputTimers = new Map();
 const emit = event => { if(event.platform)state.outputs[event.output || event.platform]=event; subscriber?.write('data: '+JSON.stringify({kind:'event',...event})+'\n\n'); };
 const server = http.createServer((req,res)=>{
  requests.push(req.url); assert.equal(req.headers.authorization,'Bearer test-token'); assert.equal(req.headers['x-pulse-weaver-client'],'lumia-plugin');
  const url=new URL(req.url,'http://localhost');
  if(url.pathname.endsWith('/events')){ subscriber=res;res.writeHead(200,{'Content-Type':'text/event-stream'});const frame='data: '+JSON.stringify({kind:'snapshot',state})+'\n\n';res.write(frame.slice(0,23));setTimeout(()=>res.write(frame.slice(23)),5);return; }
  res.setHeader('Content-Type','application/json');
  if(url.pathname.endsWith('/state'))return res.end(JSON.stringify(state));
  if(url.pathname.endsWith('/end-stream')) {
   assert.equal(state.live, false, 'Legacy cleanup must never click a live/stale toggle');
   return res.end(JSON.stringify({ok:true}));
  }
  if(url.pathname.includes('/destination/')) {
   const platform=url.searchParams.get('platform'),start=url.pathname.endsWith('/start');
   emit({event:'destination_state',platform,output:platform,state:start?'starting':'stopping'});
   clearTimeout(outputTimers.get(platform));
   outputTimers.set(platform,setTimeout(()=>emit({event:'destination_state',platform,output:platform,state:start?'live':'stopped'}),40));
   return res.end(JSON.stringify({ok:true,message:'Accepted'}));
  }
  if(url.pathname.endsWith('/source'))return res.end(JSON.stringify({ok:true,message:'Applied'}));
  if(url.pathname.endsWith('/motion/run'))return res.end(JSON.stringify({ok:true,accepted:true,message:'Motion started'}));
  if(url.pathname.endsWith('/motion/stop'))return res.end(JSON.stringify({ok:true,accepted:true,message:'Motion restored'}));
  if(url.pathname.endsWith('/motion/original'))return res.end(JSON.stringify({ok:true,message:'Original scenes restored'}));
  res.statusCode=404;res.end(JSON.stringify({error:'Unknown route'}));
 });
 server.on('connection',socket=>{sockets.add(socket);socket.on('close',()=>sockets.delete(socket));});
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const lumia={updateConnection:async()=>{},setVariable:async(k,v)=>variables.set(k,v),updateActionFieldOptions:async v=>options.push(v),triggerAlert:async v=>alerts.push(v),showToast:async v=>toasts.push(v)};
 const plugin=load(lumia,server.address().port);await plugin.onload();await until(()=>plugin.state && options.length===6);
 return {plugin,state,alerts,variables,options,requests,toasts,emit,close:async()=>{await plugin.onunload();for(const socket of sockets)socket.destroy();await new Promise(resolve=>server.close(resolve));}};
}
test('Manifest includes the P logo, operating controls and native alerts; no editing or raw action',()=>{
 assert.equal(manifest.icon,'./assets/icon.png');assert.ok(fs.statSync(path.join(root,manifest.icon)).size>1000);
 assert.equal(manifest.id,'pulseweavercontrol');assert.equal(manifest.name,'Pulse Weaver');assert.equal(manifest.version,'1.4.0');
 assert.equal(manifest.config.settings.find(setting=>setting.key==='port').defaultValue,18755);
 assert.equal(manifest.config.actions.length,18);assert.equal(manifest.config.alerts.length,36);
 assert.ok(manifest.config.actions.some(action=>action.type==='run_motion' && action.fields[0].dynamicOptions));
 for(const action of manifest.config.actions)assert.ok(!/create|delete|transform|filter|raw|url|file/i.test(action.type));
 assert.equal(new Set(manifest.config.alerts.map(a=>a.key)).size,36);
});
test('Saved motion actions populate the existing plugin and use the guarded motion routes',async()=>{
 const f=await fixture();try{
  const motion=f.options.find(option=>option.actionType==='run_motion');
  assert.equal(JSON.stringify(motion.options),JSON.stringify([{label:'Camera close-up · Gaming',value:'motion-1'}]));
  const run=await f.plugin.actions({actions:[{type:'run_motion',value:{action:'motion-1'}}]});
  assert.equal(run.shouldStop,false);
  assert.ok(f.requests.some(route=>route.startsWith('/api/v1/lumia/motion/run?id=motion-1&request=')));
  await f.plugin.actions({actions:[{type:'stop_motion'}]});
  assert.ok(f.requests.some(route=>route==='/api/v1/lumia/motion/stop'));
  assert.equal(run.newlyPassedVariables.pulseweavercontrol_result,'Motion started');
  const original=await f.plugin.actions({actions:[{type:'restore_original_motion'}]});
  assert.equal(original.shouldStop,false);
  assert.ok(f.requests.includes('/api/v1/lumia/motion/original'));
  assert.equal(original.newlyPassedVariables.pulseweavercontrol_result,'Original scenes restored');
 }finally{await f.close();}
});

test('Custom ports, tokens and explicit config paths keep their installation identity',()=>{
 const plugin=load({},19755);
 assert.equal(plugin.connectionPort(),19755);
 assert.equal(plugin.connectionToken(),'test-token');
 plugin.settings.port=18765;
 assert.ok(plugin.configCandidates().every(file=>!file.includes('Motion Preview')));
 plugin.settings.configPath='C:\\portable\\pulse-weaver.ini';
 assert.equal(JSON.stringify(plugin.configCandidates()),JSON.stringify(['C:\\portable\\pulse-weaver.ini']));
 assert.equal(plugin.connectionPort(),18765);
});
test('Split SSE frames, initial snapshot without alerts, dynamic existing-source lists, no idle polling',async()=>{
 const f=await fixture();try{
 assert.equal(f.alerts.length,0);assert.equal(f.variables.get('active_stage'),'Starting');
 assert.ok(f.options.find(o=>o.actionType==='source_visibility').options[0].value.includes('22'));
 const initial=f.requests.length;await sleep(150);assert.equal(f.requests.length,initial);
 f.emit({event:'source_hidden',source:'Camera',scene:'Main',itemId:22});await until(()=>f.alerts.length===1);
 assert.equal(f.alerts[0].alert,'source_hidden');assert.equal(f.alerts[0].showInEventList,false);
 assert.equal(f.alerts[0].extraSettings.scene,'Main');
 }finally{await f.close();}
});
test('Platform start waits for real live state; stopping one leaves the others alone',async()=>{
 const f=await fixture();try{
 const pending=f.plugin.actions({actions:[{type:'start_platform',value:{platform:'kick'}}]});
 await until(()=>f.plugin.outputStatus('kick')==='starting');
 assert.notEqual(f.variables.get('kick_status'),'LIVE');
 const result=await pending;assert.equal(result.shouldStop,false);assert.equal(f.plugin.outputStatus('kick'),'live');
 f.state.outputs.twitch={platform:'twitch',output:'twitch',state:'live'};f.plugin.state.outputs.twitch=f.state.outputs.twitch;
 await f.plugin.actions({actions:[{type:'stop_platform',value:{platform:'kick'}}]});
 assert.equal(f.plugin.outputStatus('twitch'),'live');
 assert.ok(!f.requests.some(route=>route.includes('platform=twitch')));
 }finally{await f.close();}
});
test('Start Show refreshes a stale destination plan before choosing platforms',async()=>{
 const f=await fixture();try{
  f.plugin.state.destinations={twitch:'off',kick:'off',youtube:'off'};
  f.state.destinations={twitch:'off',kick:'horizontal',youtube:'off'};
  f.requests.length=0;
  const result=await f.plugin.actions({actions:[{type:'go_live'}]});
  assert.equal(result.shouldStop,false);
  assert.deepEqual(f.requests.filter(route=>route.includes('/destination/start')),['/api/v1/lumia/destination/start?platform=kick']);
  assert.equal(f.plugin.outputStatus('kick'),'live');
  assert.ok(f.requests[0].endsWith('/state'),'Start Show must refresh Pulse Weaver before filtering destinations');
 }finally{await f.close();}
});
test('Source controls preserve stable IDs and reject raw/editing and unsupported operations',async()=>{
 const f=await fixture();try{
 const ok=await f.plugin.actions({actions:[{type:'source_visibility',value:{target:JSON.stringify({source:'scene',item:'22'}),operation:'hide'}}]});assert.equal(ok.shouldStop,false);
 assert.ok(f.requests.some(route=>route.includes('action=hide&source=scene&item=22')));
 const before=f.requests.length;
 const rejected=await f.plugin.actions({actions:[{type:'raw',value:{url:'/scene/create'}}]});assert.equal(rejected.shouldStop,true);assert.equal(f.requests.length,before);
 const media=await f.plugin.actions({actions:[{type:'media_control',value:{source:'media',operation:'replace_file'}}]});assert.equal(media.shouldStop,true);
 }finally{await f.close();}
});
test('Native failures trigger the correct alert and fail confirmation; dual YouTube waits for both outputs',async()=>{
 const f=await fixture();try{
 f.emit({event:'destination_state',platform:'youtube',output:'primary',state:'live'});await until(()=>f.plugin.outputStatus('youtube')==='starting');
 f.emit({event:'destination_state',platform:'youtube',output:'vertical',state:'failed',message:'Connection refused'});await until(()=>f.plugin.outputStatus('youtube')==='failed');
 await assert.rejects(f.plugin.waitForOutput('youtube',true,50),/Connection refused/);
 assert.ok(f.alerts.some(a=>a.alert==='youtube_failed'));
 }finally{await f.close();}
});
test('A stale live snapshot cannot make a confirmation timeout look successful',async()=>{
 const f=await fixture();try{
 f.plugin.state.outputs.kick={platform:'kick',state:'starting'};
 await assert.rejects(f.plugin.waitForOutput('kick',true,20),/not confirmed/);
 f.plugin.state=null;await assert.rejects(f.plugin.waitForOutput('kick',false,20),/disconnected/);
 }finally{await f.close();}
});
test('Stop bypasses a pending start and the cancelled start cannot report success',async()=>{
 const f=await fixture();try{
 const starting=f.plugin.actions({actions:[{type:'start_platform',value:{platform:'kick'}}]});
 await until(()=>f.plugin.outputStatus('kick')==='starting');
 const stopping=f.plugin.actions({actions:[{type:'stop_platform',value:{platform:'kick'}}]});
 const [started,stopped]=await Promise.all([starting,stopping]);
 assert.equal(started.shouldStop,true);assert.match(started.newlyPassedVariables.pulseweavercontrol_result,/superseded/);
 assert.equal(stopped.shouldStop,false);assert.equal(f.plugin.outputStatus('kick'),'stopped');
 }finally{await f.close();}
});

test('End Show waits out the legacy cached-live flag and repeated stops never start an output',async()=>{
 const f=await fixture();try{
 f.state.live=true;
 for(const platform of ['twitch','kick','youtube'])f.emit({event:'destination_state',platform,output:platform,state:'live'});
 const stopping=f.plugin.actions({actions:[{type:'end_stream'}]});
 await until(()=>['twitch','kick','youtube'].every(p=>f.plugin.outputStatus(p)==='stopped'));
 await sleep(180);
 assert.ok(!f.requests.some(p=>p.endsWith('/end-stream')), 'No toggle while the shell still reports live');
 f.state.live=false;
 assert.equal((await stopping).shouldStop,false);
 assert.equal((await f.plugin.actions({actions:[{type:'end_stream'}]})).shouldStop,false);
 assert.equal(f.requests.filter(p=>p.endsWith('/end-stream')).length,2);
 assert.ok(!f.requests.some(p=>p.includes('/start')||p.endsWith('/go-live')));
 }finally{await f.close();}
});

test('End Show cleanup cannot run after a newer show command supersedes it',async()=>{
 const f=await fixture();try{
 f.state.live=true;
 const stopping=f.plugin.actions({actions:[{type:'end_stream'}]});
 await until(()=>['twitch','kick','youtube'].every(p=>f.plugin.outputStatus(p)==='stopped'));
 const starting=f.plugin.actions({actions:[{type:'start_platform',value:{platform:'kick'}}]});
 assert.equal((await stopping).shouldStop,true);
 assert.equal((await starting).shouldStop,false);
 assert.equal(f.plugin.outputStatus('kick'),'live');
 assert.ok(!f.requests.some(p=>p.endsWith('/end-stream')));
 }finally{await f.close();}
});
