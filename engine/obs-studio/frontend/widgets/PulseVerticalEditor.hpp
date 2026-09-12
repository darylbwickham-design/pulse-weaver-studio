#pragma once

#include <obs.h>

#include <QWidget>

#include <memory>

class OBSBasic;

/* Returns Pulse Weaver's referenced vertical canvas. Third-party canvases are
 * deliberately never returned here: they are import sources, not runtime
 * dependencies. */
obs_canvas_t *PulseWeaverGetVerticalCanvas();

/* Copies a scene into Pulse Weaver's canvas while preserving the scene-item
 * graph and native OBS transform data. The caller owns the returned scene. */
obs_scene_t *PulseWeaverDuplicateToVertical(obs_source_t *source, const char *name, bool fitHorizontalLayout,
					 bool linkToHorizontal);

/* Returns a referenced linked vertical scene, or nullptr. */
obs_source_t *PulseWeaverFindLinkedVerticalScene(const char *horizontalUuid);

class PulseVerticalEditor final : public QWidget {
public:
	explicit PulseVerticalEditor(OBSBasic *main, QWidget *parent = nullptr);
	~PulseVerticalEditor() override;

	void Refresh();
	void SetDisplayEnabled(bool enabled);
	void Shutdown();

private:
	void OpenProperties(obs_source_t *source);
	void OpenFilters(obs_source_t *source);

	struct Impl;
	std::unique_ptr<Impl> impl;
};
