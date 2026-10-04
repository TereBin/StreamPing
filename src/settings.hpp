#pragma once

#include <QString>

QString defaultDiscordMessageTemplate();
QString defaultXMessageTemplate();

struct PluginSettings
{
  bool discordEnabled = true;
  QString channelId;
  QString discordWebhook;
  QString discordChannelName;
  QString discordRoleId;
  QString discordRoleName;
  bool discordManagedWebhook = false;
  QString discordMessageTemplate = defaultDiscordMessageTemplate();
  int discordEmbedColor = 0x00ffa3;
  bool discordEmbedShowChannel = true;
  bool discordEmbedShowCategory = true;
  bool discordEmbedShowThumbnail = true;
  bool xEnabled = true;
  QString xMessageTemplate = defaultXMessageTemplate();
  int initialDelaySeconds = 5;
  int pollingIntervalSeconds = 5;
  int maximumWaitSeconds = 120;
  bool automaticUpdateChecks = true;
  QString lastUpdateCheckUtc;
  QString skippedUpdateVersion;
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
