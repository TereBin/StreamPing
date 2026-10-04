#include "settings-dialog.hpp"

#include "discord-message.hpp"
#include "message-template.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QSysInfo>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace
{
constexpr int kMessageEditorHeight = 140;
const QUrl kIssuesUrl(QStringLiteral("https://github.com/TereBin/StreamPing/issues"));
const QUrl kDiscordSupportUrl(QStringLiteral("https://discordapp.com/users/537256771501424640"));
} // namespace

SettingsDialog::SettingsDialog(const SettingsStore &store, QWidget *parent)
    : QDialog(parent), store_(store), originalSettings_(store_.load()), chzzk_(this),
      discord_(this), discordConnection_(this), updateChecker_(this)
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
  selectDiscordRoleButton_ = new QPushButton(QStringLiteral("역할 선택"), this);
  clearDiscordRoleButton_ = new QPushButton(QStringLiteral("역할 해제"), this);
  auto *discordConnectionLayout = new QHBoxLayout;
  discordConnectionLayout->addWidget(connectDiscordButton_);
  discordConnectionLayout->addWidget(disconnectDiscordButton_);
  discordConnectionLayout->addWidget(selectDiscordRoleButton_);
  discordConnectionLayout->addWidget(clearDiscordRoleButton_);
  discordConnectionLayout->addStretch();

  discordMessageTemplate_ = new QPlainTextEdit(originalSettings_.discordMessageTemplate, this);
  discordMessageTemplate_->setFixedHeight(kMessageEditorHeight);
  discordMessageTemplate_->setPlaceholderText(
      QStringLiteral("사용 가능: {role}, {title}, {category}, {channel}, {url}"));
  discordMessageTemplate_->setToolTip(
      QStringLiteral("{role}은 선택한 역할 멘션으로 바뀝니다. 역할을 선택하지 않으면 빈 "
                     "문자열이 됩니다. @everyone과 @here도 사용할 수 있습니다."));
  discordEmbedColor_ = originalSettings_.discordEmbedColor;
  discordEmbedColorButton_ = new QPushButton(this);
  discordEmbedColorButton_->setToolTip(QStringLiteral("Discord 임베드 왼쪽 강조 색상"));
  discordEmbedShowChannel_ = new QCheckBox(QStringLiteral("채널"), this);
  discordEmbedShowChannel_->setChecked(originalSettings_.discordEmbedShowChannel);
  discordEmbedShowCategory_ = new QCheckBox(QStringLiteral("카테고리"), this);
  discordEmbedShowCategory_->setChecked(originalSettings_.discordEmbedShowCategory);
  discordEmbedShowThumbnail_ = new QCheckBox(QStringLiteral("썸네일"), this);
  discordEmbedShowThumbnail_->setChecked(originalSettings_.discordEmbedShowThumbnail);
  updateDiscordEmbedColorButton();

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

  testChzzkButton_ = new QPushButton(QStringLiteral("치지직 확인"), this);
  testDiscordButton_ = new QPushButton(QStringLiteral("알림 메시지 테스트"), this);
  testXButton_ = new QPushButton(QStringLiteral("X 작성 화면 테스트"), this);
  previewDiscordButton_ = new QPushButton(QStringLiteral("미리보기"), this);
  previewXButton_ = new QPushButton(QStringLiteral("미리보기"), this);
  resetDiscordTemplateButton_ = new QPushButton(QStringLiteral("메시지 초기화"), this);
  resetXTemplateButton_ = new QPushButton(QStringLiteral("메시지 초기화"), this);
  copyDiagnosticsButton_ = new QPushButton(QStringLiteral("진단 정보 복사"), this);

  auto *chzzkForm = new QFormLayout;
  chzzkForm->addRow(QStringLiteral("채널"), channelId_);
  chzzkForm->addRow(QString(), testChzzkButton_);
  auto *chzzkGroup = new QGroupBox(QStringLiteral("치지직"), this);
  chzzkGroup->setLayout(chzzkForm);

  auto *discordForm = new QFormLayout;
  discordForm->addRow(QString(), discordEnabled_);
  discordForm->addRow(QStringLiteral("상태"), discordStatus_);
  discordForm->addRow(QString(), discordConnectionLayout);
  discordForm->addRow(QStringLiteral("수동 Webhook (고급)"), webhook_);
  discordForm->addRow(QStringLiteral("메시지"), discordMessageTemplate_);
  auto *embedOptions = new QHBoxLayout;
  embedOptions->addWidget(discordEmbedColorButton_);
  embedOptions->addWidget(discordEmbedShowChannel_);
  embedOptions->addWidget(discordEmbedShowCategory_);
  embedOptions->addWidget(discordEmbedShowThumbnail_);
  embedOptions->addStretch();
  discordForm->addRow(QStringLiteral("임베드"), embedOptions);
  auto *discordActions = new QHBoxLayout;
  discordActions->addWidget(previewDiscordButton_);
  discordActions->addWidget(testDiscordButton_);
  discordActions->addWidget(resetDiscordTemplateButton_);
  discordActions->addStretch();
  discordForm->addRow(QString(), discordActions);
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
  auto *xActions = new QHBoxLayout;
  xActions->addWidget(previewXButton_);
  xActions->addWidget(testXButton_);
  xActions->addWidget(resetXTemplateButton_);
  xActions->addStretch();
  xForm->addRow(QString(), xActions);
  auto *xGroup = new QGroupBox(QStringLiteral("X"), this);
  xGroup->setLayout(xForm);

  automaticUpdateChecks_ = new QCheckBox(QStringLiteral("OBS 시작 시 새 버전 자동 확인"), this);
  automaticUpdateChecks_->setChecked(originalSettings_.automaticUpdateChecks);
  checkUpdatesButton_ = new QPushButton(QStringLiteral("지금 확인"), this);
  auto *updateActions = new QHBoxLayout;
  updateActions->addWidget(automaticUpdateChecks_);
  updateActions->addWidget(checkUpdatesButton_);
  updateActions->addStretch();
  auto *updateForm = new QFormLayout;
  updateForm->addRow(QStringLiteral("현재 버전"),
                     new QLabel(QString::fromUtf8(STREAMPING_VERSION), this));
  updateForm->addRow(QString(), updateActions);
  auto *updateGroup = new QGroupBox(QStringLiteral("업데이트"), this);
  updateGroup->setLayout(updateForm);

  auto *issuesButton = new QPushButton(QStringLiteral("GitHub Issues"), this);
  auto *discordSupportButton = new QPushButton(QStringLiteral("Discord 문의"), this);
  auto *supportActions = new QHBoxLayout;
  supportActions->addWidget(copyDiagnosticsButton_);
  supportActions->addWidget(issuesButton);
  supportActions->addWidget(discordSupportButton);
  supportActions->addStretch();
  auto *supportGroup = new QGroupBox(QStringLiteral("지원"), this);
  supportGroup->setLayout(supportActions);

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
  layout->addWidget(updateGroup);
  layout->addWidget(supportGroup);
  layout->addWidget(timingToggle);
  layout->addWidget(timingPanel);
  layout->addWidget(status_);
  layout->addWidget(buttons);

  connectionPollTimer_ = new QTimer(this);
  connectionPollTimer_->setSingleShot(true);
  updateDiscordStatus();

  connect(testChzzkButton_, &QPushButton::clicked, this, &SettingsDialog::testChzzk);
  connect(testDiscordButton_, &QPushButton::clicked, this, &SettingsDialog::testDiscordMessage);
  connect(testXButton_, &QPushButton::clicked, this, &SettingsDialog::testX);
  connect(previewDiscordButton_, &QPushButton::clicked, this,
          &SettingsDialog::previewDiscordMessage);
  connect(previewXButton_, &QPushButton::clicked, this, &SettingsDialog::previewXMessage);
  connect(resetDiscordTemplateButton_, &QPushButton::clicked, this,
          &SettingsDialog::resetDiscordTemplate);
  connect(resetXTemplateButton_, &QPushButton::clicked, this, &SettingsDialog::resetXTemplate);
  connect(discordEmbedColorButton_, &QPushButton::clicked, this,
          &SettingsDialog::chooseDiscordEmbedColor);
  connect(copyDiagnosticsButton_, &QPushButton::clicked, this, &SettingsDialog::copyDiagnostics);
  connect(issuesButton, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(kIssuesUrl); });
  connect(discordSupportButton, &QPushButton::clicked, this,
          [] { QDesktopServices::openUrl(kDiscordSupportUrl); });
  connect(checkUpdatesButton_, &QPushButton::clicked, this, &SettingsDialog::checkUpdates);
  connect(connectDiscordButton_, &QPushButton::clicked, this, &SettingsDialog::connectDiscord);
  connect(disconnectDiscordButton_, &QPushButton::clicked, this,
          &SettingsDialog::disconnectDiscord);
  connect(selectDiscordRoleButton_, &QPushButton::clicked, this,
          &SettingsDialog::selectDiscordRole);
  connect(clearDiscordRoleButton_, &QPushButton::clicked, this, &SettingsDialog::clearDiscordRole);
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
  {
    settings.discordChannelName.clear();
    settings.discordRoleId.clear();
    settings.discordRoleName.clear();
    settings.discordManagedWebhook = false;
  }
  settings.discordMessageTemplate = discordMessageTemplate_->toPlainText().trimmed();
  settings.discordEmbedColor = discordEmbedColor_;
  settings.discordEmbedShowChannel = discordEmbedShowChannel_->isChecked();
  settings.discordEmbedShowCategory = discordEmbedShowCategory_->isChecked();
  settings.discordEmbedShowThumbnail = discordEmbedShowThumbnail_->isChecked();
  settings.xEnabled = xEnabled_->isChecked();
  settings.xMessageTemplate = xMessageTemplate_->toPlainText().trimmed();
  settings.initialDelaySeconds = initialDelay_->value();
  settings.pollingIntervalSeconds = pollingInterval_->value();
  settings.maximumWaitSeconds = maximumWait_->value();
  settings.automaticUpdateChecks = automaticUpdateChecks_->isChecked();
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
      [this, webhook](const QString &message, const LiveInfo &live, bool usedFallback)
      {
        const PluginSettings settings = formSettings();
        const DiscordEmbedOptions embedOptions{
            settings.discordEmbedColor, settings.discordEmbedShowChannel,
            settings.discordEmbedShowCategory, settings.discordEmbedShowThumbnail};
        discord_.send(webhook, replaceDiscordRoleTag(message, originalSettings_.discordRoleId),
                      live, embedOptions,
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

void SettingsDialog::previewDiscordMessage()
{
  prepareTestMessage(
      discordMessageTemplate_, QStringLiteral("Discord 미리보기를 준비하는 중..."),
      [this](const QString &message, const LiveInfo &live, bool usedFallback)
      {
        const PluginSettings settings = formSettings();
        const DiscordEmbedOptions options{
            settings.discordEmbedColor, settings.discordEmbedShowChannel,
            settings.discordEmbedShowCategory, settings.discordEmbedShowThumbnail};
        const QJsonObject payload =
            QJsonDocument::fromJson(
                buildDiscordMessagePayload(
                    replaceDiscordRoleTagForPreview(message, originalSettings_.discordRoleName),
                    live, options))
                .object();
        const QJsonObject embed =
            payload.value(QStringLiteral("embeds")).toArray().first().toObject();
        QStringList lines;
        lines << QStringLiteral("메시지") << QStringLiteral("------")
              << payload.value(QStringLiteral("content")).toString() << QString()
              << QStringLiteral("임베드") << QStringLiteral("------")
              << QStringLiteral("제목: %1").arg(embed.value(QStringLiteral("title")).toString())
              << QStringLiteral("색상: #%1")
                     .arg(options.color & 0xffffff, 6, 16, QLatin1Char('0'))
                     .toUpper();
        const QJsonArray fields = embed.value(QStringLiteral("fields")).toArray();
        for (const QJsonValue &value : fields)
        {
          const QJsonObject field = value.toObject();
          lines << QStringLiteral("%1: %2").arg(field.value(QStringLiteral("name")).toString(),
                                                field.value(QStringLiteral("value")).toString());
        }
        lines << QStringLiteral("썸네일: %1")
                     .arg(embed.contains(QStringLiteral("image")) ? QStringLiteral("표시")
                                                                  : QStringLiteral("숨김"));
        if (usedFallback)
          lines << QString()
                << QStringLiteral(
                       "치지직 조회에 실패하거나 방송 정보가 없어 예시 값을 사용했습니다.");
        setBusy(false);
        showTextPreview(QStringLiteral("Discord 메시지 미리보기"), lines.join(QLatin1Char('\n')));
      });
}

void SettingsDialog::testX()
{
  prepareTestMessage(
      xMessageTemplate_, QStringLiteral("치지직 정보를 반영해 X 작성 화면을 준비하는 중..."),
      [this](const QString &message, const LiveInfo &, bool usedFallback)
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

void SettingsDialog::previewXMessage()
{
  prepareTestMessage(
      xMessageTemplate_, QStringLiteral("X 미리보기를 준비하는 중..."),
      [this](const QString &message, const LiveInfo &, bool usedFallback)
      {
        QString preview = message;
        if (usedFallback)
          preview += QStringLiteral(
              "\n\n---\n치지직 조회에 실패하거나 방송 정보가 없어 예시 값을 사용했습니다.");
        setBusy(false);
        showTextPreview(QStringLiteral("X 메시지 미리보기"), preview);
      });
}

void SettingsDialog::resetDiscordTemplate()
{
  if (QMessageBox::question(this, QStringLiteral("메시지 초기화"),
                            QStringLiteral("Discord 메시지를 기본값으로 되돌릴까요?")) ==
      QMessageBox::Yes)
    discordMessageTemplate_->setPlainText(defaultDiscordMessageTemplate());
}

void SettingsDialog::resetXTemplate()
{
  if (QMessageBox::question(this, QStringLiteral("메시지 초기화"),
                            QStringLiteral("X 메시지를 기본값으로 되돌릴까요?")) ==
      QMessageBox::Yes)
    xMessageTemplate_->setPlainText(defaultXMessageTemplate());
}

void SettingsDialog::chooseDiscordEmbedColor()
{
  const QColor color = QColorDialog::getColor(QColor::fromRgb(discordEmbedColor_), this,
                                              QStringLiteral("Discord 임베드 색상"));
  if (!color.isValid())
    return;
  discordEmbedColor_ = color.rgb() & 0xffffff;
  updateDiscordEmbedColorButton();
}

void SettingsDialog::copyDiagnostics()
{
  const PluginSettings settings = formSettings();
  const QString recentStatus = status_->text().trimmed();
  const auto enabledText = [](bool enabled)
  { return enabled ? QStringLiteral("켜짐") : QStringLiteral("꺼짐"); };
  const auto visibleText = [](bool visible)
  { return visible ? QStringLiteral("표시") : QStringLiteral("숨김"); };
  const QString color = QStringLiteral("#%1")
                            .arg(settings.discordEmbedColor & 0xffffff, 6, 16, QLatin1Char('0'))
                            .toUpper();
  QStringList lines;
  lines << QStringLiteral("StreamPing 진단 정보")
        << QStringLiteral("생성 시각: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        << QStringLiteral("StreamPing: %1").arg(QString::fromUtf8(STREAMPING_VERSION))
        << QStringLiteral("OBS: %1").arg(QString::fromUtf8(obs_get_version_string()))
        << QStringLiteral("Windows: %1").arg(QSysInfo::prettyProductName())
        << QStringLiteral("치지직 채널: %1")
               .arg(settings.channelId.isEmpty() ? QStringLiteral("없음")
                                                 : QStringLiteral("설정됨"))
        << QStringLiteral("Discord 자동 알림: %1").arg(enabledText(settings.discordEnabled))
        << QStringLiteral("Discord 연결: %1")
               .arg(isValidDiscordWebhook(settings.discordWebhook) ? QStringLiteral("연결됨")
                                                                   : QStringLiteral("연결 안 됨"))
        << QStringLiteral("Discord 연결 방식: %1")
               .arg(settings.discordManagedWebhook ? QStringLiteral("간편 연결")
                                                   : QStringLiteral("수동 또는 없음"))
        << QStringLiteral("Discord 역할: %1")
               .arg(settings.discordRoleName.isEmpty() ? QStringLiteral("없음")
                                                       : settings.discordRoleName)
        << QStringLiteral("Discord 임베드: %1 / 채널 %2 / 카테고리 %3 / 썸네일 %4")
               .arg(color, visibleText(settings.discordEmbedShowChannel),
                    visibleText(settings.discordEmbedShowCategory),
                    visibleText(settings.discordEmbedShowThumbnail))
        << QStringLiteral("X 작성 화면: %1").arg(enabledText(settings.xEnabled))
        << QStringLiteral("LIVE 확인: 첫 지연 %1초 / 간격 %2초 / 최대 %3초")
               .arg(settings.initialDelaySeconds)
               .arg(settings.pollingIntervalSeconds)
               .arg(settings.maximumWaitSeconds)
        << QStringLiteral("자동 업데이트 확인: %1").arg(enabledText(settings.automaticUpdateChecks))
        << QStringLiteral("최근 상태: %1")
               .arg(recentStatus.isEmpty() ? QStringLiteral("없음") : recentStatus);
  const QString diagnostics = lines.join(QLatin1Char('\n'));
  QGuiApplication::clipboard()->setText(diagnostics);
  status_->setText(QStringLiteral("민감한 연결 주소를 제외한 진단 정보를 복사했습니다."));
}

void SettingsDialog::checkUpdates()
{
  setBusy(true, QStringLiteral("최신 StreamPing 버전을 확인하는 중..."));
  updateChecker_.check(
      QString::fromUtf8(STREAMPING_VERSION),
      [this](const UpdateCheckResult &result)
      {
        setBusy(false);
        if (!result.error.isEmpty())
        {
          status_->setText(result.error);
          return;
        }
        if (!result.updateAvailable)
        {
          status_->setText(QStringLiteral("현재 최신 버전을 사용 중입니다."));
          return;
        }

        QMessageBox messageBox(this);
        messageBox.setWindowTitle(result.update.urgency == UpdateUrgency::Required
                                      ? QStringLiteral("StreamPing 필수 업데이트")
                                      : (result.update.urgency == UpdateUrgency::Recommended
                                             ? QStringLiteral("StreamPing 권장 업데이트")
                                             : QStringLiteral("StreamPing 업데이트")));
        messageBox.setIcon(result.update.urgency == UpdateUrgency::Required
                               ? QMessageBox::Critical
                               : (result.update.urgency == UpdateUrgency::Recommended
                                      ? QMessageBox::Warning
                                      : QMessageBox::Information));
        messageBox.setText(result.update.title.isEmpty()
                               ? QStringLiteral("StreamPing %1 버전이 배포되었습니다.")
                                     .arg(result.update.latestVersion)
                               : result.update.title);
        messageBox.setInformativeText(QStringLiteral("현재 버전: %1\n최신 버전: %2\n\n%3")
                                          .arg(QString::fromUtf8(STREAMPING_VERSION),
                                               result.update.latestVersion, result.update.message));
        auto *downloadButton =
            messageBox.addButton(QStringLiteral("다운로드 페이지 열기"), QMessageBox::AcceptRole);
        messageBox.addButton(QStringLiteral("닫기"), QMessageBox::RejectRole);
        messageBox.exec();
        if (messageBox.clickedButton() == downloadButton)
          QDesktopServices::openUrl(QUrl(result.update.downloadUrl));
      });
}

void SettingsDialog::prepareTestMessage(
    QPlainTextEdit *editor, const QString &preparingStatus,
    std::function<void(const QString &message, const LiveInfo &live, bool usedFallback)> callback)
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
  chzzk_.fetchLive(
      channelId,
      [channelId, messageTemplate,
       callback = std::move(callback)](const LiveInfo &live, const QString &lookupError) mutable
      {
        const bool usedFallback = !lookupError.isEmpty() || live.title.isEmpty() ||
                                  live.category.isEmpty() || live.channelName.isEmpty();
        LiveInfo displayLive = live;
        if (displayLive.title.isEmpty())
          displayLive.title = QStringLiteral("테스트 방송 제목");
        if (displayLive.category.isEmpty())
          displayLive.category = QStringLiteral("테스트 카테고리");
        if (displayLive.channelName.isEmpty())
          displayLive.channelName = QStringLiteral("테스트 채널");
        if (displayLive.channelUrl.isEmpty())
          displayLive.channelUrl = QStringLiteral("https://chzzk.naver.com/live/%1").arg(channelId);
        callback(renderMessage(messageTemplate, displayLive), displayLive, usedFallback);
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
  selectDiscordRoleButton_->setDisabled(true);
  clearDiscordRoleButton_->setDisabled(true);
  selectingDiscordRole_ = false;
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

void SettingsDialog::selectDiscordRole()
{
  const QString webhook = webhook_->text().trimmed();
  if (!originalSettings_.discordManagedWebhook || !isValidDiscordWebhook(webhook))
  {
    QMessageBox::information(
        this, QStringLiteral("Discord 역할 선택"),
        QStringLiteral("역할 선택은 Discord 간편 연결로 만든 Webhook에서 사용할 수 있습니다."));
    return;
  }

  setBusy(true, QStringLiteral("Discord 역할 선택을 준비하는 중..."));
  discordConnection_.startRoleSelection(
      webhook,
      [this](const QString &sessionId, const QString &pollToken, const QString &error)
      {
        if (!error.isEmpty())
        {
          setBusy(false);
          status_->setText(error);
          return;
        }
        connectionSessionId_ = sessionId;
        connectionPollToken_ = pollToken;
        connectionPollSeconds_ = 0;
        selectingDiscordRole_ = true;
        status_->setText(
            QStringLiteral("연결된 Discord 채널에서 /streamping-role 명령으로 역할을 선택해 "
                           "주세요."));
        connectionPollTimer_->start(1000);
      });
}

void SettingsDialog::clearDiscordRole()
{
  connectionPollTimer_->stop();
  selectingDiscordRole_ = false;
  originalSettings_.discordRoleId.clear();
  originalSettings_.discordRoleName.clear();
  QString saveError;
  if (!store_.save(originalSettings_, &saveError))
  {
    status_->setText(saveError);
    return;
  }
  updateDiscordStatus();
  status_->setText(QStringLiteral("Discord 역할 멘션을 해제했습니다."));
}

void SettingsDialog::pollDiscordConnection()
{
  connectionPollSeconds_ += 2;
  discordConnection_.poll(
      connectionSessionId_, connectionPollToken_,
      [this](bool pending, const QString &webhookUrl, const QString &channelName,
             const QString &roleId, const QString &roleName, const QString &error)
      {
        if (pending && connectionPollSeconds_ < 300)
        {
          connectionPollTimer_->start(2000);
          return;
        }
        if (pending)
        {
          if (selectingDiscordRole_)
          {
            selectingDiscordRole_ = false;
            setBusy(false);
          }
          updateDiscordStatus();
          status_->setText(QStringLiteral("Discord 연결 승인 시간이 만료되었습니다."));
          return;
        }
        if (!error.isEmpty())
        {
          if (selectingDiscordRole_)
          {
            selectingDiscordRole_ = false;
            setBusy(false);
          }
          updateDiscordStatus();
          status_->setText(error);
          return;
        }

        if (selectingDiscordRole_)
        {
          selectingDiscordRole_ = false;
          setBusy(false);
          originalSettings_.discordRoleId = roleId;
          originalSettings_.discordRoleName = roleName;
          QString saveError;
          if (!store_.save(originalSettings_, &saveError))
          {
            updateDiscordStatus();
            status_->setText(saveError);
            return;
          }
          updateDiscordStatus();
          status_->setText(QStringLiteral("Discord 역할 선택이 완료되었습니다. 메시지의 {role} "
                                          "위치에서 멘션됩니다."));
          return;
        }

        webhook_->setText(webhookUrl);
        originalSettings_.discordWebhook = webhookUrl;
        originalSettings_.discordChannelName = channelName;
        originalSettings_.discordRoleId.clear();
        originalSettings_.discordRoleName.clear();
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
    originalSettings_.discordRoleId.clear();
    originalSettings_.discordRoleName.clear();
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
                    originalSettings_.discordRoleId.clear();
                    originalSettings_.discordRoleName.clear();
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
                 : (originalSettings_.discordRoleName.isEmpty()
                        ? QStringLiteral("연결됨: %1").arg(originalSettings_.discordChannelName)
                        : QStringLiteral("연결됨: %1 / 역할 @%2")
                              .arg(originalSettings_.discordChannelName,
                                   originalSettings_.discordRoleName)))
          : QStringLiteral("연결되지 않음"));
  connectDiscordButton_->setEnabled(!connected);
  disconnectDiscordButton_->setEnabled(connected);
  selectDiscordRoleButton_->setEnabled(connected && originalSettings_.discordManagedWebhook);
  clearDiscordRoleButton_->setEnabled(connected && !originalSettings_.discordRoleId.isEmpty());
  testDiscordButton_->setEnabled(connected);
}

void SettingsDialog::updateDiscordEmbedColorButton()
{
  const QString hex =
      QStringLiteral("#%1").arg(discordEmbedColor_ & 0xffffff, 6, 16, QLatin1Char('0')).toUpper();
  const QColor color = QColor::fromRgb(discordEmbedColor_);
  const QColor textColor = color.lightness() < 128 ? Qt::white : Qt::black;
  discordEmbedColorButton_->setText(hex);
  discordEmbedColorButton_->setStyleSheet(
      QStringLiteral("QPushButton { background-color: %1; color: %2; }")
          .arg(hex, textColor.name()));
}

void SettingsDialog::showTextPreview(const QString &title, const QString &text)
{
  QDialog dialog(this);
  dialog.setWindowTitle(title);
  dialog.resize(520, 420);
  auto *editor = new QPlainTextEdit(text, &dialog);
  editor->setReadOnly(true);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("닫기"));
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(editor);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.exec();
}

void SettingsDialog::setBusy(bool busy, const QString &status)
{
  testChzzkButton_->setDisabled(busy);
  testDiscordButton_->setDisabled(busy);
  testXButton_->setDisabled(busy);
  previewDiscordButton_->setDisabled(busy);
  previewXButton_->setDisabled(busy);
  resetDiscordTemplateButton_->setDisabled(busy);
  resetXTemplateButton_->setDisabled(busy);
  discordEmbedColorButton_->setDisabled(busy);
  copyDiagnosticsButton_->setDisabled(busy);
  connectDiscordButton_->setDisabled(busy);
  disconnectDiscordButton_->setDisabled(busy);
  selectDiscordRoleButton_->setDisabled(busy);
  clearDiscordRoleButton_->setDisabled(busy);
  checkUpdatesButton_->setDisabled(busy);
  saveButton_->setDisabled(busy);
  if (!status.isEmpty())
    status_->setText(status);
  if (!busy)
    updateDiscordStatus();
}
