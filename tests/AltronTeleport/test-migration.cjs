const assert=require('node:assert/strict');
const {migrate}=require('../../integrations/altron-teleport/bridge/Use-Native-Teleport.cjs');
const {nativeKind}=require('../../integrations/altron-teleport/bridge/native-control.cjs');
function fixture(failRename=false){
 const filters=[{filterName:'White Ranger Teleport',filterKind:'pw_white_teleport_filter',filterEnabled:true,filterSettings:{intensity:1.4}}],calls=[],inputs=[];
 const api={call:async(t,d={})=>{
  calls.push({t,d});
  if(t==='GetSourceFilterKindList')return{sourceFilterKinds:[nativeKind]};
  if(t==='GetSceneList')return{scenes:['PW White Teleport','PW Teleport Actor','Original Stage'].map(sceneName=>({sceneName}))};
  if(t==='GetSceneItemList')return{sceneItems:[{sourceName:'PW Teleport Actor'}]};
  if(t==='GetInputList')return{inputs};
  if(t==='GetSourceFilterList')return{filters:filters.map(f=>({...f}))};
  if(t==='CreateInput'){inputs.push({inputName:d.inputName,inputKind:d.inputKind});return{};}
  if(t==='CreateSourceFilter'){filters.push({...d,filterEnabled:true});return{};}
  if(t==='SetSourceFilterEnabled'){filters.find(f=>f.filterName===d.filterName).filterEnabled=d.filterEnabled;return{};}
  if(t==='SetSourceFilterName'){
   if(failRename&&d.filterName==='White Ranger Teleport (native migration)'){failRename=false;throw Error('Synthetic rename failure');}
   filters.find(f=>f.filterName===d.filterName).filterName=d.newFilterName;return{};
  }
  if(t==='RemoveSourceFilter'){filters.splice(filters.findIndex(f=>f.filterName===d.filterName),1);return{};}
  return{};
 }};return{api,filters,calls};
}
(async()=>{
 let f=fixture();await migrate(f.api);
 assert(f.filters.some(x=>x.filterName==='White Ranger Teleport (Lua backup)'&&!x.filterEnabled));
 assert(f.filters.some(x=>x.filterKind===nativeKind&&x.filterName==='White Ranger Teleport'&&x.filterEnabled));
 assert(f.calls.filter(c=>!c.t.startsWith('Get')).every(c=>c.d.sourceName==='PW Teleport Actor'||c.d.sceneName==='PW White Teleport'));
 f=fixture(true);await assert.rejects(migrate(f.api),/Synthetic/);
 assert.deepEqual(f.filters.map(x=>[x.filterName,x.filterKind,x.filterEnabled]),[['White Ranger Teleport','pw_white_teleport_filter',true]]);
 f=fixture();f.api.call=async(t)=>t==='GetSourceFilterKindList'?{sourceFilterKinds:[]}:assert.fail('Must stop before mutation');
 await assert.rejects(migrate(f.api),/Install/);
 console.log('PASS: isolated helper migration, disabled Lua backup, rollback after rename failure, missing-plugin guard.');
})().catch(e=>{console.error(e);process.exitCode=1;});
