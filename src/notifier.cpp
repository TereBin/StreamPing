#include "notifier.hpp"

#include "discord-message.hpp"
#include "message-template.hpp"
#include "settings-dialog.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QWidget>

namespace
{
constexpr int kMaximumSendAttempts = 3;

QByteArray logText(const QString &value) { return value.toUtf8(); }
} // namespace

StreamNotifier::StreamNotifier(QWidget *mainWindow)
    : QObject(mainWindow), mainWindow_(mainWindow), chzzk_(this), discord_(this),
      updateChecker_(this)
{
  pollTimer_.setSingleShot(true);
  connect(&pollTimer_, &QTimer::timeout, this, &StreamNotifier::poll);
  QTimer::singleShot(12000, this, &StreamNotifier::checkForUpdates);
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

void StreamNotifier::checkForUpdates()
{
  PluginSettings updateSettings = store_.load();
  if (!updateSettings.automaticUpdateChecks)
    return;

  const QDateTime lastCheck = QDateTime::fromString(updateSettings.lastUpdateCheckUtc, Qt::ISODate);
  if (lastCheck.isValid())
  {
    const qint64 elapsedSeconds = lastCheck.secsTo(QDateTime::currentDateTimeUtc());
    if (elapsedSeconds >= 0 && elapsedSeconds < 86400)
      return;
  }

  updateChecker_.check(
      QString::fromUtf8(STREAMPING_VERSION),
      [this](const UpdateCheckResult &result)
      {
        PluginSettings settings = store_.load();
        const bool requiredUpdate = result.updateAvailable && result.error.isEmpty() &&
                                    result.update.urgency == UpdateUrgency::Required;
        settings.lastUpdateCheckUtc =
            requiredUpdate ? QString() : QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QString saveError;
        if (!store_.save(settings, &saveError))
          blog(LOG_WARNING, "[StreamPing] Failed to save update check time: %s",
               logText(saveError).constData());

        if (!result.error.isEmpty())
        {
          blog(LOG_WARNING, "[StreamPing] Update check failed: %s",
               logText(result.error).constData());
          return;
        }
        if (!result.updateAvailable)
          return;
        if (result.update.urgency != UpdateUrgency::Required &&
            settings.skippedUpdateVersion == result.update.latestVersion)
          return;
        showUpdateNotice(result.update);
      });
}

void StreamNotifier::showUpdateNotice(const UpdateInfo &update)
{
  auto *messageBox = new QMessageBox(mainWindow_);
  messageBox->setAttribute(Qt::WA_DeleteOnClose);
  messageBox->setWindowTitle(update.urgency == UpdateUrgency::Required
                                 ? QStringLiteral("StreamPing 필수 업데이트")
                                 : (update.urgency == UpdateUrgency::Recommended
                                        ? QStringLiteral("StreamPing 권장 업데이트")
                                        : QStringLiteral("StreamPing 업데이트")));
  messageBox->setIcon(update.urgency == UpdateUrgency::Required
                          ? QMessageBox::Critical
                          : (update.urgency == UpdateUrgency::Recommended
                                 ? QMessageBox::Warning
                                 : QMessageBox::Information));
  messageBox->setText(
      update.title.isEmpty()
          ? QStringLiteral("StreamPing %1 버전이 배포되었습니다.").arg(update.latestVersion)
          : update.title);
  messageBox->setInformativeText(
      QStringLiteral("현재 버전: %1\n최신 버전: %2\n\n%3")
          .arg(QString::fromUtf8(STREAMPING_VERSION), update.latestVersion, update.message));

  auto *downloadButton =
      messageBox->addButton(QStringLiteral("다운로드 페이지 열기"), QMessageBox::AcceptRole);
  messageBox->addButton(QStringLiteral("나중에"), QMessageBox::RejectRole);
  QPushButton *skipButton = nullptr;
  if (update.urgency != UpdateUrgency::Required)
    skipButton = messageBox->addButton(QStringLiteral("이 버전 건너뛰기"), QMessageBox::ActionRole);

  connect(messageBox, &QMessageBox::buttonClicked, this,
          [this, update, downloadButton, skipButton](QAbstractButton *button)
          {
            if (button == downloadButton)
            {
              QDesktopServices::openUrl(QUrl(update.downloadUrl));
              return;
            }
            if (skipButton && button == skipButton)
            {
              PluginSettings settings = store_.load();
              settings.skippedUpdateVersion = update.latestVersion;
              QString error;
              if (!store_.save(settings, &error))
                blog(LOG_WARNING, "[StreamPing] Failed to save skipped update: %s",
                     logText(error).constData());
            }
          });
  messageBox->open();
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
  const QString message = replaceDiscordRoleTag(
      renderMessage(settings_.discordMessageTemplate, live), settings_.discordRoleId);
  const DiscordEmbedOptions embedOptions{
      settings_.discordEmbedColor, settings_.discordEmbedShowChannel,
      settings_.discordEmbedShowCategory, settings_.discordEmbedShowThumbnail};

  discord_.send(settings_.discordWebhook, message, live, embedOptions,
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
