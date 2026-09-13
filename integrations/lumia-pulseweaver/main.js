'use strict';
const { Plugin } = require('@lumiastream/plugin');
const fs = require('fs');
const path = require('path');
const http = require('http');
const { EventEmitter } = require('events');

class PulseWeaverPlugin extends Plugin {
  constructor(manifest, context) {
    super(manifest, context);
    this.token = ''; this.state = null; this.enabled = false;
    this.stream = null; this.retryTimer = null; this.retry = 0;
    this.generation = 0; this.values = new Map(); this.options = new Map();
    this.changes = new EventEmitter(); this.controllers = new Set();
    this.eventQueue = Promise.resolve(); this.pendingEvents = 0;
    this.actionQueue = Promise.resolve();
	this.operations = new Map(); this.showAttempt = 0;
  }
  async onload() { this.enabled = true; this.connect(); }
  async onunload() {
    this.enabled = false; ++this.generation;
    clearTimeout(this.retryTimer); this.stream?.destroy(); this.stream = null;
    for (const controller of this.controllers) controller.abort();
    this.state = null; this.changes.emit('state');
    await this.lumia.updateConnection(false);
  }
  async onsettingsupdate() {
    await this.onunload(); this.token = ''; this.retry = 0;
    this.values.clear(); this.options.clear(); await this.onload();
  }
  configCandidates() {
    const configured = typeof this.settings.configPath === 'string' ? this.settings.configPath.trim() : '';
    const local = process.env.LOCALAPPDATA || '';
    return [configured, local && path.join(local, 'Programs', 'Pulse Weaver', 'config', 'obs-studio', 'plugin_config', 'pulse-weaver-core', 'pulse-weaver.ini')].filter(Boolean);
  }
  connectionToken() {
    const configured = typeof this.settings.apiToken === 'string' ? this.settings.apiToken.trim() : '';
    if (configured) return configured;
    if (this.token) return this.token;
    for (const file of this.configCandidates()) {
      try {
        const content = fs.readFileSync(file, 'utf8');
        const section = content.match(/(?:^|\r?\n)\[api\]([\s\S]*?)(?=\r?\n\[|$)/i)?.[1] || '';
        const token = section.match(/(?:^|\r?\n)token\s*=\s*([^\r\n]+)/i)?.[1]?.trim() || content.match(/(?:^|\r?\n)api\/token\s*=\s*([^\r\n]+)/i)?.[1]?.trim();
        if (token) return (this.token = token);
      } catch { /* Custom portable path can be supplied in settings. */ }
    }
    throw new Error('Start Pulse Weaver or set its configuration path in the plugin settings.');
  }
  endpoint(route) {
    const port = Number(this.settings.port || 18755);
    if (!Number.isInteger(port) || port < 1024 || port > 65535) throw new Error('Invalid Pulse Weaver port.');
    if (!route.startsWith('/') || route.includes('..') || route.includes('\\')) throw new Error('Invalid operation route.');
    return `http://127.0.0.1:${port}/api/v1/lumia${route}`;
  }
  headers() { return { Authorization: `Bearer ${this.connectionToken()}`, 'X-Pulse-Weaver-Client': 'lumia-plugin' }; }
  async request(route, method = 'GET') {
    const controller = new AbortController(); this.controllers.add(controller);
    const timeout = setTimeout(() => controller.abort(), 30000);
    try {
      const response = await fetch(this.endpoint(route), { method, headers: this.headers(), signal: controller.signal, redirect: 'error' });
      const payload = await response.json();
      if (!response.ok || payload.ok === false) throw new Error(payload.message || payload.error || `HTTP ${response.status}`);
      return payload;
    } finally { clearTimeout(timeout); this.controllers.delete(controller); }
  }
  connect() {
    if (!this.enabled) return;
    clearTimeout(this.retryTimer); this.stream?.destroy();
    const generation = ++this.generation;
    let ended = false;
    const disconnect = (error) => {
      if (ended || generation !== this.generation) return;
      ended = true; this.stream?.destroy(); this.stream = null; this.state = null;
      this.changes.emit('state');
      void this.lumia.updateConnection(false).catch(() => {});
	  for (const name of ['stream_status','recording_status','twitch_status','kick_status','youtube_status'])
	    void this.setVariable(name, 'DISCONNECTED').catch(() => {});
      void this.setVariable('last_result', error?.message || 'Pulse Weaver disconnected.').catch(() => {});
      if (this.enabled && this.retry < 8) {
        const delay = Math.min(30000, 1000 * 2 ** this.retry++);
        this.retryTimer = setTimeout(() => this.connect(), delay);
      }
    };
    try {
      const request = http.get(this.endpoint('/events'), { headers: this.headers() }, response => {
        if (response.statusCode !== 200) {
          response.resume();
          disconnect(new Error(response.statusCode === 404 ? 'Install Pulse Weaver 1.11.42 or newer for these controls and alerts.' : `Connection failed: HTTP ${response.statusCode}`));
          return;
        }
        let buffer = '';
        response.setEncoding('utf8');
        response.on('data', chunk => {
          buffer += chunk;
          if (buffer.length > 1024 * 1024) return disconnect(new Error('Event stream exceeded its size limit.'));
          let boundary;
          while ((boundary = buffer.indexOf('\n\n')) >= 0) {
            const frame = buffer.slice(0, boundary); buffer = buffer.slice(boundary + 2);
            const line = frame.split('\n').find(value => value.startsWith('data: '));
            if (!line) continue;
            let payload;
            try { payload = JSON.parse(line.slice(6)); } catch { disconnect(new Error('Invalid event data.')); return; }
            if (++this.pendingEvents > 256) { --this.pendingEvents; disconnect(new Error('Event consumer fell behind; reconnecting.')); return; }
            this.eventQueue = this.eventQueue.then(async () => {
              if (generation === this.generation && !ended) await this.consume(payload);
            }).catch(disconnect).finally(() => { --this.pendingEvents; });
          }
        });
        response.on('end', () => disconnect(new Error('Pulse Weaver closed the connection.')));
        response.on('error', disconnect);
      });
      this.stream = request;
      request.setTimeout(45000, () => disconnect(new Error('Pulse Weaver connection timed out.')));
      request.on('error', disconnect);
    } catch (error) { disconnect(error); }
  }
  async setVariable(name, value) {
    if (this.values.get(name) === value) return;
    await this.lumia.setVariable(name, value); this.values.set(name, value);
  }
  async setOptions(actionType, fieldKey, options) {
    options.sort((a,b) => a.label.localeCompare(b.label));
    const key = `${actionType}/${fieldKey}`, serialized = JSON.stringify(options);
    if (this.options.get(key) === serialized) return;
    await this.lumia.updateActionFieldOptions({ actionType, fieldKey, options });
    this.options.set(key, serialized);
  }
  outputStatus(platform) {
    const outputs = Object.values(this.state?.outputs || {}).filter(value => value.platform === platform);
    if (!outputs.length) return 'stopped';
    if (outputs.some(value => value.state === 'failed')) return 'failed';
    if (outputs.some(value => value.state === 'reconnecting')) return 'reconnecting';
    if (outputs.some(value => value.state === 'stopping')) return 'stopping';
    if (outputs.some(value => value.state === 'starting')) return 'starting';
    if (outputs.some(value => value.state === 'live')) {
      const dual = platform === 'youtube' && this.state?.destinations?.youtube === 'dual';
      return dual && outputs.filter(value => value.state === 'live').length < 2 ? 'starting' : 'live';
    }
    return 'stopped';
  }
  async updateVariables() {
    if (!this.state) return;
    const updates = ['twitch','kick','youtube'].map(platform => this.setVariable(`${platform}_status`, this.outputStatus(platform).toUpperCase()));
    updates.push(this.setVariable('active_stage', this.state.activeStage || ''));
    const live = Object.values(this.state.outputs || {}).some(output => output.platform !== 'recording' && output.state === 'live');
    updates.push(this.setVariable('stream_status', live ? 'LIVE' : 'OFF'));
    updates.push(this.setVariable('recording_status', this.outputStatus('recording') === 'live' ? 'RECORDING' : this.outputStatus('recording').toUpperCase()));
    await Promise.all(updates);
  }
  async refreshOptions() {
    const sources = this.state?.sources || [];
    const sourceOptions = sources.filter(source => source.audio).map(source => ({ label: source.name, value: source.id }));
    const mediaOptions = sources.filter(source => source.media).map(source => ({ label: source.name, value: source.id }));
    const items = sources.flatMap(scene => (scene.items || []).map(item => ({ label: `${scene.name} / ${item.name} [${item.itemId}]`, value: JSON.stringify({ source: scene.id, item: item.itemId }) })));
    await Promise.all([
      this.setOptions('select_stage', 'stage', (this.state?.stages || []).map(stage => ({ label: stage, value: stage }))),
      this.setOptions('source_visibility', 'target', items),
      this.setOptions('source_mute', 'source', sourceOptions),
      this.setOptions('source_volume', 'source', sourceOptions),
      this.setOptions('media_control', 'source', mediaOptions)
    ]);
  }
  async consume(payload) {
    if (payload.kind === 'snapshot' || payload.kind === 'catalogue') {
      if (payload.state?.operatorApi < 2 || !payload.state?.operatorApi) throw new Error('Pulse Weaver 1.11.42 or newer is required.');
      this.state = payload.state; this.retry = 0;
      await this.lumia.updateConnection(true);
      await this.updateVariables(); await this.refreshOptions(); this.changes.emit('state');
      return; // A snapshot never replays old alerts on reconnect.
    }
    if (payload.kind !== 'event' || !this.state) return;
    let alert = payload.event;
    if (alert === 'stage_changed') this.state.activeStage = payload.stage || '';
    if (alert === 'destination_state' || alert === 'recording_state') {
      this.state.outputs ||= {};
      this.state.outputs[payload.output || payload.platform] = payload;
      alert = payload.platform === 'recording' ? `recording_${payload.state}` : `${payload.platform}_${payload.state}`;
    }
    await this.updateVariables(); this.changes.emit('state');
    const known = new Set((this.manifest.config.alerts || []).map(value => value.key));
    if (known.has(alert)) await this.lumia.triggerAlert({ alert, showInEventList: false, extraSettings: {
      platform: payload.platform || '', output: payload.output || '', state: payload.state || '',
      stage: payload.stage || this.state.activeStage || '', source: payload.source || '', sourceId: payload.sourceId || '',
      scene: payload.scene || '', itemId: payload.itemId ?? '', volume: payload.volume ?? '', message: payload.message || ''
    }});
  }
  async ensureState() {
    if (!this.state) {
      const state = await this.request('/state');
      if (!(state.operatorApi >= 2)) throw new Error('Install Pulse Weaver 1.11.42 or newer first.');
      this.state = state;
      if (!this.stream && this.enabled) { this.retry = 0; this.connect(); }
    }
    return this.state;
  }
  async waitForOutput(platform, start, timeoutMs = 20000, attempt) {
    const check = () => {
	  if (attempt !== undefined && this.operations.get(platform) !== attempt)
	    throw new Error(`${platform}: operation superseded by a newer command.`);
      if (!this.state) throw new Error('Pulse Weaver disconnected before the result could be confirmed.');
      const status = this.outputStatus(platform);
      if (start && status === 'failed') {
        const failed = Object.values(this.state.outputs || {}).find(value => value.platform === platform && value.state === 'failed');
        throw new Error(`${platform} failed: ${failed?.message || 'check Pulse Weaver for details'}`);
      }
      return status === (start ? 'live' : 'stopped');
    };
    if (check()) return;
    await new Promise((resolve, reject) => {
      const changed = () => { try { if (check()) finish(); } catch (error) { finish(error); } };
      const finish = error => { clearTimeout(timer); this.changes.off('state', changed); error ? reject(error) : resolve(); };
      const timer = setTimeout(() => finish(new Error(`${platform}: ${start ? 'live' : 'stopped'} state was not confirmed within 20 seconds. Check Pulse Weaver before retrying.`)), timeoutMs);
      this.changes.on('state', changed); changed();
    });
  }
  async destination(platform, start) {
    if (!['twitch','kick','youtube'].includes(platform)) throw new Error('Choose Twitch, Kick or YouTube.');
    await this.ensureState();
    if (start && (!this.state.destinations?.[platform] || this.state.destinations[platform] === 'off')) throw new Error(`Configure ${platform}'s output mode inside Pulse Weaver first.`);
	const attempt = (this.operations.get(platform) || 0) + 1;
	this.operations.set(platform, attempt); this.changes.emit('state');
    const result = await this.request(`/destination/${start ? 'start' : 'stop'}?platform=${platform}`, 'POST');
    // Refresh once after the command; subsequent confirmation uses pushed events.
    this.state = await this.request('/state');
    await this.waitForOutput(platform, start, 20000, attempt);
    return { ...result, message: `${platform} ${start ? 'is live' : 'is stopped'}.` };
  }
  async runAction(action) {
    const params = action.value || {};
    if (action.type === 'reconnect') { this.retry = 0; this.connect(); return { message: 'Connection requested.' }; }
    await this.ensureState();
    switch (action.type) {
      case 'start_platform': ++this.showAttempt; return this.destination(String(params.platform), true);
      case 'stop_platform': return this.destination(String(params.platform), false);
      case 'go_live': {
	    const showAttempt = ++this.showAttempt;
        const providers = ['twitch','kick','youtube'].filter(platform => this.state.destinations?.[platform] && this.state.destinations[platform] !== 'off');
        if (!providers.length) throw new Error('Choose your show destinations inside Pulse Weaver first.');
        const errors = [];
        for (const platform of providers) {
	      if (showAttempt !== this.showAttempt) throw new Error('Show start cancelled by End Show.');
	      try { await this.destination(platform, true); } catch (error) { errors.push(error.message); }
	    }
        if (errors.length) throw new Error(`Show start incomplete. ${errors.join(' ')} Other destinations may be live.`);
        return { message: 'All configured show destinations are live.' };
      }
      case 'end_stream': {
	    const stopAttempt = ++this.showAttempt;
        const errors = [];
        for (const platform of ['twitch','kick','youtube']) {
          if (stopAttempt !== this.showAttempt) throw new Error('Show stop superseded by a newer show command.');
          try { await this.destination(platform, false); } catch (error) { errors.push(error.message); }
        }
        if (errors.length) throw new Error(`Show stop incomplete. ${errors.join(' ')}`);
	    // Private builds through v50 route this last cleanup through a UI toggle
	    // using a one-second cached live flag. Only clear the show session after
	    // that flag is explicitly false, or leave it alone if it stays stale.
	    const deadline = Date.now() + 5000;
	    while (stopAttempt === this.showAttempt) {
	      const snapshot = await this.request('/state');
	      this.state = snapshot;
	      if (snapshot.live === false) {
	        if (stopAttempt === this.showAttempt) await this.request('/end-stream', 'POST');
	        break;
	      }
	      if (Date.now() >= deadline) throw new Error('Outputs stopped, but Pulse Weaver has not refreshed its show status. Cleanup was skipped to avoid restarting the show.');
	      await new Promise(resolve => setTimeout(resolve, 100));
	    }
        if (stopAttempt !== this.showAttempt) throw new Error('Show stop superseded by a newer show command.');
        return { message: 'All show destinations are stopped.' };
      }
      case 'select_stage': return this.request(`/stage?name=${encodeURIComponent(String(params.stage || ''))}`, 'POST');
      case 'next_stage': return this.request('/stage/next', 'POST');
      case 'previous_stage': return this.request('/stage/previous', 'POST');
      case 'start_recording': case 'stop_recording': {
        const start = action.type === 'start_recording';
        await this.request(`/record/${start ? 'start' : 'stop'}`, 'POST');
        this.state = await this.request('/state'); await this.waitForOutput('recording', start);
        return { message: start ? 'Recording started.' : 'Recording stopped.' };
      }
      case 'source_visibility': {
        let target; try { target = JSON.parse(params.target); } catch { throw new Error('Choose an existing scene item.'); }
        if (!['show','hide','toggle_visibility'].includes(params.operation)) throw new Error('Invalid visibility operation.');
        return this.request('/source?' + new URLSearchParams({ action: params.operation, source: target.source, item: target.item }), 'POST');
      }
      case 'source_mute': {
        if (!['mute','unmute','toggle_mute'].includes(params.operation)) throw new Error('Invalid audio operation.');
        return this.request('/source?' + new URLSearchParams({ action: params.operation, source: params.source }), 'POST');
      }
      case 'source_volume': return this.request('/source?' + new URLSearchParams({ action: 'volume', source: params.source, volume: params.volume }), 'POST');
      case 'media_control': {
        if (!['play','pause','restart','stop_media','next_media','previous_media'].includes(params.operation)) throw new Error('Invalid playback operation.');
        return this.request('/source?' + new URLSearchParams({ action: params.operation, source: params.source }), 'POST');
      }
      default: throw new Error(`Unsupported operation: ${action.type}`);
    }
  }
  async actions(config) {
    const execute = async () => {
      try {
        let last;
        for (const action of config.actions || []) last = await this.runAction(action);
        const message = last?.message || 'Operation completed.';
        await this.updateVariables(); await this.setVariable('last_result', message);
        return { newlyPassedVariables: { pulseweavercontrol_result: message, pulseweavercontrol_active_stage: this.state?.activeStage || '' }, shouldStop: false };
      } catch (error) {
        const message = error.message || String(error);
        await this.setVariable('last_result', message);
        await this.lumia.showToast({ type: 'error', message: `Pulse Weaver: ${message}` });
        return { newlyPassedVariables: { pulseweavercontrol_result: message }, shouldStop: true };
      }
    };
	// Stop controls must not sit behind a start waiting for a network handshake.
	if ((config.actions || []).length && config.actions.every(action => ['end_stream','stop_platform','stop_recording'].includes(action.type))) return execute();
    const queued = this.actionQueue.then(execute, execute);
    this.actionQueue = queued.catch(() => {}); return queued;
  }
}
module.exports = PulseWeaverPlugin;
