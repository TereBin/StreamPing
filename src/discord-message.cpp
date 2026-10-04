#include "discord-message.hpp"

#include "chzzk-client.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace
{
QString suppressAutomaticPreview(QString message, const QString &url)
{
  if (url.isEmpty())
    return message;

  const QRegularExpression bareUrl(
      QStringLiteral("(?<!<)%1(?!>)").arg(QRegularExpression::escape(url)));
  return message.replace(bareUrl, QStringLiteral("<%1>").arg(url));
}

bool isHttpsUrl(const QString &value)
{
  const QUrl url(value);
  return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty();
}
} // namespace

QString replaceDiscordRoleTag(const QString &message, const QString &roleId)
{
  static const QRegularExpression roleIdPattern(QStringLiteral("^[0-9]{17,20}$"));
  QString rendered = message;
  const QString mention =
      roleIdPattern.match(roleId).hasMatch() ? QStringLiteral("<@&%1>").arg(roleId) : QString();
  return rendered.replace(QStringLiteral("{role}"), mention);
}

QByteArray buildDiscordMessagePayload(const QString &message, const LiveInfo &live,
                                      const DiscordEmbedOptions &options)
{
  QJsonArray parsedMentions;
  parsedMentions.append(QStringLiteral("roles"));
  parsedMentions.append(QStringLiteral("everyone"));

  QJsonObject allowedMentions;
  allowedMentions.insert(QStringLiteral("parse"), parsedMentions);

  QJsonObject embed;
  embed.insert(QStringLiteral("title"),
               (live.title.isEmpty() ? QStringLiteral("방송 시작") : live.title).left(256));
  if (isHttpsUrl(live.channelUrl))
    embed.insert(QStringLiteral("url"), live.channelUrl);
  embed.insert(QStringLiteral("color"), options.color & 0xffffff);

  QJsonArray fields;
  if (options.showChannel && !live.channelName.isEmpty())
  {
    QJsonObject channelField;
    channelField.insert(QStringLiteral("name"), QStringLiteral("채널"));
    channelField.insert(QStringLiteral("value"), live.channelName.left(1024));
    channelField.insert(QStringLiteral("inline"), true);
    fields.append(channelField);
  }
  if (options.showCategory && !live.category.isEmpty())
  {
    QJsonObject categoryField;
    categoryField.insert(QStringLiteral("name"), QStringLiteral("카테고리"));
    categoryField.insert(QStringLiteral("value"), live.category.left(1024));
    categoryField.insert(QStringLiteral("inline"), true);
    fields.append(categoryField);
  }
  if (!fields.isEmpty())
    embed.insert(QStringLiteral("fields"), fields);
  if (options.showThumbnail && isHttpsUrl(live.thumbnailUrl))
  {
    QJsonObject image;
    image.insert(QStringLiteral("url"), live.thumbnailUrl);
    embed.insert(QStringLiteral("image"), image);
  }

  QJsonArray embeds;
  embeds.append(embed);

  QJsonObject payload;
  payload.insert(QStringLiteral("content"),
                 suppressAutomaticPreview(message, live.channelUrl).left(2000));
  payload.insert(QStringLiteral("embeds"), embeds);
  payload.insert(QStringLiteral("allowed_mentions"), allowedMentions);
  return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}
