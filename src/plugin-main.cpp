#include "notifier.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("streamping", "ko-KR")

namespace
{
StreamNotifier *g_notifier = nullptr;
QAction *g_settingsAction = nullptr;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
  if (!g_notifier)
    return;

  switch (event)
  {
  case OBS_FRONTEND_EVENT_STREAMING_STARTED:
    g_notifier->onStreamingStarted();
    break;
  case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
    g_notifier->onStreamingStopped();
    break;
  default:
    break;
  }
}
} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
  return "Sends a Discord notification after a CHZZK stream becomes live.";
}

MODULE_EXPORT const char *obs_module_name(void) { return "StreamPing"; }

bool obs_module_load(void)
{
  auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
  g_notifier = new StreamNotifier(mainWindow);

  g_settingsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("StreamPing 설정"));
  QObject::connect(g_settingsAction, &QAction::triggered, g_notifier,
                   &StreamNotifier::showSettings);
  obs_frontend_add_event_callback(onFrontendEvent, nullptr);

  blog(LOG_INFO, "[StreamPing] Plugin loaded (version %s)", STREAMPING_VERSION);
  return true;
}

void obs_module_unload(void)
{
  obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
  delete g_settingsAction;
  delete g_notifier;
  g_settingsAction = nullptr;
  g_notifier = nullptr;
  blog(LOG_INFO, "[StreamPing] Plugin unloaded");
}
