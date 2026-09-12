#include <obs-module.h>
#include <obs-frontend-api.h>
#include <callback/calldata.h>
#include <callback/proc.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Pulse Weaver sample extension proving native action registration";
}

namespace {

void writeClock(void *, calldata_t *data)
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t stamp = std::chrono::system_clock::to_time_t(now);
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &stamp);
#else
	localtime_r(&stamp, &local);
#endif
	std::ostringstream text;
	text << "Sample Clock module: " << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
	const char *value = calldata_string(data, "value");
	if (value && *value)
		text << " — " << value;
	const std::string message = text.str();
	blog(LOG_INFO, "[Pulse Weaver module] %s", message.c_str());
	calldata_set_bool(data, "success", true);
	calldata_set_string(data, "message", message.c_str());
}

void registerAction()
{
	calldata_t registration;
	calldata_init(&registration);
	calldata_set_string(&registration, "module_id", "sample.clock");
	calldata_set_string(&registration, "module_name", "Sample Clock");
	calldata_set_string(&registration, "action_id", "write-clock-to-log");
	calldata_set_string(&registration, "action_name", "Write current time to log");
	calldata_set_string(&registration, "proc_name", "pulseweaver_sample_clock_write");
	proc_handler_call(obs_get_proc_handler(), "pulseweaver_register_action", &registration);
	calldata_free(&registration);
}

void frontendEvent(obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		registerAction();
}

} // namespace

bool obs_module_load(void)
{
	proc_handler_add(obs_get_proc_handler(),
			 "void pulseweaver_sample_clock_write(in string value, in string json, out bool success, out string message)",
			 writeClock, nullptr);
	obs_frontend_add_event_callback(frontendEvent, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
}
