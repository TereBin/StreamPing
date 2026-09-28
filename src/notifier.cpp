#include "notifier.hpp"

#include "discord-message.hpp"
#include "message-template.hpp"
#include "settings-dialog.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QWidget>

namespace
{
constexpr int kMaximumSendAttempts = 3;

QByteArray logText(const QString &value) { return value.toUtf8(); }
} // namespace

StreamNotifier::StreamNotifier(QWidget *mainWindow)
    : QObject(mainWindow), mainWindow_(mainWindow), chzzk_(this), discord_(this)
{
  pollTimer_.setSingleShot(true);
  connect(&pollTimer_, &QTimer::timeout, this, &StreamNotifier::poll);
}

void StreamNotifier::onStreamingStarted()
{
  ++runId_;
  pollTimer_.stop();
  settings_ = store_.load();
  elapsedSeconds_ = 0;
  sendAttempts_ = 0;
  notificationInFlight_ = false;

  const bool discordConfigured =
      settings_.discordEnabled && isValidDiscordWebhook(settings_.discordWebhook);
  if (settings_.channelId.isEmpty() || (!discordConfigured && !settings_.xEnabled))
  {
    blog(LOG_WARNING, "[StreamPing] Settings are incomplete; notification was skipped");
    return;
  }

  blog(LOG_INFO, "[StreamPing] Streaming started; waiting for CHZZK live status");
  pollTimer_.start(settings_.initialDelaySeconds * 1000);
}

void StreamNotifier::onStreamingStopped()
{
  ++runId_;
  pollTimer_.stop();
  notificationInFlight_ = false;
  blog(LOG_INFO, "[StreamPing] Streaming stopped; pending notification cancelled");
}

void StreamNotifier::showSettings()
{
  SettingsDialog dialog(store_, mainWindow_);
  dialog.exec();
}

void StreamNotifier::poll()
{
  if (notificationInFlight_)
    return;

  const quint64 currentRun = runId_;
  chzzk_.fetchLive(
      settings_.channelId,
      [this, currentRun](const LiveInfo &live, const QString &error)
      {
        if (currentRun != runId_)
          return;

        if (!error.isEmpty())
        {
          blog(LOG_WARNING, "[StreamPing] %s", logText(error).constData());
          scheduleNextPoll();
          return;
        }

        if (!live.isLive)
        {
          scheduleNextPoll();
          return;
        }

        QString liveKey = live.liveKey;
        if (liveKey.isEmpty())
        {
          liveKey =
              QStringLiteral("%1:%2:%3")
                  .arg(settings_.channelId, live.title,
                       QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd")));
        }

        LiveInfo keyedLive = live;
        keyedLive.liveKey = liveKey;
        const bool needsX = settings_.xEnabled && liveKey != settings_.lastXPromptedLiveKey;
        bool xOpened = false;
        if (needsX)
        {
          xOpened = x_.openComposer(renderMessage(settings_.xMessageTemplate, keyedLive));
          if (xOpened)
          {
            settings_.lastXPromptedLiveKey = liveKey;
            QString saveError;
            if (!store_.save(settings_, &saveError))
              blog(LOG_WARNING, "[StreamPing] Failed to save X duplicate guard: %s",
                   logText(saveError).constData());
          }
          else
          {
            blog(LOG_WARNING, "[StreamPing] Failed to open X compose window");
          }
        }

        const bool needsDiscord = settings_.discordEnabled &&
                                  isValidDiscordWebhook(settings_.discordWebhook) &&
                                  liveKey != settings_.lastDiscordNotifiedLiveKey;
        if (needsDiscord)
        {
          sendDiscordNotification(keyedLive);
        }
        else if (needsX)
        {
          stopRun(xOpened ? QStringLiteral("X 작성 화면을 열었습니다.")
                          : QStringLiteral("X 작성 화면을 열 수 없습니다."));
        }
        else
        {
          stopRun(QStringLiteral("현재 방송의 알림 처리를 이미 완료했습니다."));
        }
      });
}

void StreamNotifier::scheduleNextPoll()
{
  elapsedSeconds_ += settings_.pollingIntervalSeconds;
  if (elapsedSeconds_ >= settings_.maximumWaitSeconds)
  {
    stopRun(QStringLiteral("치지직 LIVE 확인 제한 시간을 초과했습니다."));
    return;
  }
  pollTimer_.start(settings_.pollingIntervalSeconds * 1000);
}

void StreamNotifier::stopRun(const QString &reason)
{
  pollTimer_.stop();
  blog(LOG_INFO, "[StreamPing] %s", logText(reason).constData());
}

void StreamNotifier::sendDiscordNotification(const LiveInfo &live)
{
  notificationInFlight_ = true;
  ++sendAttempts_;
  const quint64 currentRun = runId_;
  const QString message = prependDiscordRoleMention(
      renderMessage(settings_.discordMessageTemplate, live), settings_.discordRoleId);

  discord_.send(settings_.discordWebhook, message,
                [this, currentRun, live](const QString &error)
                {
                  if (currentRun != runId_)
                    return;

                  notificationInFlight_ = false;
                  if (!error.isEmpty())
                  {
                    blog(LOG_WARNING, "[StreamPing] %s", logText(error).constData());
                    if (sendAttempts_ >= kMaximumSendAttempts)
                    {
                      stopRun(QStringLiteral("Discord 전송을 3회 실패했습니다."));
                    }
                    else
                    {
                      scheduleNextPoll();
                    }
                    return;
                  }

                  settings_.lastDiscordNotifiedLiveKey = live.liveKey;
                  QString saveError;
                  if (!store_.save(settings_, &saveError))
                    blog(LOG_WARNING, "[StreamPing] Failed to save duplicate guard: %s",
                         logText(saveError).constData());
                  stopRun(QStringLiteral("Discord 방송 알림을 전송했습니다."));
                });
}
