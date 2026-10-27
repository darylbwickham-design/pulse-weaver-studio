#pragma once

#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <chrono>
#include <thread>

static QImage reflowScenePixels(obs_scene_t *scene, uint32_t width=640, uint32_t height=480)
{
	// Scene-item cropped textures are cached until the native video tick. Each
	// sample must advance a frame before comparing changed crop/bounds geometry.
	const uint64_t before=obs_get_video_frame_time();
	for(int n=0;n<100&&obs_get_video_frame_time()==before;++n)
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	QImage image(width, height, QImage::Format_RGBA8888);
	obs_enter_graphics();
	gs_texrender_t *texture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	check(texture && gs_texrender_begin(texture, width, height), "reflow pixel surface starts");
	vec4 clear{}; gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
	gs_ortho(0, float(width), 0, float(height), -100, 100);
	obs_source_video_render(obs_scene_get_source(scene));
	gs_texrender_end(texture);
	gs_stagesurf_t *stage = gs_stagesurface_create(width, height, GS_RGBA);
	check(stage != nullptr, "reflow staging surface starts");
	gs_stage_texture(stage, gs_texrender_get_texture(texture));
	uint8_t *bytes = nullptr; uint32_t stride = 0;
	check(gs_stagesurface_map(stage, &bytes, &stride), "reflow pixels readable");
	for (uint32_t y = 0; y < height; ++y)
		std::memcpy(image.scanLine(y), bytes + y * stride, width * 4);
	gs_stagesurface_unmap(stage); gs_stagesurface_destroy(stage); gs_texrender_destroy(texture);
	obs_leave_graphics();
	return image;
}

static int reflowPixelDifferences(const QImage &left, const QImage &right)
{
	check(left.size() == right.size(), "reflow pixel comparison size");
	int different = 0;
	for (int y = 0; y < left.height(); ++y) for (int x = 0; x < left.width(); ++x) {
		const QRgb a = left.pixel(x, y), b = right.pixel(x, y);
		if (std::abs(qRed(a)-qRed(b)) > 2 || std::abs(qGreen(a)-qGreen(b)) > 2 ||
		    std::abs(qBlue(a)-qBlue(b)) > 2 || std::abs(qAlpha(a)-qAlpha(b)) > 2) ++different;
	}
	return different;
}

static void reflowMotionTests()
{
	std::fprintf(stderr,"Reflow: native fixture\n");std::fflush(stderr);
	static bool registered = false;
	if (!registered) {
		obs_source_info pattern{}; pattern.id = "reflow_audit_pattern"; pattern.type = OBS_SOURCE_TYPE_INPUT;
		pattern.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW;
		pattern.get_name = [](void *) { return "Reflow coloured geometry fixture"; };
		pattern.create = [](obs_data_t *settings, obs_source_t *) -> void * { return new int(int(obs_data_get_int(settings,"variant"))); };
		pattern.destroy = [](void *value) { delete static_cast<int *>(value); };
		pattern.get_width = [](void *) -> uint32_t { return 640; };
		pattern.get_height = [](void *) -> uint32_t { return 360; };
		pattern.video_render = [](void *opaque, gs_effect_t *) {
			gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
			for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x) {
				vec4 colour{}; colour.x = x & 1 ? 1.0f : 0.15f; colour.y = y ? 0.85f : 0.1f;
				colour.z = x & 2 ? 0.9f : 0.1f; colour.w = 1;
				if(*static_cast<int *>(opaque)==1){colour.x=1;colour.y=0;colour.z=1;}
				gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &colour);
				gs_matrix_push(); gs_matrix_translate3f(float(x*160), float(y*180), 0);
				while (gs_effect_loop(effect, "Solid")) gs_draw_sprite(nullptr, 0, 160, 180);
				gs_matrix_pop();
			}
		};
		obs_register_source(&pattern); registered = true;
	}
	OBSSceneAutoRelease scene = obs_scene_create_private("Reflow native pixel fixture");
	OBSSourceAutoRelease source = obs_source_create_private("reflow_audit_pattern", "Reflow camera", nullptr);
	obs_source_set_audio_mixers(source, 5); obs_source_set_muted(source, false);
	OBSSceneItem item = obs_scene_add(scene, source);
	QTemporaryDir storage; PulseMotionEngine engine(nullptr,storage.filePath("motion.json"));
	const QJsonObject inputSettings = QJsonDocument::fromJson(obs_data_get_json(OBSDataAutoRelease(obs_source_get_settings(source)))).object();
	QJsonObject raw{{"positionX",65.0},{"positionY",100.0},{"scaleX",0.42},{"scaleY",0.43},
		{"boundsWidth",141.5},{"boundsHeight",267.5},{"boundsType",0},{"alignment",5},{"boundsAlignment",0},
		{"rotation",0},{"cropLeft",11},{"cropTop",7},{"cropRight",13},{"cropBottom",9},
		{"cropToBounds",false},{"visible",true},{"locked",true},{"order",0}};
	raw=PulseMotionEngineTestAccess::canonicalTransform(raw);
	for (int mode : {0,1,2,3,4,5,6}) for (bool flipped : {false,true}) for (int alignment : {0,5,10}) {
		std::fprintf(stderr,"Reflow: bounds %d flip%d align%d\n",mode,int(flipped),alignment);std::fflush(stderr);
		auto endpoint = raw; endpoint.insert("boundsType",mode); endpoint.insert("alignment",alignment);
		endpoint.insert("boundsAlignment",alignment); endpoint.insert("rotation",flipped ? 13 : 0);
		endpoint.insert("cropToBounds",true); endpoint.insert("scaleX",flipped ? -0.42 : 0.42);
		PulseMotionEngineTestAccess::applyTransform(item, endpoint); obs_sceneitem_force_update_transform(item);
		matrix4 native{}; obs_sceneitem_get_draw_transform(item, &native);
		const auto nativePixels = reflowScenePixels(scene);
		if (mode == 0 && !flipped && alignment == 5)
			check(qAlpha(nativePixels.pixel(100,130)) > 0, "reflow pixel fixture actually renders its coloured source");
		const auto normalized = PulseMotionEngineTestAccess::visualTransform(endpoint,640,360);
		check(normalized.value("boundsType").toInt() == OBS_BOUNDS_NONE, "reflow resolves every native bounds mode");
		PulseMotionEngineTestAccess::applyTransform(item, normalized); obs_sceneitem_force_update_transform(item);
		matrix4 resolved{}; obs_sceneitem_get_draw_transform(item, &resolved);
		const float *a = reinterpret_cast<const float *>(&native), *b = reinterpret_cast<const float *>(&resolved);
		for (int n = 0; n < 16; ++n) {
			// Relative-coordinate OBS items nudge newly assigned positions to .5 px.
			const float tolerance=(n==12||n==13) ? .26f : .002f;
			if(std::abs(a[n]-b[n])>=tolerance)std::fprintf(stderr,"Reflow matrix mismatch index%d native%g normalized%g normPos%g,%g\n",n,a[n],b[n],normalized.value("positionX").toDouble(),normalized.value("positionY").toDouble());
			check(std::abs(a[n]-b[n]) < tolerance, "resolved camera transform matches native draw geometry within native half-pixel positioning");
		}
		const int differences = reflowPixelDifferences(nativePixels,reflowScenePixels(scene));
		if (differences > 960) std::fprintf(stderr,"Reflow pixel difference mode=%d flipped=%d alignment=%d pixels=%d\n",mode,int(flipped),alignment,differences);
		check(differences <= 960, "resolved bounds crop and alignment preserve endpoint pixels within native subpixel rounding");
	}
	// Mixed modes formerly retained the old interpretation until the final frame.
	for (int fromMode : {0,3,4}) for (int toMode : {0,3,4}) {
		auto from = raw, to = raw; from.insert("boundsType",fromMode); to.insert("boundsType",toMode);
		from.insert("positionX",20); from.insert("positionY",15); from.insert("cropToBounds",true);
		to.insert("positionX",320); to.insert("positionY",170); to.insert("boundsWidth",240); to.insert("boundsHeight",180);
		to.insert("scaleX",0.36); to.insert("scaleY",0.35); to.insert("cropToBounds",true);
		from=PulseMotionEngineTestAccess::canonicalTransform(from);to=PulseMotionEngineTestAccess::canonicalTransform(to);
		const auto end = PulseMotionEngineTestAccess::reflowFrame(item,from,to,1.0,640,480);
		check(end == to, "reflow restores every exact saved endpoint field");
		check(PulseMotionEngineTestAccess::reflowFrame(item,from,to,0.0,640,480) == from, "continuous reflow starts at exact saved state");
		PulseMotionEngineTestAccess::applyTransform(item,to); obs_sceneitem_force_update_transform(item);
		const auto endpointPixels = reflowScenePixels(scene);
		PulseMotionEngineTestAccess::movementFrame(item,from,to,0.9999,true); obs_sceneitem_force_update_transform(item);
		check(reflowPixelDifferences(endpointPixels,reflowScenePixels(scene)) <= 320, "mixed bounds modes approach their final pixels without a last-frame size snap");
		PulseMotionEngineTestAccess::movementFrame(item,from,to,1.0,true);
		check(PulseMotionEngineTestAccess::frame(item) == to, "runtime reflow restores crop, bounds, lock and ordering exactly");
	}
	for (const QSize canvas : {QSize(640,480),QSize(1080,1920)}) {
		std::vector<QPointF> entrances;
		for (const QPointF point : {QPointF(20,20),QPointF(canvas.width()-180,20),QPointF(20,canvas.height()-140)}) {
			auto hidden = raw, visible = raw; hidden.insert("visible",false);
			visible.insert("positionX",point.x()); visible.insert("positionY",point.y()); visible.insert("scaleX",0.25); visible.insert("scaleY",0.25);
			const auto start = PulseMotionEngineTestAccess::reflowFrame(item,hidden,visible,0,canvas.width(),canvas.height());
			entrances.push_back({start.value("positionX").toDouble(),start.value("positionY").toDouble()});
			const double x=start.value("positionX").toDouble(),y=start.value("positionY").toDouble();
			const double w=640*std::abs(start.value("scaleX").toDouble()),h=360*std::abs(start.value("scaleY").toDouble());
			check(x+w < 0 || y+h < 0 || x > canvas.width() || y > canvas.height(), "incoming camera begins completely outside its own canvas");
			check(PulseMotionEngineTestAccess::reflowFrame(item,visible,hidden,1,canvas.width(),canvas.height()) == hidden,
				"exiting camera ends hidden instead of remaining active offscreen");
			const auto middle=PulseMotionEngineTestAccess::reflowFrame(item,hidden,visible,.5,canvas.width(),canvas.height());
			check(middle.value("scaleX") != visible.value("scaleX"), "incoming camera resizes as it joins its saved layout");
		}
		check(entrances[0] != entrances[1] && entrances[1] != entrances[2], "camera destinations produce varied entrance paths in landscape and portrait");
		for(bool flipX:{false,true})for(bool flipY:{false,true})for(float angle:{0.0f,17.0f}) {
			auto hidden=raw,visible=raw;hidden.insert("visible",false);visible.insert("positionX",canvas.width()-180);visible.insert("positionY",20);
			visible.insert("scaleX",flipX ? -.25 : .25);visible.insert("scaleY",flipY ? -.25 : .25);visible.insert("rotation",angle);
			const auto start=PulseMotionEngineTestAccess::reflowFrame(item,hidden,visible,0,canvas.width(),canvas.height());
			matrix4 rotation{};matrix4_identity(&rotation);matrix4_rotate_aa4f(&rotation,&rotation,0,0,1,RAD(angle));
			const float w=(640-start.value("cropLeft").toInt()-start.value("cropRight").toInt())*float(start.value("scaleX").toDouble());
			const float h=(360-start.value("cropTop").toInt()-start.value("cropBottom").toInt())*float(start.value("scaleY").toDouble());
			const float x=float(start.value("positionX").toDouble()),y=float(start.value("positionY").toDouble());
			const float xs[]{x,x+w*rotation.x.x,x+h*rotation.y.x,x+w*rotation.x.x+h*rotation.y.x};
			const float ys[]{y,y+w*rotation.x.y,y+h*rotation.y.y,y+w*rotation.x.y+h*rotation.y.y};
			check(*std::max_element(xs,xs+4)<0||*std::max_element(ys,ys+4)<0||*std::min_element(xs,xs+4)>canvas.width()||*std::min_element(ys,ys+4)>canvas.height(),
				"flipped and rotated incoming cameras start entirely outside the canvas");
		}
	}
	const auto unchanged = PulseMotionEngineTestAccess::reflowFrame(item,raw,raw,.43,640,480);
	check(unchanged == raw, "stationary overlays retain their exact transform throughout reflow");
	auto hidden=raw;hidden.insert("visible",false);
	PulseMotionEngineTestAccess::movementFrame(item,hidden,raw,0.0,false);
	check(std::abs(PulseMotionEngineTestAccess::frame(item).value("positionX").toDouble() - (raw.value("positionX").toDouble()-640)) < .01,
		"legacy profile retains its existing left entrance");
	// Repeated native samples must retain the common covered region while the
	// views exchange roles; the magenta alert strip must remain above them.
	OBSSourceAutoRelease other=obs_source_create_private("reflow_audit_pattern","Second reflow camera",nullptr);
	OBSDataAutoRelease overlaySettings=obs_data_create();obs_data_set_int(overlaySettings,"variant",1);
	OBSSourceAutoRelease overlaySource=obs_source_create_private("reflow_audit_pattern","Stationary reflow alert",overlaySettings);
	OBSSceneItem otherItem=obs_scene_add(scene,other),overlayItem=obs_scene_add(scene,overlaySource);
	const std::vector<obs_sceneitem_t *> items{item.Get(),otherItem.Get(),overlayItem.Get()};
	auto rect=[&](double x,double y,double w,double h,int order,int mode=3) {
		auto value=raw;value.insert("positionX",x);value.insert("positionY",y);value.insert("boundsWidth",w);value.insert("boundsHeight",h);
		value.insert("boundsType",mode);value.insert("scaleX",1);value.insert("scaleY",1);value.insert("cropLeft",0);value.insert("cropRight",0);
		value.insert("cropTop",0);value.insert("cropBottom",0);value.insert("cropToBounds",true);value.insert("order",order);return value;
	};
	auto coverage=[&](const QJsonArray &from,const QJsonArray &to,int width,int height,const char *label) {
		std::fprintf(stderr,"Reflow coverage: %s\n",label);std::fflush(stderr);
		OBSSceneAutoRelease coverageScene=obs_scene_create_private(label);
		OBSDataAutoRelease size=obs_data_create();obs_data_set_bool(size,"custom_size",true);obs_data_set_int(size,"cx",width);obs_data_set_int(size,"cy",height);
		obs_source_update(obs_scene_get_source(coverageScene),size);obs_source_load(obs_scene_get_source(coverageScene));
		check(obs_source_get_width(obs_scene_get_source(coverageScene))==uint32_t(width)&&obs_source_get_height(obs_scene_get_source(coverageScene))==uint32_t(height),
			"coverage fixture uses its actual landscape or portrait canvas");
		const std::vector<obs_sceneitem_t *> coverageItems{obs_scene_add(coverageScene,source),obs_scene_add(coverageScene,other),obs_scene_add(coverageScene,overlaySource)};
		for(int n=0;n<3;++n){PulseMotionEngineTestAccess::applyTransform(coverageItems[n],from[n].toObject());obs_sceneitem_force_update_transform(coverageItems[n]);}
		const auto before=reflowScenePixels(coverageScene,width,height);
		for(int n=0;n<3;++n){PulseMotionEngineTestAccess::applyTransform(coverageItems[n],to[n].toObject());obs_sceneitem_force_update_transform(coverageItems[n]);}
		const auto after=reflowScenePixels(coverageScene,width,height);
		for(double p:{0.0,.1,.25,.39,.5,.64,.65,.7,.8,.95,.9999,1.0,.5,.2}) {
			check(PulseMotionEngineTestAccess::reflowCoverageFrame(engine,coverageItems,from,to,p)>0,"default reflow recognises the main-view coverage pair");
			const auto pixels=reflowScenePixels(coverageScene,width,height);int gaps=0;
			for(int y=2;y<height-2;++y)for(int x=2;x<width-2;++x)
				if(qAlpha(before.pixel(x,y))>250&&qAlpha(after.pixel(x,y))>250&&qAlpha(pixels.pixel(x,y))<240)++gaps;
			if(gaps>width*2)std::fprintf(stderr,"Reflow coverage %s progress%g gaps%d\n",label,p,gaps);
			check(gaps<=width*2,"default reflow exposes no additional background gaps during view changes");
			const auto alert=pixels.pixel(width/2,5);
			check(qRed(alert)>240&&qBlue(alert)>240&&qGreen(alert)<15,"stationary alert remains above moving camera views");
		}
		obs_source_remove(obs_scene_get_source(coverageScene));
	};
	const auto alert=rect(0,0,640,10,2,1);
	const QJsonArray wideFrom{rect(0,0,640,480,0),rect(455,25,160,120,1),alert};
	const QJsonArray wideTo{rect(455,25,160,120,1),rect(0,0,640,480,0),alert};
	coverage(wideFrom,wideTo,640,480,"full to inset");coverage(wideTo,wideFrom,640,480,"inset to full");
	const QJsonArray splitFrom{rect(0,0,640,360,0,4),rect(0,360,640,778,1),alert};
	const QJsonArray splitTo{rect(0,360,640,778,1),rect(0,0,640,360,0,4),alert};
	coverage(splitFrom,splitTo,640,1138,"portrait panels");coverage(splitTo,splitFrom,640,1138,"reverse portrait panels");
	const QJsonArray columnFrom{rect(20,25,160,120,1),rect(0,0,640,480,0),alert};
	const QJsonArray columnTo{rect(0,0,240,480,1),rect(240,0,400,480,0),alert};
	coverage(columnFrom,columnTo,640,480,"inset to column");coverage(columnTo,columnFrom,640,480,"column to inset");
	auto absentFull=rect(0,0,640,480,0);absentFull.insert("visible",false);
	auto visibleFull=absentFull;visibleFull.insert("visible",true);visibleFull.insert("boundsType",0);visibleFull.insert("scaleX",1);visibleFull.insert("scaleY",480.0/360);
	auto leavingFull=rect(0,0,640,480,1);auto hiddenFull=leavingFull;hiddenFull.insert("visible",false);
	coverage(QJsonArray{absentFull,leavingFull,alert},QJsonArray{visibleFull,hiddenFull,alert},640,480,"new main source");
	coverage(QJsonArray{visibleFull,hiddenFull,alert},QJsonArray{absentFull,leavingFull,alert},640,480,"return from new main");
	auto absentBottom=rect(-377,363,1374,775,0);absentBottom.insert("visible",false);
	auto visibleBottom=absentBottom;visibleBottom.insert("visible",true);visibleBottom.insert("boundsType",0);visibleBottom.insert("scaleX",1374.0/640);visibleBottom.insert("scaleY",775.0/360);
	auto oldBottom=rect(0,360,640,778,1);auto hiddenBottom=oldBottom;hiddenBottom.insert("visible",false);
	coverage(QJsonArray{absentBottom,oldBottom,alert},QJsonArray{visibleBottom,hiddenBottom,alert},640,1138,"portrait desk region");
	coverage(QJsonArray{visibleBottom,hiddenBottom,alert},QJsonArray{absentBottom,oldBottom,alert},640,1138,"return portrait desk region");
	for(bool flipped:{false,true}) {
		auto unsupportedFrom=wideFrom,unsupportedTo=wideTo;
		auto changedFrom=unsupportedFrom[0].toObject(),changedTo=unsupportedTo[0].toObject();
		changedFrom.insert(flipped ? "scaleX" : "rotation",flipped ? -1 : 17);
		changedTo.insert(flipped ? "scaleX" : "rotation",flipped ? -1 : 17);
		unsupportedFrom[0]=changedFrom;unsupportedTo[0]=changedTo;
		check(PulseMotionEngineTestAccess::reflowCoverageFrame(engine,items,unsupportedFrom,unsupportedTo,.25)==0,
			"rotated or flipped bounds cannot masquerade as a rectangular camera coverage pair");
		auto leavingUnsupported=leavingFull;leavingUnsupported.insert(flipped ? "scaleX" : "rotation",flipped ? -1 : 17);
		check(PulseMotionEngineTestAccess::reflowCoverageFrame(engine,items,QJsonArray{absentFull,leavingFull,alert},
			QJsonArray{visibleFull,leavingUnsupported,alert},.25)==0,
			"a visible unsupported destination cannot masquerade as a shrinking camera reveal");
	}
	auto artworkFrom=rect(0,0,640,480,2);artworkFrom.insert("visible",false);
	const auto artworkTo=rect(0,0,640,480,2);
	for(double p:{.25,.75}) {
		check(PulseMotionEngineTestAccess::reflowCoverageFrame(engine,items,QJsonArray{absentFull,leavingFull,artworkFrom},
			QJsonArray{absentFull,hiddenFull,artworkTo},p)==0,"full-canvas artwork cannot become a moving camera coverage pair");
		const auto actual=PulseMotionEngineTestAccess::frame(overlayItem);
		check(actual==(p<.5 ? artworkFrom : artworkTo),"artwork switches at the midpoint without moving or resizing");
	}
	check(PulseMotionEngineTestAccess::editorStyle(engine,"reflow")=="reflow", "saving a reflow Look retains its motion profile");
	check(PulseMotionEngineTestAccess::editorStyle(engine,"legacy")=="legacy", "saving an explicitly legacy Look retains its compatibility profile");
	check(PulseMotionEngineTestAccess::editorStyle(engine,{})=="reflow", "existing and new layouts default to smooth camera reflow");
	check(obs_source_get_audio_mixers(source)==5 && !obs_source_muted(source), "camera movement never changes audio routing or mute state");
	check(QJsonDocument::fromJson(obs_data_get_json(OBSDataAutoRelease(obs_source_get_settings(source)))).object()==inputSettings,
		"camera movement never edits shared source or filter settings");
	obs_source_remove(obs_scene_get_source(scene)); obs_source_remove(source);
	obs_source_remove(other);obs_source_remove(overlaySource);
	item=nullptr;otherItem=nullptr;overlayItem=nullptr;scene=nullptr;source=nullptr;other=nullptr;overlaySource=nullptr;obs_wait_for_destroy_queue();
}
