#include "discord-message.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QByteArray buildDiscordMessagePayload(const QString &message)
{
  QJsonArray parsedMentions;
  parsedMentions.append(QStringLiteral("users"));
  parsedMentions.append(QStringLiteral("roles"));
  parsedMentions.append(QStringLiteral("everyone"));

  QJsonObject allowedMentions;
  allowedMentions.insert(QStringLiteral("parse"), parsedMentions);

  QJsonObject payload;
  payload.insert(QStringLiteral("content"), message.left(2000));
  payload.insert(QStringLiteral("allowed_mentions"), allowedMentions);
  return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}
