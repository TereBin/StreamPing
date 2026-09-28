#include "discord-message.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QString prependDiscordRoleMention(const QString &message, const QString &roleId)
{
  static const QRegularExpression roleIdPattern(QStringLiteral("^[0-9]{17,20}$"));
  if (!roleIdPattern.match(roleId).hasMatch())
    return message;
  return QStringLiteral("<@&%1>\n%2").arg(roleId, message);
}

QByteArray buildDiscordMessagePayload(const QString &message)
{
  QJsonArray parsedMentions;
  parsedMentions.append(QStringLiteral("roles"));
  parsedMentions.append(QStringLiteral("everyone"));

  QJsonObject allowedMentions;
  allowedMentions.insert(QStringLiteral("parse"), parsedMentions);

  QJsonObject payload;
  payload.insert(QStringLiteral("content"), message.left(2000));
  payload.insert(QStringLiteral("allowed_mentions"), allowedMentions);
  return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}
