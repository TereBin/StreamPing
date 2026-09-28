#include "discord-message.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QString replaceDiscordRoleTag(const QString &message, const QString &roleId)
{
  static const QRegularExpression roleIdPattern(QStringLiteral("^[0-9]{17,20}$"));
  QString rendered = message;
  const QString mention =
      roleIdPattern.match(roleId).hasMatch() ? QStringLiteral("<@&%1>").arg(roleId) : QString();
  return rendered.replace(QStringLiteral("{role}"), mention);
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
