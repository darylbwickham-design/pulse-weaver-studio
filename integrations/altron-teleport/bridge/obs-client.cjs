const fs=require('node:fs');
const crypto=require('node:crypto');
const CONFIG=require('node:path').join(process.env.LOCALAPPDATA,'Programs','Pulse Weaver','config','obs-studio','plugin_config','obs-websocket','config.json');
async function connect(options={}){
 const configPath=options.backend==='obs'?(process.env.ALTRON_OBS_CONFIG||require('node:path').join(process.env.APPDATA,'obs-studio','plugin_config','obs-websocket','config.json')):CONFIG;
 const config=JSON.parse(fs.readFileSync(configPath,'utf8'));
 if(config.server_enabled===false)throw Error('Enable OBS WebSocket Server in Tools first');
 const socket=new WebSocket(`ws://127.0.0.1:${config.server_port||4455}`);
 const digest=s=>crypto.createHash('sha256').update(s).digest('base64');
 let seq=0;const tasks=new Map();
 await new Promise((resolve,reject)=>{
  const timer=setTimeout(()=>(socket.close(),reject(new Error('OBS WebSocket connection timed out'))),10000);
  socket.addEventListener('error',()=>{clearTimeout(timer);reject(new Error('OBS WebSocket connection failed'));});
  socket.addEventListener('close',()=>{clearTimeout(timer);reject(new Error('OBS WebSocket closed; check its password and server settings'));for(const task of tasks.values()){clearTimeout(task.timer);task.reject(new Error('OBS WebSocket disconnected'));}tasks.clear();});
  socket.addEventListener('message',event=>{
   const m=JSON.parse(event.data);
   if(m.op===0){const d={rpcVersion:1,eventSubscriptions:0};if(m.d.authentication)d.authentication=digest(digest(config.server_password+m.d.authentication.salt)+m.d.authentication.challenge);socket.send(JSON.stringify({op:1,d}));}
   if(m.op===2){clearTimeout(timer);resolve();}
   if(m.op===7){const task=tasks.get(m.d.requestId);if(!task)return;clearTimeout(task.timer);tasks.delete(m.d.requestId);m.d.requestStatus.result?task.resolve(m.d.responseData||{}):task.reject(new Error(m.d.requestStatus.comment||'Request failed'));}
  });
 });
 return {
  call(type,data={}){return new Promise((resolve,reject)=>{const id=String(++seq);const timer=setTimeout(()=>{tasks.delete(id);reject(new Error(type+' timed out'));},10000);tasks.set(id,{resolve,reject,timer});socket.send(JSON.stringify({op:6,d:{requestType:type,requestId:id,requestData:data}}));});},
  close(){socket.close();}
 };
}
module.exports={connect};
