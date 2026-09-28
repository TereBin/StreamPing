#pragma once

#include "chzzk-client.hpp"
#include "discord-client.hpp"
#include "discord-connection-client.hpp"
#include "settings.hpp"
#include "x-client.hpp"

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class SettingsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit SettingsDialog(const SettingsStore &store, QWidget *parent = nullptr);

private:
  PluginSettings formSettings() const;
  bool validate(PluginSettings *settings);
  void saveAndAccept();
  void testChzzk();
  void testDiscordMessage();
  void testX();
  void connectDiscord();
  void selectDiscordRole();
  void clearDiscordRole();
  void pollDiscordConnection();
  void disconnectDiscord();
  void updateDiscordStatus();
  void setBusy(bool busy, const QString &status = {});
  void prepareTestMessage(QPlainTextEdit *editor, const QString &preparingStatus,
                          std::function<void(const QString &message, bool usedFallback)> callback);

  SettingsStore store_;
  PluginSettings originalSettings_;
  ChzzkClient chzzk_;
  DiscordClient discord_;
  DiscordConnectionClient discordConnection_;
  XClient x_;
  QCheckBox *discordEnabled_;
  QLineEdit *channelId_;
  QLineEdit *webhook_;
  QPlainTextEdit *discordMessageTemplate_;
  QCheckBox *xEnabled_;
  QPlainTextEdit *xMessageTemplate_;
  QSpinBox *initialDelay_;
  QSpinBox *pollingInterval_;
  QSpinBox *maximumWait_;
  QPushButton *testChzzkButton_;
  QPushButton *testDiscordButton_;
  QPushButton *testXButton_;
  QPushButton *connectDiscordButton_;
  QPushButton *disconnectDiscordButton_;
  QPushButton *selectDiscordRoleButton_;
  QPushButton *clearDiscordRoleButton_;
  QPushButton *saveButton_;
  QLabel *status_;
  QLabel *discordStatus_;
  QTimer *connectionPollTimer_;
  QString connectionSessionId_;
  QString connectionPollToken_;
  int connectionPollSeconds_ = 0;
  bool selectingDiscordRole_ = false;
};
