// Included after the audit helpers in main.cpp, so these checks exercise the
// same source graph, production Finish wrapper and geometry measurements.
static void selectionFirstTests(const obs_video_info &initialVideo)
{
	obs_video_info video = initialVideo; video.base_width = video.output_width = 1920; video.base_height = video.output_height = 1080;
	check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "selection-first landscape canvas");
	obs_video_info portrait = video; portrait.base_width = portrait.output_width = 1080; portrait.base_height = portrait.output_height = 1920;
	OBSCanvasAutoRelease canvas = obs_canvas_create("Pulse Weaver Vertical", &portrait, PROGRAM); check(bool(canvas), "selection-first portrait canvas");
	QTemporaryDir files; QMap<QString, OBSSource> inputs;
	if (!obs_source_get_display_name("color_filter")) {
		obs_source_info type{}; type.id="color_filter"; type.type=OBS_SOURCE_TYPE_FILTER; type.output_flags=OBS_SOURCE_VIDEO;
		type.get_name=[](void *){return "Inert colour fixture";}; type.create=[](obs_data_t *,obs_source_t *)->void *{return new int(1);}; type.destroy=[](void *p){delete static_cast<int *>(p);}; obs_register_source(&type);
	}
	for (const auto &role : {QString("presenter"), QString("game"), QString("screen"), QString("activity")}) {
		const QSize size = role == "presenter" ? QSize(1200,900) : role == "activity" ? QSize(900,1600) : role == "screen" ? QSize(2520,1080) : QSize(1920,1080);
		const auto path = labelledInput(files.path(), "Select " + role, size, QColor(role == "game" ? "#215876" : "#634876"));
		OBSDataAutoRelease settings = obs_data_create(); obs_data_set_string(settings, "file", path.toUtf8().constData());
		OBSSourceAutoRelease source = obs_source_create("image_source", ("Select " + role).toUtf8().constData(), settings, nullptr); inputs.insert(role, OBSSource(source));
	}
	OBSDataAutoRelease priorColour=obs_data_create();obs_data_set_double(priorColour,"saturation",.15);
	OBSSourceAutoRelease priorFilter=obs_source_create_private("color_filter","Retained camera colour",priorColour);
	check(bool(priorFilter),"existing camera filter fixture");obs_source_filter_add(inputs["presenter"],priorFilter);
	PulseSelectPlan::Options all;
	for (const auto &choice : PulseSelectPlan::choices()) {
		check(!choice.key.contains("yug", Qt::CaseInsensitive) && !choice.title.contains("printer", Qt::CaseInsensitive), "catalogue generalises activity cameras without copying bespoke stream templates"); all.selected.insert(choice.key);
	}
	for (auto it = inputs.begin(); it != inputs.end(); ++it) all.sources.insert(it.key(), obs_source_get_uuid(it.value()));
	QVector<PulseSelectPlan::Options> cases;
	for (const auto &choice : PulseSelectPlan::choices()) { auto one = all; one.selected = {choice.key}; cases.append(one); }
	cases.append(all);
	auto layered=all;
	for(int i=0;i<3;++i) {
		const QString id="layer-"+QString::number(i+1);layered.newSources.insert(id,QJsonObject{{"name","Select alert "+QString::number(i+1)},{"kind","browser_source"},{"url","https://example.invalid/selection-alert/"+QString::number(i+1)}});
		layered.alerts[i]={true,"new:"+id,"new:"+id,"all"};
	}
	layered.framing.insert("presenterMonochrome",true);layered.framing.insert("presenterFill",true);cases.append(layered);
	auto noCamera = all; noCamera.selected = {"select-main/game-camera"}; noCamera.sources.remove("presenter"); cases.append(noCamera);
	for (int index = 0; index < cases.size(); ++index) {
		auto options = cases[index]; options.name = "Select QA " + QString::number(index);
		check(PulseSelectPlan::problem(options).isEmpty(), "each selected outcome has a valid plan, including holding-only and camera-free game");
		const auto needed = PulseSelectPlan::inputs(options);
		if (options.selected == QSet<QString>{"guided-starting/card"}) check(needed.isEmpty(), "a starting-only show asks for no captures");
		QTemporaryDir directory; const QString store = directory.filePath("plugin_config/pulse-weaver-core/motion.json"); check(QDir().mkpath(QFileInfo(store).absolutePath()), "selection fixture storage");
		const auto baseline = wizardSourceIdentities(); QMap<QString,QJsonObject> before;
		for (auto it = inputs.begin(); it != inputs.end(); ++it) before.insert(it.key(), preservedInputState(it.value()));
		PulseMotionEngine engine(nullptr, store); const auto request = PulseSelectPlan::draft(options), result = engine.createGuidedShow(request);
		if (!result.value("ok").toBool()) std::fprintf(stderr, "selection build error: %s\n", result.value("message").toString().toUtf8().constData());
		check(result.value("ok").toBool(), "every selection-first recipe creates successfully through the real backend");
		const auto actions = PulseMotionEngineTestAccess::actions(engine); check(actions.size() == options.selected.size(), "exactly the selected Looks are emitted");
		QSet<QString> emitted;
		for (const auto &entry : actions) {
			const auto action = entry.toObject(); const auto key = action.value("templateId").toString(); check(options.selected.contains(key) && !emitted.contains(key), "no cosmetic or unselected Looks are generated"); emitted.insert(key);
			PulseMotionEngineTestAccess::settleLayout(engine,action);
			const auto *look = PulseSelectPlan::look(key);
			for (const auto &route : {QString("horizontal"), QString("vertical")}) {
				QSet<QString> visible, expected;
				for (const auto &role : {look->main,look->support,look->third}) if (!role.isEmpty() && role != "graphic" && !options.sources.value(role).isEmpty()) expected.insert(options.sources.value(role));
				for (const auto &value : action.value("items").toArray()) {
					const auto item = value.toObject(), transform = item.value("transform").toObject();
					if (item.value("container").toString().endsWith(" · Portrait") != (route == "vertical") || !transform.value("visible").toBool()) continue;
					const auto id = item.value("sourceUuid").toString(); if (all.sources.values().contains(id)) visible.insert(id);
					OBSSourceAutoRelease source=obs_get_source_by_uuid(id.toUtf8().constData());
					if (source && obs_scene_from_source(source)) {
						check(options.framing.value("presenterMonochrome").toBool() && obs_source_filter_count(source)==1,"camera colour uses a single native filter on a wrapper");
						obs_scene_enum_items(obs_scene_from_source(source),[](obs_scene_t *,obs_sceneitem_t *child,void *opaque){static_cast<QSet<QString> *>(opaque)->insert(obs_source_get_uuid(obs_sceneitem_get_source(child)));return true;},&visible);
					}
					const double w = route == "vertical" ? 1080 : 1920, h = route == "vertical" ? 1920 : 1080;
					check(transform.value("positionX").toDouble() >= 0 && transform.value("positionY").toDouble() >= 0 && transform.value("boundsWidth").toDouble() > 0 && transform.value("boundsHeight").toDouble() > 0 && transform.value("positionX").toDouble() + transform.value("boundsWidth").toDouble() <= w + .5 && transform.value("positionY").toDouble() + transform.value("boundsHeight").toDouble() <= h + .5, "all generated visible panels stay within their actual canvas");
					if (id == options.sources.value("game") || id == options.sources.value("screen")) check(transform.value("cropLeft").toInt() == 0 && transform.value("cropRight").toInt() == 0 && transform.value("cropTop").toInt() == 0 && transform.value("cropBottom").toInt() == 0, "wide game and screen pictures retain the complete frame");
				}
				check(visible == expected, "each Look displays exactly its intended shared inputs on both canvases");
			}
		}
		if (options.selected == all.selected) check(request.value("stages").toArray().size() == 5 && actions.size() == 11, "the Q3-inspired general workflows share one Main Stage plus four optional screens");
		if(options.alerts[0].enabled) {
			int copies=0;for(const auto &source:auditSources())if(QString::fromUtf8(obs_source_get_name(source)).startsWith("Select alert ")){++copies;check(obs_source_get_audio_mixers(source)==0 && obs_source_get_monitoring_type(source)==OBS_MONITORING_TYPE_NONE,"new alert browser copies start silent to avoid double audio");}
			check(copies==6 && request.value("overlays").toArray().size()==6,"all three layers have exactly one browser copy per canvas");
		}
		for (auto it = inputs.begin(); it != inputs.end(); ++it) check(preservedInputState(it.value()) == before.value(it.key()) && obs_source_filter_count(it.value())==(it.key()=="presenter"?1:0), "reused source filters, settings, audio routing and monitoring remain unchanged");
		OBSDataAutoRelease retainedColour=obs_source_get_settings(priorFilter);check(obs_data_get_double(retainedColour,"saturation")==.15,"optional monochrome never rewrites the original camera colour correction");
		removeAuditAdditions(baseline);
	}
	// Missing inputs are conditional, and stale assignments from Back are pruned.
	auto changed = all; changed.selected = {"select-main/game-camera"}; changed.newSources.insert("unused", QJsonObject{{"kind","image_source"},{"name","Do not create this"}});
	check(PulseSelectPlan::inputs(changed).keys() == QStringList{"game","presenter"} && !PulseSelectPlan::draft(changed).value("newSources").toObject().contains("unused"), "only selected layouts prompt for inputs or retain source drafts");
	changed.sources.remove("game"); check(!PulseSelectPlan::problem(changed).isEmpty(), "a selected gameplay Look requires its game");
	changed = all; changed.sources["activity"] = changed.sources["presenter"]; check(!PulseSelectPlan::problem(changed).isEmpty(), "a two-camera layout cannot silently show the same camera twice");
	changed=layered;changed.selected={"guided-starting/card"};changed.alerts[0].scope="main";check(!PulseSelectPlan::problem(changed).isEmpty(),"holding-only alerts cannot silently target absent Main layouts");
	changed=layered;changed.alerts[1].horizontal=changed.alerts[0].horizontal;check(!PulseSelectPlan::problem(changed).isEmpty(),"duplicate browser layers are corrected before Create");

	// Drive the actual default entry, with a real pre-existing catalogue file.
	QTemporaryDir directory; const QString store = directory.filePath("config/obs-studio/plugin_config/pulse-weaver-core/motion.json"), stagePath = directory.filePath("config/obs-studio/pulseweaver-stages.json"); check(QDir().mkpath(QFileInfo(store).absolutePath()), "selection Finish fixture storage");
	writeFile(store, QJsonDocument(QJsonObject{{"version",1},{"actions",QJsonArray{}},{"originals",QJsonArray{}},{"originalScenes",QJsonObject{}}}).toJson());
	const QJsonObject retained{{"name","Retained user show"},{"horizontal","Unrelated existing scene"},{"auditSentinel","preserve every field"}}; writeFile(stagePath, QJsonDocument(QJsonArray{retained}).toJson());
	const auto baseline = wizardSourceIdentities(); bool finished = false;
	{
		PulseMotionEngine engine(nullptr, store);
		QTimer::singleShot(0, [&] {
			auto *wizard = dynamic_cast<PulseSelectWizard *>(QApplication::activeModalWidget()); check(wizard && wizard->property("pulseSelectionFirst").toBool(), "default production entry opens selection-first setup"); settleWizard();
			check(wizard->pageIds() == QList<int>{0,1} && !wizard->button(QWizard::NextButton)->isEnabled(), "only two required pages, with no empty automatic selection"); captureWizardWindow(wizard, "select-0-layouts");
			auto choose = [&](const QString &key) { for (auto *button : wizard->findChildren<QPushButton *>()) if (button->property("pulseSelectLook") == key) { button->click(); return; } check(false,"selection card exists"); };
			choose("guided-starting/card"); wizard->next(); settleWizard();
			for (auto *row : wizard->findChildren<QWidget *>()) if (row->property("pulseSelectInput").isValid()) check(!row->isVisibleTo(wizard), "holding-only flow hides every irrelevant source prompt");
			check(wizard->button(QWizard::FinishButton)->isEnabled(), "a static starting screen can be built immediately"); captureWizardWindow(wizard,"select-1-holding-only");
			wizard->back(); choose("guided-starting/card"); choose("select-main/game-camera"); choose("select-main/screen-cameras"); wizard->next(); settleWizard();
			check(!wizard->button(QWizard::FinishButton)->isEnabled(), "missing required pictures block creation on the build page");
			for (const auto &role : {QString("presenter"),QString("game"),QString("screen"),QString("activity")}) {
				QTimer::singleShot(0,[&] { auto *picker=qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(picker && picker->objectName()=="PulseWeaverSetupChoiceDialog","existing input opens the picker directly, without a preliminary question"); setupField<QPushButton>(picker,"Select "+role)->click(); });
				setupField<QPushButton>(wizard,"Choose existing "+role)->click();
			}
			setupField<QLineEdit>(wizard,"Show name")->setText("Selection Finish"); settleWizard();
			const auto beforeEdit = PulseSelectPlan::draft(wizard->options());
			QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());setupField<QSlider>(dialog,"Landscape inset size")->setValue(39);dialog->reject();}); guidedButton(wizard,"Camera framing & colour")->click();
			check(PulseSelectPlan::draft(wizard->options())==beforeEdit,"Cancel inside optional framing preserves the plan");
			wizard->back(); wizard->next(); check(wizard->options().sources == all.sources && wizard->options().selected.size()==2,"Back preserves assignments and selected outcomes");
			check(wizard->finalProblem().isEmpty() && wizardSourceIdentities()==baseline,"valid two-page planning creates no native sources or scenes"); captureWizardWindow(wizard,"select-1-ready");
			QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(auto *error=qobject_cast<QMessageBox *>(dialog))std::fprintf(stderr,"Selection Finish error: %s\n",error->text().toUtf8().constData());check(dialog&&dialog->windowTitle()=="Your show is created","actual Finish commits and opens completion");finished=true;guidedButton(dialog,"Done")->click();}); wizard->button(QWizard::FinishButton)->click();
		});
		PulseMotionEngineTestAccess::openWizard(engine); check(finished,"selection-first production transaction completes");
		const auto saved=QJsonDocument::fromJson(readFile(stagePath)).array();check(saved.size()==2&&saved.first().toObject()==retained,"Finish atomically appends the show while preserving existing catalogue entries");
		check(PulseMotionEngineTestAccess::actions(engine).size()==2,"production Finish builds exactly both requested operating layouts");
	}
	removeAuditAdditions(baseline);
	for (const auto &source : inputs) obs_source_remove(source); inputs.clear(); obs_canvas_remove(canvas); obs_wait_for_destroy_queue();
	obs_video_info restored=initialVideo;check(obs_reset_video(&restored)==OBS_VIDEO_SUCCESS,"selection fixture restores canvas dimensions");
}
