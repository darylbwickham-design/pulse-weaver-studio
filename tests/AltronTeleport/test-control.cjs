const assert=require('node:assert/strict');
const {trigger}=require('../../integrations/altron-teleport/bridge/native-control.cjs');
(async()=>{
 const config={actorScene:'Only this helper',filterName:'Teleport'},calls=[];
 const api={call:async(t,d)=>{calls.push({t,d});return t==='GetSourceFilter'?{filterKind:'pw_altron_teleport_filter'}:{};}};
 for(const cue of ['hide','out','in','reset'])await trigger(api,config,cue);
 const edits=calls.filter(c=>c.t==='SetSourceFilterSettings');
 assert.deepEqual(edits.map(c=>c.d.filterSettings.cue),['hide','out','in','reset']);
 assert.equal(new Set(edits.map(c=>c.d.filterSettings.cue_id)).size,4);
 assert(edits.every(c=>c.d.sourceName===config.actorScene&&c.d.filterName===config.filterName&&c.d.overlay));
 assert(!calls.some(c=>c.t==='TriggerHotkeyByName'));
 calls.length=0;
 api.call=async(t,d)=>{calls.push({t,d});return t==='GetSourceFilter'?{filterKind:'pw_white_teleport_filter'}:{};};
 await trigger(api,config,'in');assert.equal(calls.at(-1).d.hotkeyName,'pw_white_teleport_in');
 api.call=async()=>({filterKind:'unrelated_filter'});await assert.rejects(trigger(api,config,'out'),/not an Altron/);
 console.log('PASS: native cues target only the configured helper, unique counters, legacy fallback and unrelated-filter guard.');
})().catch(e=>{console.error(e);process.exitCode=1;});
