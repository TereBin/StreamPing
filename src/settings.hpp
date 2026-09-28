#pragma once

#include <QString>

struct PluginSettings
{
  bool discordEnabled = true;
  QString channelId;
  QString discordWebhook;
  QString discordChannelName;
  QString discordRoleId;
  QString discordRoleName;
  bool discordManagedWebhook = false;
  QString discordMessageTemplate =
      QString::fromUtf8("🔴 {channel} 방송 시작!\n\n{title}\n카테고리: {category}\n{url}");
  bool xEnabled = true;
  QString xMessageTemplate =
      QString::fromUtf8("🔴 {channel} 방송 시작!\n\n{title}\n{category}\n{url}");
  int initialDelaySeconds = 5;
  int pollingIntervalSeconds = 5;
  int maximumWaitSeconds = 120;
  QString lastDiscordNotifiedLiveKey;
  QString lastXPromptedLiveKey;
};

class SettingsStore
{
public:
  PluginSettings load() const;
  bool save(const PluginSettings &settings, QString *error = nullptr) const;

private:
  QString filePath() const;
};

QString normalizeChannelId(const QString &value);
bool isValidDiscordWebhook(const QString &value);
