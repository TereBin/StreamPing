#include "chzzk-client.hpp"
#include "discord-message.hpp"
#include "message-template.hpp"
#include "settings.hpp"
#include "x-client.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace
{
int failures = 0;

void check(bool condition)
{
  if (!condition)
    ++failures;
}

void normalizesChannelIds()
{
  const QString id = QStringLiteral("ABCDEF0123456789ABCDEF0123456789");
  check(normalizeChannelId(id) == QStringLiteral("abcdef0123456789abcdef0123456789"));
  check(normalizeChannelId(QStringLiteral("https://chzzk.naver.com/live/%1?foo=bar").arg(id)) ==
        QStringLiteral("abcdef0123456789abcdef0123456789"));
  check(normalizeChannelId(QStringLiteral("not-a-channel")).isEmpty());
}

void validatesDiscordWebhooks()
{
  check(isValidDiscordWebhook(QStringLiteral("https://discord.com/api/webhooks/123/token")));
  check(isValidDiscordWebhook(QStringLiteral("https://canary.discord.com/api/webhooks/123/token")));
  check(!isValidDiscordWebhook(QStringLiteral("http://discord.com/api/webhooks/123/token")));
  check(
      !isValidDiscordWebhook(QStringLiteral("https://discord.com.example/api/webhooks/123/token")));
  check(!isValidDiscordWebhook(QStringLiteral("https://discord.com/channels/123")));
}

void buildsDiscordMessagePayloads()
{
  const QString message = QStringLiteral("<@123456789012345678> 방송 시작!");
  const QJsonDocument document = QJsonDocument::fromJson(buildDiscordMessagePayload(message));
  const QJsonObject payload = document.object();
  const QJsonArray parsedMentions = payload.value(QStringLiteral("allowed_mentions"))
                                        .toObject()
                                        .value(QStringLiteral("parse"))
                                        .toArray();

  check(payload.value(QStringLiteral("content")).toString() == message);
  check(!parsedMentions.contains(QStringLiteral("users")));
  check(parsedMentions.contains(QStringLiteral("roles")));
  check(parsedMentions.contains(QStringLiteral("everyone")));
  check(prependDiscordRoleMention(QStringLiteral("방송 시작"),
                                  QStringLiteral("123456789012345678")) ==
        QStringLiteral("<@&123456789012345678>\n방송 시작"));
  check(prependDiscordRoleMention(QStringLiteral("방송 시작"), QStringLiteral("role-name")) ==
        QStringLiteral("방송 시작"));
  check(QJsonDocument::fromJson(buildDiscordMessagePayload(QString(2001, QLatin1Char('x'))))
            .object()
            .value(QStringLiteral("content"))
            .toString()
            .size() == 2000);
}

void rendersMessageVariables()
{
  LiveInfo live;
  live.title = QStringLiteral("방송 제목");
  live.category = QStringLiteral("게임");
  live.channelName = QStringLiteral("채널 이름");
  live.channelUrl = QStringLiteral("https://example.test/live");

  check(renderMessage(QStringLiteral("{channel}|{title}|{category}|{url}"), live) ==
        QStringLiteral("채널 이름|방송 제목|게임|https://example.test/live"));

  LiveInfo offline;
  check(renderTestMessage(QStringLiteral("{channel}|{title}|{category}|{url}"), offline,
                          QStringLiteral("abcdef")) ==
        QStringLiteral("테스트 채널|테스트 방송 제목|테스트 카테고리|"
                       "https://chzzk.naver.com/live/abcdef"));
}

void buildsXComposeUrls()
{
  const QUrl url = buildXComposeUrl(QStringLiteral("방송 시작 https://example.test"));
  check(url.scheme() == QStringLiteral("https"));
  check(url.host() == QStringLiteral("twitter.com"));
  check(url.path() == QStringLiteral("/intent/tweet"));
  check(QUrlQuery(url).queryItemValue(QStringLiteral("text")) ==
        QStringLiteral("방송 시작 https://example.test"));
}
} // namespace

int main()
{
  normalizesChannelIds();
  validatesDiscordWebhooks();
  buildsDiscordMessagePayloads();
  rendersMessageVariables();
  buildsXComposeUrls();
  return failures == 0 ? 0 : 1;
}
