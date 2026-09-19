#include "pulse-overlay-renderer.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace PulseOverlay {
namespace {
QString scriptValue(const QJsonValue &value)
{
	QByteArray encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
	// JSON escaping alone does not protect the HTML script raw-text boundary.
	return QString::fromUtf8(encoded.mid(1, encoded.size() - 2)).replace('<', "\\u003c")
		.replace(QChar(0x2028), "\\u2028").replace(QChar(0x2029), "\\u2029");
}
}

QString renderBootstrap()
{
	return QStringLiteral(R"HTML(<!doctype html><meta charset="utf-8"><meta name="referrer" content="no-referrer">
<style>html,body{margin:0;background:transparent;color:#dbeafe;font:16px Segoe UI}.error{display:none;padding:24px}</style>
<div class="error" id="error">This Pulse Weaver browser source needs reconnecting from Camera → Overlay Designer.</div><script>
(async()=>{const token=new URLSearchParams(location.hash.slice(1)).get('token')||'';if(!token)throw Error('Missing capability');const r=await fetch(location.pathname+'/render'+location.search,{headers:{Authorization:'Bearer '+token},cache:'no-store'});if(!r.ok)throw Error('Render unavailable');const frame=document.createElement('iframe');frame.title='Pulse Weaver overlay renderer';frame.referrerPolicy='no-referrer';frame.style.cssText='position:fixed;inset:0;width:100%;height:100%;border:0;background:transparent';frame.srcdoc=await r.text();document.body.replaceChildren(frame)})().catch(()=>document.getElementById('error').style.display='block');
</script>)HTML");
}

QString renderPage(const QJsonObject &document, const QString &layout, const QString &eventPath)
{
	const QUrl imported(document.value("externalUrl").toString());
	if (imported.isValid() && (imported.scheme() == "https" || imported.scheme() == "http")) {
		// Imported sites run at their own origin without inheriting a render capability.
		return "<!doctype html><meta name='referrer' content='no-referrer'><script>location.replace(" +
			scriptValue(imported.toString()) + ")</script>";
	}

	QString body;
	for (const QJsonValue &value : document.value("elements").toArray()) {
		const QJsonObject e = value.toObject();
		if (!e.value("visible").toBool(true)) continue;
		const QString type = e.value("type").toString("text");
		const QString style = QString("left:%1px;top:%2px;width:%3px;height:%4px;transform:rotate(%5deg);opacity:%6;color:%7;background:%8;font-size:%9px;")
			.arg(e.value("x").toDouble()).arg(e.value("y").toDouble())
			.arg(e.value("width").toDouble(720)).arg(e.value("height").toDouble(120))
			.arg(e.value("rotation").toDouble()).arg(e.value("opacity").toDouble(1))
			.arg(e.value("color").toString("#ffffff"), type == "text" ? "transparent" : e.value("background").toString("transparent"))
			.arg(e.value("fontSize").toDouble(48));
		const QString attributes = QString(" class='pw e %1' data-bind='%2' style='%3'")
			.arg(type.toHtmlEscaped(), e.value("binding").toString().toHtmlEscaped(), style.toHtmlEscaped());
		if (type == "image" || type == "video")
			body += QString("<%1%2 src='%3'%4></%1>").arg(type == "image" ? "img" : "video", attributes,
				e.value("asset").toString().toHtmlEscaped(), type == "video" ? " autoplay loop muted" : "");
		else
			body += "<div" + attributes + ">" + (type == "progress" ? "<div class='fill'></div>" : e.value("text").toString().toHtmlEscaped()) + "</div>";
	}

	// Every authored surface shares one isolated document. Legacy code can still
	// access #stage, visual elements, apply(), es and EventSource message events.
	// Only the fixed parent shell knows the host capability or opens the stream.
	const QString child = QStringLiteral(R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src https: data:; media-src https: data:; font-src https: data:; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'none'; base-uri 'none'; form-action 'none'">
<style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent}.viewport{position:absolute;transform-origin:top left}.stage{position:relative}.e{position:absolute;box-sizing:border-box;display:flex;align-items:center;justify-content:center;text-align:center}.shape{border:2px solid color-mix(in srgb,var(--accent,#22d3ee) 45%,transparent);border-radius:24px}.progress{padding:8px;border-radius:999px}.fill{height:100%;width:0;background:var(--accent,#22d3ee);border-radius:999px}.stage.active{animation:pw-in .38s ease-out both}.stage.out{animation:pw-out .35s ease-in both}img:empty{display:none}
</style></head><body><div class="viewport"><div id="stage" class="stage">%1</div></div>
<script>
const config=%2,stage=document.getElementById('stage'),viewport=document.querySelector('.viewport');
stage.style.cssText='width:'+config.width+'px;height:'+config.height+'px;opacity:'+(config.eventDriven?0:1);
document.body.style.fontFamily=config.fontFamily||'Segoe UI,Arial,sans-serif';
const enter={fade:'opacity:0',zoom:'opacity:0;transform:scale(.72)','slide-left':'opacity:0;transform:translateX(-90px)','slide-up':'opacity:0;transform:translateY(70px)'};
const exit={fade:'opacity:0',zoom:'opacity:0;transform:scale(.82)','slide-left':'opacity:0;transform:translateX(-90px)','slide-up':'opacity:0;transform:translateY(-70px)'};
const styles=document.createElement('style');styles.textContent='@keyframes pw-in{from{'+(enter[config.enterAnimation]||enter['slide-up'])+'}to{opacity:1;transform:none}}@keyframes pw-out{to{'+(exit[config.exitAnimation]||exit.fade)+'}}'+config.customCss;document.head.append(styles);
function fitViewport(){const s=Math.min(innerWidth/config.width,innerHeight/config.height);viewport.style.cssText='width:'+config.width+'px;height:'+config.height+'px;left:'+Math.max(0,(innerWidth-config.width*s)/2)+'px;top:'+Math.max(0,config.portrait?innerHeight-config.height*s-100:(innerHeight-config.height*s)/2)+'px;transform:scale('+s+')'}addEventListener('resize',fitViewport);fitViewport();
let hideTimer,lastAudio,port,muted=false;const get=(o,p)=>p.split('.').reduce((v,k)=>v&&v[k],o);
function stopAudio(){if(lastAudio){lastAudio.pause();lastAudio.removeAttribute('src');lastAudio.load();lastAudio=null}if(window.speechSynthesis)speechSynthesis.cancel()}
function finish(){clearTimeout(hideTimer);stopAudio();stage.classList.add('out')}
function apply(d){if(d.__pwControl==='mute'){muted=!!d.muted;if(muted)stopAudio();return}if(d.__pwControl==='skip'||d.__pwControl==='complete'){finish();return}
stopAudio();clearTimeout(hideTimer);document.querySelectorAll('[data-bind]').forEach(el=>{const key=el.dataset.bind;if(!key)return;const v=get(d,key)??get(d,'event.'+key)??d[key];if(v===undefined)return;if(el.classList.contains('progress'))el.querySelector('.fill').style.width=Math.max(0,Math.min(100,Number(v)))+'%';else if(el.tagName==='IMG'||el.tagName==='VIDEO'){el.src=v||'';el.style.display=v?'':'none'}else el.textContent=v});
const a=d.__pwAlert;if(a){document.documentElement.style.setProperty('--accent',a.color||'#22d3ee');document.querySelectorAll('[data-bind="__pwAlert.headline"]').forEach(el=>el.style.color=a.color||'#22d3ee');if(a.sound&&!a.muted&&!muted){lastAudio=new Audio(a.sound);lastAudio.play().catch(()=>{})}if(a.tts&&!a.muted&&!muted&&a.message&&window.speechSynthesis)speechSynthesis.speak(new SpeechSynthesisUtterance(a.message))}
stage.classList.remove('out','active');void stage.offsetWidth;stage.classList.add('active');stage.style.opacity=1;if(config.eventDriven)hideTimer=setTimeout(finish,Math.max(0,a?.remainingMs??config.durationMs))}
// Compatibility EventSource is an in-document event adapter, never a network API.
const eventSources=new Set();class OverlayEventSource extends EventTarget{constructor(url){super();this.url=String(url);this.readyState=1;eventSources.add(this);queueMicrotask(()=>{const e=new Event('open');this.dispatchEvent(e);this.onopen?.(e)})}close(){this.readyState=2;eventSources.delete(this)}}
OverlayEventSource.CONNECTING=0;OverlayEventSource.OPEN=1;OverlayEventSource.CLOSED=2;window.EventSource=OverlayEventSource;
const es=new EventSource(config.legacyEventPath);es.onmessage=e=>apply(JSON.parse(e.data).data||{});
addEventListener('message',function connect(e){if(e.source!==parent||e.data?.type!=='pw-connect'||!e.ports[0]||port)return;port=e.ports[0];port.onmessage=({data})=>{if(!data||typeof data!=='object')return;if(data.data?.__pwControl){apply(data.data);return}for(const source of eventSources){const event=new MessageEvent('message',{data:JSON.stringify(data)});source.dispatchEvent(event);source.onmessage?.(event)}dispatchEvent(new CustomEvent('pulseweaver:event',{detail:data}))};port.start();port.postMessage({type:'ready'})});
addEventListener('pagehide',stopAudio);
</script>%3<script>%4</script></body></html>)HTML")
		.arg(body + document.value("customHtml").toString(), scriptValue(QJsonObject{
			{"width", document.value("width").toDouble(1920)}, {"height", document.value("height").toDouble(1080)},
			{"durationMs", document.value("durationMs").toDouble(6000)}, {"eventDriven", document.value("eventDriven").toBool()},
			{"fontFamily", document.value("fontFamily").toString("Segoe UI")}, {"portrait", layout == "portrait"},
			{"customCss", document.value("customCss").toString()}, {"enterAnimation", document.value("enterAnimation")},
			{"exitAnimation", document.value("exitAnimation")}, {"legacyEventPath", "/overlay/" + document.value("id").toString() + "/events"}}),
			QString(),
			// A separate script retains legacy global scope. Escape the HTML end tag
			// without changing string literal values or requiring unsafe-eval.
			QString(document.value("customJs").toString()).replace("</script", "<\\/script", Qt::CaseInsensitive));

	QString streamPath = eventPath.isEmpty() ? "/overlay/" + document.value("id").toString() + "/events" : eventPath;
	streamPath += "?layout=" + layout;
	return QStringLiteral(R"HTML(<!doctype html><meta charset="utf-8"><meta name="referrer" content="no-referrer">
<style>html,body{margin:0;width:100%;height:100%;overflow:hidden;background:%1}iframe{position:fixed;inset:0;width:100%;height:100%;border:0}</style>
<iframe id="content" title="Overlay content" sandbox="allow-scripts" referrerpolicy="no-referrer"></iframe><script>
const content=document.getElementById('content');let bridge,ready=false,waiting=[],stopped=false;
function deliver(payload){if(payload.data?.__pwControl==='reload'){stopped=true;if(frameElement?.hasAttribute('srcdoc'))parent.location.reload();else location.reload();return}if(ready)bridge.postMessage(payload);else if(waiting.length<128)waiting.push(payload)}
content.onload=()=>{ready=false;bridge?.close();const channel=new MessageChannel();bridge=channel.port1;bridge.onmessage=({data})=>{if(data?.type!=='ready')return;ready=true;for(const p of waiting)bridge.postMessage(p);waiting=[]};bridge.start();content.contentWindow.postMessage({type:'pw-connect'},'*',[channel.port2])};content.srcdoc=%2;
const token=new URLSearchParams((location.hash||parent.location.hash).slice(1)).get('token')||'';
async function connect(){try{const r=await fetch(%3,{headers:{Authorization:'Bearer '+token},cache:'no-store'});if(!r.ok)throw Error('Event stream '+r.status);const rd=r.body.getReader(),dec=new TextDecoder();let buf='';for(;;){const n=await rd.read();if(n.done)break;buf+=dec.decode(n.value,{stream:true});if(buf.length>1048576)throw Error('Oversized stream');let cut;while((cut=buf.indexOf('\n\n'))>=0){const block=buf.slice(0,cut);buf=buf.slice(cut+2);for(const line of block.split('\n'))if(line.startsWith('data: '))deliver(JSON.parse(line.slice(6)))}}}catch(e){console.error(e)}if(!stopped)setTimeout(connect,1000)}connect();
</script>)HTML").arg(eventPath.isEmpty() ? "transparent" : "#05080d", scriptValue(child), scriptValue(streamPath));
}
} // namespace PulseOverlay
