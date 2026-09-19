// Seed only the disposable portable smoke-test build, never installed settings.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../../artifacts/alert-repair-native/config/obs-studio');
const scenes = path.join(root, 'basic/scenes/Untitled.json');
const document = JSON.parse(fs.readFileSync(scenes, 'utf8'));
if (document.sources.some(source => source.name === 'Native Fixture')) throw Error('Fixture already seeded');
delete document.DesktopAudioDevice1;
delete document.AuxAudioDevice1;
const scene = document.sources.find(source => source.name === 'Scene');
const item = {name:'Native Fixture',source_uuid:'11111111-2222-4333-8444-555555555555',id:7,
 visible:true,locked:true,rot:12.5,pos:{x:137.5,y:212.25},scale:{x:0.75,y:0.75},align:5,
 bounds_type:0,bounds_align:0,bounds:{x:0,y:0},crop_left:3,crop_top:4,crop_right:5,crop_bottom:6};
scene.settings.items=[item];scene.settings.id_counter=7;
document.sources.unshift({name:'Native Fixture',uuid:item.source_uuid,id:'browser_source',versioned_id:'browser_source',
 settings:{url:'http://127.0.0.1:18754/overlay/native-fixture?layout=landscape',width:1920,height:1080,webpage_control_level:4},
 enabled:true,volume:1,muted:false});
fs.writeFileSync(scenes,JSON.stringify(document,null,2));
fs.writeFileSync(path.resolve(__dirname,'../../artifacts/native-fixture-before.json'),JSON.stringify({sceneUuid:scene.uuid,item},null,2));
const dir=path.join(root,'plugin_config/pulse-weaver-core/overlays');
fs.renameSync(path.join(dir,'overlays.v2.json'),path.join(dir,'clean-start.v2.json'));
fs.writeFileSync(path.join(dir,'overlays.json'),JSON.stringify({schema:1,overlays:[{id:'native-fixture',name:'Native Fixture',
 width:1920,height:1080,eventDriven:false,durationMs:6123.5,customHtml:'<p id="legacy" style="color:white;font:48px sans-serif">Native legacy code awaiting event</p>',
 customJs:"const legacyEvents=new EventSource('/overlay/native-fixture/events');legacyEvents.onmessage=e=>document.getElementById('legacy').textContent='Native legacy bridge: '+JSON.parse(e.data).data.user;stage.dataset.legacy='yes';",
 elements:[{id:'headline',type:'text',text:'Native repaired renderer',x:100,y:200,width:1500,height:150,fontSize:72,color:'#ffffff',visible:true},
 {id:'user',type:'text',text:'Waiting',binding:'user',x:100,y:400,width:1500,height:150,fontSize:60,color:'#ffffff',visible:true}]}]},null,2));
console.log('Disposable native fixture seeded.');
