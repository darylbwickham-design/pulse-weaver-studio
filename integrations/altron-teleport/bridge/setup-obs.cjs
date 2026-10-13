const {connect}=require('./obs-client.cjs');
async function main(){let camera=process.argv[2],alerts=process.argv[3];if(!camera){const prompt=require('node:readline/promises').createInterface({input:process.stdin,output:process.stdout});try{camera=(await prompt.question('Existing background-removed camera scene/source name: ')).trim();alerts=(await prompt.question('Existing alert source/scene name (Enter to skip): ')).trim();}finally{prompt.close();}}if(!camera)throw Error('A camera cutout scene/source name is required.');const api=await connect({backend:process.env.ALTRON_SETUP_BACKEND==='pulse-weaver'?'pulse-weaver':'obs'});try{
 const names=['PW Teleport AI Camera','PW Teleport Actor','Altron Background','PW White Teleport'];
 const protectedNames=[...names,'Altron Teleport Audio'];
 const scenes=(await api.call('GetSceneList')).scenes.map(s=>s.sceneName),inputs=(await api.call('GetInputList')).inputs.map(i=>i.inputName);
 if(protectedNames.some(n=>scenes.includes(n)||inputs.includes(n)))throw Error('Dedicated Altron names already exist. Setup stops without editing them.');
 for(const n of [camera,alerts].filter(Boolean))if(!scenes.includes(n)&&!inputs.includes(n))throw Error('Source not found: '+n);
 if(names.includes(camera)||names.includes(alerts))throw Error('Choose your existing camera and alert sources');
 const video=await api.call('GetVideoSettings');if(Math.abs(video.baseWidth/video.baseHeight-16/9)>.01)throw Error('Use a 16:9 base canvas for this landscape effect; existing video settings are unchanged.');
 const kinds=await api.call('GetSourceFilterKindList');const native=kinds.sourceFilterKinds.includes('pw_altron_teleport_filter');if(!native&&!kinds.sourceFilterKinds.includes('pw_white_teleport_filter'))throw Error('Install the native teleport alpha first. The previous Lua filter remains a supported fallback.');
 const add=async(sceneName,sourceName)=>api.call('CreateSceneItem',{sceneName,sourceName,sceneItemEnabled:true});
 await api.call('CreateScene',{sceneName:names[0]});const cutout=await add(names[0],camera);
 await api.call('SetSceneItemTransform',{sceneName:names[0],sceneItemId:cutout.sceneItemId,sceneItemTransform:{positionX:0,positionY:0,alignment:5,boundsType:'OBS_BOUNDS_SCALE_INNER',boundsWidth:video.baseWidth,boundsHeight:video.baseHeight,scaleX:1,scaleY:1}});
 await api.call('CreateScene',{sceneName:names[1]});await add(names[1],names[0]);
 await api.call('CreateSourceFilter',{sourceName:names[1],filterName:'White Ranger Teleport',filterKind:native?'pw_altron_teleport_filter':'pw_white_teleport_filter',filterSettings:{role:0,intensity:1.3,preview_time:-1}});
 await api.call('CreateScene',{sceneName:names[3]});
 await api.call('CreateInput',{sceneName:names[3],inputName:names[2],inputKind:'browser_source',inputSettings:{url:'http://127.0.0.1:8766/mission?v=3',width:1920,height:1080,fps:30,shutdown:false},sceneItemEnabled:true});
 const room=(await api.call('GetSceneItemList',{sceneName:names[3]})).sceneItems.find(i=>i.sourceName===names[2]);await api.call('SetSceneItemTransform',{sceneName:names[3],sceneItemId:room.sceneItemId,sceneItemTransform:{positionX:0,positionY:0,alignment:5,scaleX:video.baseWidth/1920,scaleY:video.baseHeight/1080}});
 await add(names[3],names[1]);if(native)await api.call('CreateInput',{sceneName:names[3],inputName:'Altron Teleport Audio',inputKind:'pw_altron_teleport_audio',inputSettings:{volume:70,monitor:false},sceneItemEnabled:true});if(alerts)await add(names[3],alerts);
 console.log('Created dedicated show scene and camera helpers. Existing sources, filters and current program scene were not edited.');console.log(process.env.ALTRON_SETUP_BACKEND==='pulse-weaver'?'Create a new PW White Teleport Stage in Manage stages, using the PW White Teleport scene for its landscape view. Save Lumia mode = Pulse Weaver Stage.':'Save Lumia mode = OBS scene (standard OBS).');console.log('Run Start-Bridge.cmd and TEST altron-show after importing the overlay and command.');
 }finally{api.close();}}
main().catch(e=>{console.error(e.message);process.exitCode=1;});
