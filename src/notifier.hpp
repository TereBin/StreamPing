#pragma once

#include "chzzk-client.hpp"
#include "discord-client.hpp"
#include "settings.hpp"
#include "x-client.hpp"

#include <QObject>
#include <QTimer>

class QWidget;

class StreamNotifier : public QObject
{
  Q_OBJECT

public:
  explicit StreamNotifier(QWidget *mainWindow);
  void onStreamingStarted();
  void onStreamingStopped();
  void showSettings();

private:
  void poll();
  void scheduleNextPoll();
  void stopRun(const QString &reason);
  void sendDiscordNotification(const LiveInfo &live);

  QWidget *mainWindow_;
  SettingsStore store_;
  PluginSettings settings_;
  ChzzkClient chzzk_;
  DiscordClient discord_;
  XClient x_;
  QTimer pollTimer_;
  quint64 runId_ = 0;
  int elapsedSeconds_ = 0;
  int sendAttempts_ = 0;
  bool notificationInFlight_ = false;
};
