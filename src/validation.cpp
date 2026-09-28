#include "settings.hpp"

#include <QRegularExpression>
#include <QUrl>

QString normalizeChannelId(const QString &value)
{
  static const QRegularExpression channelIdPattern(QStringLiteral("([0-9a-fA-F]{32})"));
  const QRegularExpressionMatch match = channelIdPattern.match(value.trimmed());
  return match.hasMatch() ? match.captured(1).toLower() : QString();
}

bool isValidDiscordWebhook(const QString &value)
{
  const QUrl url(value.trimmed());
  const QString host = url.host().toLower();
  const bool discordHost =
      host == QStringLiteral("discord.com") || host.endsWith(QStringLiteral(".discord.com")) ||
      host == QStringLiteral("discordapp.com") || host.endsWith(QStringLiteral(".discordapp.com"));
  return url.isValid() && url.scheme() == QStringLiteral("https") && discordHost &&
         url.path().startsWith(QStringLiteral("/api/webhooks/"));
}
