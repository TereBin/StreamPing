#include "settings.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <windows.h>
#include <dpapi.h>

namespace
{
constexpr auto kSettingsFile = "settings.json";

QString protectWebhook(const QString &value, QString *error)
{
  if (value.isEmpty())
    return {};

  const QByteArray plain = value.toUtf8();
  DATA_BLOB input{static_cast<DWORD>(plain.size()),
                  reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
  DATA_BLOB output{};
  if (!CryptProtectData(&input, L"StreamPing Discord webhook", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output))
  {
    if (error)
      *error = QStringLiteral("Discord 연결 정보를 암호화할 수 없습니다. Windows 오류 %1")
                   .arg(GetLastError());
    return {};
  }

  const QByteArray encrypted(reinterpret_cast<const char *>(output.pbData),
                             static_cast<qsizetype>(output.cbData));
  LocalFree(output.pbData);
  return QString::fromLatin1(encrypted.toBase64());
}

QString unprotectWebhook(const QString &value)
{
  const QByteArray encrypted = QByteArray::fromBase64(value.toLatin1());
  if (encrypted.isEmpty())
    return {};

  DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                  reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()))};
  DATA_BLOB output{};
  if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &output))
    return {};

  const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(output.pbData),
                                          static_cast<qsizetype>(output.cbData));
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return plain;
}
} // namespace

QString SettingsStore::filePath() const
{
  char *path = obs_module_config_path(kSettingsFile);
  if (!path)
    return {};

  const QString result = QString::fromUtf8(path);
  bfree(path);
  return result;
}

PluginSettings SettingsStore::load() const
{
  PluginSettings settings;
  QFile file(filePath());
  if (!file.open(QIODevice::ReadOnly))
    return settings;

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return settings;

  const QJsonObject object = document.object();
  settings.discordEnabled = object.contains("discordEnabled")
                                ? object.value("discordEnabled").toBool(settings.discordEnabled)
                                : object.value("enabled").toBool(settings.discordEnabled);
  settings.channelId = object.value("channelId").toString();
  settings.discordWebhook = unprotectWebhook(object.value("discordWebhookProtected").toString());
  if (settings.discordWebhook.isEmpty())
    settings.discordWebhook = object.value("discordWebhook").toString();
  settings.discordChannelName = object.value("discordChannelName").toString();
  settings.discordRoleId = object.value("discordRoleId").toString();
  settings.discordRoleName = object.value("discordRoleName").toString();
  settings.discordManagedWebhook = object.value("discordManagedWebhook").toBool(false);
  const QString discordTemplate = object.value("discordMessageTemplate").toString();
  const QString legacyTemplate = object.value("messageTemplate").toString();
  if (!discordTemplate.isEmpty())
    settings.discordMessageTemplate = discordTemplate;
  else if (!legacyTemplate.isEmpty())
    settings.discordMessageTemplate = legacyTemplate;
  settings.xEnabled = object.value("xEnabled").toBool(settings.xEnabled);
  settings.xMessageTemplate = object.value("xMessageTemplate").toString(settings.xMessageTemplate);
  settings.initialDelaySeconds =
      object.value("initialDelaySeconds").toInt(settings.initialDelaySeconds);
  settings.pollingIntervalSeconds =
      object.value("pollingIntervalSeconds").toInt(settings.pollingIntervalSeconds);
  settings.maximumWaitSeconds =
      object.value("maximumWaitSeconds").toInt(settings.maximumWaitSeconds);
  settings.automaticUpdateChecks =
      object.value("automaticUpdateChecks").toBool(settings.automaticUpdateChecks);
  settings.lastUpdateCheckUtc = object.value("lastUpdateCheckUtc").toString();
  settings.skippedUpdateVersion = object.value("skippedUpdateVersion").toString();
  settings.lastDiscordNotifiedLiveKey = object.value("lastDiscordNotifiedLiveKey").toString();
  if (settings.lastDiscordNotifiedLiveKey.isEmpty())
    settings.lastDiscordNotifiedLiveKey = object.value("lastNotifiedLiveKey").toString();
  settings.lastXPromptedLiveKey = object.value("lastXPromptedLiveKey").toString();
  return settings;
}

bool SettingsStore::save(const PluginSettings &settings, QString *error) const
{
  QJsonObject object;
  object.insert("discordEnabled", settings.discordEnabled);
  object.insert("channelId", settings.channelId);
  QString protectionError;
  const QString protectedWebhook = protectWebhook(settings.discordWebhook, &protectionError);
  if (!settings.discordWebhook.isEmpty() && protectedWebhook.isEmpty())
  {
    if (error)
      *error = protectionError;
    return false;
  }
  object.insert("discordWebhookProtected", protectedWebhook);
  object.insert("discordChannelName", settings.discordChannelName);
  object.insert("discordRoleId", settings.discordRoleId);
  object.insert("discordRoleName", settings.discordRoleName);
  object.insert("discordManagedWebhook", settings.discordManagedWebhook);
  object.insert("discordMessageTemplate", settings.discordMessageTemplate);
  object.insert("xEnabled", settings.xEnabled);
  object.insert("xMessageTemplate", settings.xMessageTemplate);
  object.insert("initialDelaySeconds", settings.initialDelaySeconds);
  object.insert("pollingIntervalSeconds", settings.pollingIntervalSeconds);
  object.insert("maximumWaitSeconds", settings.maximumWaitSeconds);
  object.insert("automaticUpdateChecks", settings.automaticUpdateChecks);
  object.insert("lastUpdateCheckUtc", settings.lastUpdateCheckUtc);
  object.insert("skippedUpdateVersion", settings.skippedUpdateVersion);
  object.insert("lastDiscordNotifiedLiveKey", settings.lastDiscordNotifiedLiveKey);
  object.insert("lastXPromptedLiveKey", settings.lastXPromptedLiveKey);

  const QString path = filePath();
  if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath()))
  {
    if (error)
      *error = QStringLiteral("설정 폴더를 만들 수 없습니다.");
    return false;
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
  {
    if (error)
      *error = file.errorString();
    return false;
  }

  if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
  {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}
