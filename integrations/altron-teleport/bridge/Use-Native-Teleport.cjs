// Upgrade only the helpers created by Altron setup. Stage definitions and original cameras are untouched.
const {connect}=require('./obs-client.cjs');
const {nativeKind}=require('./native-control.cjs');
async function migrate(api) {
    const actor='PW Teleport Actor',show='PW White Teleport',name='White Ranger Teleport',audio='Altron Teleport Audio';
    const kinds=(await api.call('GetSourceFilterKindList')).sourceFilterKinds;
    if(!kinds.includes(nativeKind))throw Error('Install the Pulse Weaver teleport alpha and restart the app first.');
    const sceneNames=(await api.call('GetSceneList')).scenes.map(s=>s.sceneName);
    if(!sceneNames.includes(actor)||!sceneNames.includes(show))throw Error('Run Setup-Pulse-Weaver.cmd first to create dedicated Altron helpers.');
    const items=(await api.call('GetSceneItemList',{sceneName:show})).sceneItems;
    if(!items.some(i=>i.sourceName===actor))throw Error('Dedicated show does not contain the Altron actor helper; stopped without changing it.');
    const input=(await api.call('GetInputList')).inputs.find(i=>i.inputName===audio);
    if(input&&input.unversionedInputKind!=='pw_altron_teleport_audio'&&input.inputKind!=='pw_altron_teleport_audio')throw Error('An unrelated source already uses the teleport audio name.');
    const filters=(await api.call('GetSourceFilterList',{sourceName:actor})).filters;
    const old=filters.find(f=>f.filterName===name);
    if(old&&![nativeKind,'pw_white_teleport_filter'].includes(old.filterKind))throw Error('An unrelated filter uses the teleport filter name.');
    if(old&&old.filterKind!==nativeKind&&filters.some(f=>f.filterName===name+' (Lua backup)'))throw Error('A Lua backup already exists; inspect the dedicated helper before migrating again.');
    if(!input)await api.call('CreateInput',{sceneName:show,inputName:audio,inputKind:'pw_altron_teleport_audio',inputSettings:{volume:70,monitor:false},sceneItemEnabled:true});
    else if(!items.some(i=>i.sourceName===audio))await api.call('CreateSceneItem',{sceneName:show,sourceName:audio,sceneItemEnabled:true});
    if(!old||old.filterKind!==nativeKind){
      const temporary=name+' (native migration)';
      if(filters.some(f=>f.filterName===temporary))throw Error('An unfinished native migration exists; inspect the dedicated helper first.');
      // Construct the replacement first. The working Lua filter stays enabled until this succeeds.
      await api.call('CreateSourceFilter',{sourceName:actor,filterName:temporary,filterKind:nativeKind,filterSettings:{...(old?.filterSettings||{}),role:0,preview_time:-1,audio_source:audio}});
      let renamed=false;
      try{
        await api.call('SetSourceFilterEnabled',{sourceName:actor,filterName:temporary,filterEnabled:false});
        if(old){
          await api.call('SetSourceFilterEnabled',{sourceName:actor,filterName:name,filterEnabled:false});
          await api.call('SetSourceFilterName',{sourceName:actor,filterName:name,newFilterName:name+' (Lua backup)'});renamed=true;
        }
        await api.call('SetSourceFilterName',{sourceName:actor,filterName:temporary,newFilterName:name});
        await api.call('SetSourceFilterEnabled',{sourceName:actor,filterName:name,filterEnabled:true});
      }catch(error){
        const actual=(await api.call('GetSourceFilterList',{sourceName:actor})).filters.find(f=>f.filterKind===nativeKind&&(f.filterName===temporary||f.filterName===name));
        if(actual)await api.call('RemoveSourceFilter',{sourceName:actor,filterName:actual.filterName});
        if(renamed)await api.call('SetSourceFilterName',{sourceName:actor,filterName:name+' (Lua backup)',newFilterName:name});
        if(old)await api.call('SetSourceFilterEnabled',{sourceName:actor,filterName:name,filterEnabled:old.filterEnabled!==false});
        throw error;
      }
    }
}
if(require.main===module)(async()=>{
 const api=await connect({backend:process.env.ALTRON_SETUP_BACKEND==='obs'?'obs':'pulse-weaver'});
 try{await migrate(api);console.log('Native teleport ready. Open Altron Teleport Audio > Properties to choose your arrival/departure files. Use the updated Start-Bridge.cmd for Lumia/Lumi-Con.');}finally{api.close();}
})().catch(e=>{console.error(e.message);process.exitCode=1;});
module.exports={migrate};
