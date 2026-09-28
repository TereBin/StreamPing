#include "settings-dialog.hpp"

#include "message-template.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace
{
constexpr int kMessageEditorHeight = 140;
}

SettingsDialog::SettingsDialog(const SettingsStore &store, QWidget *parent)
    : QDialog(parent), store_(store), originalSettings_(store_.load()), chzzk_(this),
      discord_(this), discordConnection_(this)
{
  setWindowTitle(QStringLiteral("StreamPing 설정"));
  setMinimumWidth(560);

  discordEnabled_ = new QCheckBox(QStringLiteral("방송 시작 시 Discord 자동 알림"), this);
  discordEnabled_->setChecked(originalSettings_.discordEnabled);

  channelId_ = new QLineEdit(originalSettings_.channelId, this);
  channelId_->setPlaceholderText(QStringLiteral("치지직 채널 URL 또는 32자리 채널 ID"));

  webhook_ = new QLineEdit(originalSettings_.discordWebhook, this);
  webhook_->setEchoMode(QLineEdit::PasswordEchoOnEdit);
  webhook_->setPlaceholderText(QStringLiteral("https://discord.com/api/webhooks/..."));

  discordStatus_ = new QLabel(this);
  discordStatus_->setWordWrap(true);
  connectDiscordButton_ = new QPushButton(QStringLiteral("Discord 연결"), this);
  disconnectDiscordButton_ = new QPushButton(QStringLiteral("연결 해제"), this);
  auto *discordConnectionLayout = new QHBoxLayout;
  discordConnectionLayout->addWidget(connectDiscordButton_);
  discordConnectionLayout->addWidget(disconnectDiscordButton_);
  discordConnectionLayout->addStretch();

  discordMessageTemplate_ = new QPlainTextEdit(originalSettings_.discordMessageTemplate, this);
  discordMessageTemplate_->setFixedHeight(kMessageEditorHeight);
  discordMessageTemplate_->setPlaceholderText(
      QStringLiteral("사용 가능: {title}, {category}, {channel}, {url}"));

  initialDelay_ = new QSpinBox(this);
  initialDelay_->setRange(0, 30);
  initialDelay_->setSuffix(QStringLiteral("초"));
  initialDelay_->setValue(originalSettings_.initialDelaySeconds);

  pollingInterval_ = new QSpinBox(this);
  pollingInterval_->setRange(2, 60);
  pollingInterval_->setSuffix(QStringLiteral("초"));
  pollingInterval_->setValue(originalSettings_.pollingIntervalSeconds);

  maximumWait_ = new QSpinBox(this);
  maximumWait_->setRange(10, 300);
  maximumWait_->setSuffix(QStringLiteral("초"));
  maximumWait_->setValue(originalSettings_.maximumWaitSeconds);

  auto *chzzkForm = new QFormLayout;
  chzzkForm->addRow(QStringLiteral("채널"), channelId_);
  auto *chzzkGroup = new QGroupBox(QStringLiteral("치지직"), this);
  chzzkGroup->setLayout(chzzkForm);

  auto *discordForm = new QFormLayout;
  discordForm->addRow(QString(), discordEnabled_);
  discordForm->addRow(QStringLiteral("상태"), discordStatus_);
  discordForm->addRow(QString(), discordConnectionLayout);
  discordForm->addRow(QStringLiteral("수동 Webhook (고급)"), webhook_);
  discordForm->addRow(QStringLiteral("메시지"), discordMessageTemplate_);
  auto *discordGroup = new QGroupBox(QStringLiteral("Discord"), this);
  discordGroup->setLayout(discordForm);

  xEnabled_ = new QCheckBox(QStringLiteral("방송 시작 시 X 작성 화면 열기"), this);
  xEnabled_->setChecked(originalSettings_.xEnabled);
  xMessageTemplate_ = new QPlainTextEdit(originalSettings_.xMessageTemplate, this);
  xMessageTemplate_->setFixedHeight(kMessageEditorHeight);
  xMessageTemplate_->setPlaceholderText(
      QStringLiteral("사용 가능: {title}, {category}, {channel}, {url}"));
  auto *xForm = new QFormLayout;
  xForm->addRow(QString(), xEnabled_);
  xForm->addRow(QStringLiteral("메시지"), xMessageTemplate_);
  auto *xGroup = new QGroupBox(QStringLiteral("X"), this);
  xGroup->setLayout(xForm);

  auto *timingForm = new QFormLayout;
  timingForm->addRow(QStringLiteral("첫 확인 지연"), initialDelay_);
  timingForm->addRow(QStringLiteral("확인 간격"), pollingInterval_);
  timingForm->addRow(QStringLiteral("최대 대기"), maximumWait_);
  auto *timingPanel = new QWidget(this);
  timingPanel->setLayout(timingForm);
  timingPanel->setVisible(false);
  auto *timingToggle = new QToolButton(this);
  timingToggle->setText(QStringLiteral("LIVE 확인"));
  timingToggle->setCheckable(true);
  timingToggle->setChecked(false);
  timingToggle->setArrowType(Qt::RightArrow);
  timingToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  testChzzkButton_ = new QPushButton(QStringLiteral("치지직 확인"), this);
  testDiscordButton_ = new QPushButton(QStringLiteral("알림 메시지 테스트"), this);
  testXButton_ = new QPushButton(QStringLiteral("X 작성 화면 테스트"), this);
  auto *testLayout = new QHBoxLayout;
  testLayout->addWidget(testChzzkButton_);
  testLayout->addWidget(testDiscordButton_);
  testLayout->addWidget(testXButton_);
  testLayout->addStretch();

  status_ = new QLabel(this);
  status_->setWordWrap(true);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  saveButton_ = buttons->button(QDialogButtonBox::Save);
  saveButton_->setText(QStringLiteral("저장"));
  buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(chzzkGroup);
  layout->addWidget(discordGroup);
  layout->addWidget(xGroup);
  layout->addWidget(timingToggle);
  layout->addWidget(timingPanel);
  layout->addLayout(testLayout);
  layout->addWidget(status_);
  layout->addWidget(buttons);

  connectionPollTimer_ = new QTimer(this);
  connectionPollTimer_->setSingleShot(true);
  updateDiscordStatus();

  connect(testChzzkButton_, &QPushButton::clicked, this, &SettingsDialog::testChzzk);
  connect(testDiscordButton_, &QPushButton::clicked, this, &SettingsDialog::testDiscordMessage);
  connect(testXButton_, &QPushButton::clicked, this, &SettingsDialog::testX);
  connect(connectDiscordButton_, &QPushButton::clicked, this, &SettingsDialog::connectDiscord);
  connect(disconnectDiscordButton_, &QPushButton::clicked, this,
          &SettingsDialog::disconnectDiscord);
  connect(connectionPollTimer_, &QTimer::timeout, this, &SettingsDialog::pollDiscordConnection);
  connect(timingToggle, &QToolButton::toggled, this,
          [this, timingToggle, timingPanel](bool expanded)
          {
            timingToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            timingPanel->setVisible(expanded);
            adjustSize();
          });
  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PluginSettings SettingsDialog::formSettings() const
{
  PluginSettings settings = originalSettings_;
  settings.discordEnabled = discordEnabled_->isChecked();
  settings.channelId = normalizeChannelId(channelId_->text());
  settings.discordWebhook = webhook_->text().trimmed();
  if (settings.discordWebhook != originalSettings_.discordWebhook)
    settings.discordChannelName.clear();
  if (settings.discordWebhook != originalSettings_.discordWebhook)
    settings.discordManagedWebhook = false;
  settings.discordMessageTemplate = discordMessageTemplate_->toPlainText().trimmed();
  settings.xEnabled = xEnabled_->isChecked();
  settings.xMessageTemplate = xMessageTemplate_->toPlainText().trimmed();
  settings.initialDelaySeconds = initialDelay_->value();
  settings.pollingIntervalSeconds = pollingInterval_->value();
  settings.maximumWaitSeconds = maximumWait_->value();
  return settings;
}

bool SettingsDialog::validate(PluginSettings *settings)
{
  *settings = formSettings();
  if (settings->channelId.isEmpty())
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("올바른 치지직 채널 URL 또는 채널 ID를 입력하세요."));
    return false;
  }
  if (!settings->discordWebhook.isEmpty() && !isValidDiscordWebhook(settings->discordWebhook))
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("올바른 Discord Webhook URL을 입력하세요."));
    return false;
  }
  if (settings->discordEnabled && !isValidDiscordWebhook(settings->discordWebhook))
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("Discord 자동 알림을 사용하려면 Discord를 연결하세요."));
    return false;
  }
  if (settings->discordEnabled && settings->discordMessageTemplate.isEmpty())
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("알림 메시지를 입력하세요."));
    return false;
  }
  if (settings->xEnabled && settings->xMessageTemplate.isEmpty())
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("X 메시지를 입력하세요."));
    return false;
  }
  return true;
}

void SettingsDialog::saveAndAccept()
{
  PluginSettings settings;
  if (!validate(&settings))
    return;

  QString error;
  if (!store_.save(settings, &error))
  {
    QMessageBox::critical(this, QStringLiteral("저장 실패"), error);
    return;
  }
  accept();
}

void SettingsDialog::testChzzk()
{
  const QString channelId = normalizeChannelId(channelId_->text());
  if (channelId.isEmpty())
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("올바른 치지직 채널 URL 또는 채널 ID를 입력하세요."));
    return;
  }

  setBusy(true, QStringLiteral("치지직 채널을 확인하는 중..."));
  chzzk_.fetchLive(
      channelId,
      [this](const LiveInfo &live, const QString &error)
      {
        setBusy(false);
        if (!error.isEmpty())
        {
          status_->setText(error);
          return;
        }
        status_->setText(
            live.isLive ? QStringLiteral("LIVE 확인: %1 / %2").arg(live.channelName, live.title)
                        : QStringLiteral("채널 확인 완료: 현재 방송 중이 아닙니다."));
      });
}

void SettingsDialog::testDiscordMessage()
{
  const QString webhook = webhook_->text().trimmed();
  if (!isValidDiscordWebhook(webhook))
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("올바른 Discord Webhook URL을 입력하세요."));
    return;
  }

  prepareTestMessage(
      discordMessageTemplate_, QStringLiteral("치지직 정보를 반영해 테스트 메시지를 만드는 중..."),
      [this, webhook](const QString &message, bool usedFallback)
      {
        discord_.send(webhook, message,
                      [this, usedFallback](const QString &sendError)
                      {
                        setBusy(false);
                        if (!sendError.isEmpty())
                        {
                          status_->setText(sendError);
                          return;
                        }
                        status_->setText(
                            usedFallback ? QStringLiteral("치지직 조회에 실패해 예시 값으로 알림 "
                                                          "메시지를 전송했습니다.")
                                         : QStringLiteral("현재 치지직 정보를 반영한 알림 "
                                                          "메시지를 전송했습니다."));
                      });
      });
}

void SettingsDialog::testX()
{
  prepareTestMessage(
      xMessageTemplate_, QStringLiteral("치지직 정보를 반영해 X 작성 화면을 준비하는 중..."),
      [this](const QString &message, bool usedFallback)
      {
        const bool opened = x_.openComposer(message);
        setBusy(false);
        if (!opened)
        {
          status_->setText(QStringLiteral("X 작성 화면을 열 수 없습니다."));
          return;
        }
        status_->setText(
            usedFallback ? QStringLiteral("치지직 조회에 실패해 예시 값으로 X 작성 "
                                          "화면을 열었습니다.")
                         : QStringLiteral("현재 치지직 정보를 반영한 X 작성 화면을 열었습니다."));
      });
}

void SettingsDialog::prepareTestMessage(
    QPlainTextEdit *editor, const QString &preparingStatus,
    std::function<void(const QString &message, bool usedFallback)> callback)
{
  const QString channelId = normalizeChannelId(channelId_->text());
  if (channelId.isEmpty())
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"),
                         QStringLiteral("올바른 치지직 채널 URL 또는 채널 ID를 입력하세요."));
    return;
  }

  const QString messageTemplate = editor->toPlainText().trimmed();
  if (messageTemplate.isEmpty())
  {
    QMessageBox::warning(this, QStringLiteral("입력 확인"), QStringLiteral("메시지를 입력하세요."));
    return;
  }

  setBusy(true, preparingStatus);
  chzzk_.fetchLive(channelId,
                   [channelId, messageTemplate, callback = std::move(callback)](
                       const LiveInfo &live, const QString &lookupError) mutable
                   {
                     const bool usedFallback = !lookupError.isEmpty() || live.title.isEmpty() ||
                                               live.category.isEmpty() ||
                                               live.channelName.isEmpty();
                     callback(renderTestMessage(messageTemplate, live, channelId), usedFallback);
                   });
}

void SettingsDialog::connectDiscord()
{
  if (!discordConnection_.isConfigured())
  {
    QMessageBox::information(
        this, QStringLiteral("Discord 연결"),
        QStringLiteral("이 빌드에는 Discord 연결 서비스 주소가 설정되지 않았습니다.\n"
                       "배포자가 OAuth 중계 서비스를 먼저 구성해야 합니다."));
    return;
  }

  connectDiscordButton_->setDisabled(true);
  disconnectDiscordButton_->setDisabled(true);
  status_->setText(QStringLiteral("Discord 연결을 준비하는 중..."));
  discordConnection_.start(
      [this](const QString &authorizationUrl, const QString &sessionId, const QString &pollToken,
             const QString &error)
      {
        if (!error.isEmpty())
        {
          updateDiscordStatus();
          status_->setText(error);
          return;
        }
        if (!QDesktopServices::openUrl(QUrl(authorizationUrl)))
        {
          updateDiscordStatus();
          status_->setText(QStringLiteral("Discord 인증 페이지를 열 수 없습니다."));
          return;
        }
        connectionSessionId_ = sessionId;
        connectionPollToken_ = pollToken;
        connectionPollSeconds_ = 0;
        status_->setText(QStringLiteral("브라우저에서 알림을 받을 서버와 채널을 선택해 주세요."));
        connectionPollTimer_->start(1000);
      });
}

void SettingsDialog::pollDiscordConnection()
{
  connectionPollSeconds_ += 2;
  discordConnection_.poll(connectionSessionId_, connectionPollToken_,
                          [this](bool pending, const QString &webhookUrl,
                                 const QString &channelName, const QString &error)
                          {
                            if (pending && connectionPollSeconds_ < 300)
                            {
                              connectionPollTimer_->start(2000);
                              return;
                            }
                            if (pending)
                            {
                              updateDiscordStatus();
                              status_->setText(
                                  QStringLiteral("Discord 연결 승인 시간이 만료되었습니다."));
                              return;
                            }
                            if (!error.isEmpty())
                            {
                              updateDiscordStatus();
                              status_->setText(error);
                              return;
                            }

                            webhook_->setText(webhookUrl);
                            originalSettings_.discordWebhook = webhookUrl;
                            originalSettings_.discordChannelName = channelName;
                            originalSettings_.discordManagedWebhook = true;
                            QString saveError;
                            if (!store_.save(originalSettings_, &saveError))
                            {
                              updateDiscordStatus();
                              status_->setText(saveError);
                              return;
                            }
                            updateDiscordStatus();
                            status_->setText(QStringLiteral("Discord 채널 연결이 완료되었습니다."));
                          });
}

void SettingsDialog::disconnectDiscord()
{
  const QString webhook = webhook_->text().trimmed();
  if (!isValidDiscordWebhook(webhook))
    return;

  connectionPollTimer_->stop();
  if (!originalSettings_.discordManagedWebhook)
  {
    webhook_->clear();
    originalSettings_.discordWebhook.clear();
    originalSettings_.discordChannelName.clear();
    QString saveError;
    if (!store_.save(originalSettings_, &saveError))
    {
      status_->setText(saveError);
      return;
    }
    updateDiscordStatus();
    status_->setText(QStringLiteral("수동 Webhook 설정을 제거했습니다."));
    return;
  }

  setBusy(true, QStringLiteral("Discord 연결을 해제하는 중..."));
  discord_.remove(webhook,
                  [this](const QString &error)
                  {
                    setBusy(false);
                    if (!error.isEmpty())
                    {
                      status_->setText(error);
                      return;
                    }
                    webhook_->clear();
                    originalSettings_.discordWebhook.clear();
                    originalSettings_.discordChannelName.clear();
                    originalSettings_.discordManagedWebhook = false;
                    QString saveError;
                    if (!store_.save(originalSettings_, &saveError))
                    {
                      status_->setText(saveError);
                      return;
                    }
                    updateDiscordStatus();
                    status_->setText(QStringLiteral("Discord 연결을 해제했습니다."));
                  });
}

void SettingsDialog::updateDiscordStatus()
{
  const bool connected = isValidDiscordWebhook(webhook_->text().trimmed());
  discordStatus_->setText(
      connected
          ? (originalSettings_.discordChannelName.isEmpty()
                 ? (originalSettings_.discordManagedWebhook ? QStringLiteral("연결됨")
                                                            : QStringLiteral("수동 Webhook 연결됨"))
                 : QStringLiteral("연결됨: %1").arg(originalSettings_.discordChannelName))
          : QStringLiteral("연결되지 않음"));
  connectDiscordButton_->setEnabled(!connected);
  disconnectDiscordButton_->setEnabled(connected);
  testDiscordButton_->setEnabled(connected);
}

void SettingsDialog::setBusy(bool busy, const QString &status)
{
  testChzzkButton_->setDisabled(busy);
  testDiscordButton_->setDisabled(busy);
  testXButton_->setDisabled(busy);
  connectDiscordButton_->setDisabled(busy);
  disconnectDiscordButton_->setDisabled(busy);
  saveButton_->setDisabled(busy);
  if (!status.isEmpty())
    status_->setText(status);
  if (!busy)
    updateDiscordStatus();
}
