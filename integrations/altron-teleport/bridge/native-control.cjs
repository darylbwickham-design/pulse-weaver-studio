let nonce = Date.now() * 1000;
const nativeKind = 'pw_altron_teleport_filter';
const legacy = {out:'pw_white_teleport',in:'pw_white_teleport_in',hide:'pw_white_teleport_hide',reset:'pw_white_teleport_reset'};
async function trigger(api, config, cue) {
  if (!Object.hasOwn(legacy, cue)) throw Error('Unknown teleport cue');
  const filter = await api.call('GetSourceFilter', {sourceName:config.actorScene,filterName:config.filterName});
  if (filter.filterKind === nativeKind) {
    return api.call('SetSourceFilterSettings', {sourceName:config.actorScene,filterName:config.filterName,filterSettings:{cue,cue_id:++nonce,preview_time:-1},overlay:true});
  }
  if (filter.filterKind && filter.filterKind !== 'pw_white_teleport_filter') throw Error('The configured filter is not an Altron teleport filter');
  return api.call('TriggerHotkeyByName', {hotkeyName:legacy[cue]});
}
module.exports = {trigger,nativeKind};
