#include "chzzk-client.hpp"
#include "discord-message.hpp"
#include "message-template.hpp"
#include "settings.hpp"
#include "update-checker.hpp"
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
  LiveInfo live;
  live.title = QStringLiteral("현재 방송 제목");
  live.category = QStringLiteral("게임");
  live.channelName = QStringLiteral("채널 이름");
  live.channelUrl = QStringLiteral("https://chzzk.naver.com/live/abcdef");
  live.thumbnailUrl = QStringLiteral("https://example.test/current-thumbnail.jpg");
  const QString message =
      QStringLiteral("<@123456789012345678> 방송 시작! %1").arg(live.channelUrl);
  const QJsonDocument document = QJsonDocument::fromJson(buildDiscordMessagePayload(message, live));
  const QJsonObject payload = document.object();
  const QJsonArray parsedMentions = payload.value(QStringLiteral("allowed_mentions"))
                                        .toObject()
                                        .value(QStringLiteral("parse"))
                                        .toArray();

  check(payload.value(QStringLiteral("content")).toString() ==
        QStringLiteral("<@123456789012345678> 방송 시작! <%1>").arg(live.channelUrl));
  check(!parsedMentions.contains(QStringLiteral("users")));
  check(parsedMentions.contains(QStringLiteral("roles")));
  check(parsedMentions.contains(QStringLiteral("everyone")));
  const QJsonObject embed = payload.value(QStringLiteral("embeds")).toArray().first().toObject();
  check(embed.value(QStringLiteral("title")).toString() == live.title);
  check(embed.value(QStringLiteral("url")).toString() == live.channelUrl);
  check(embed.value(QStringLiteral("fields")).toArray().size() == 2);
  check(embed.value(QStringLiteral("image")).toObject().value(QStringLiteral("url")).toString() ==
        live.thumbnailUrl);

  const DiscordEmbedOptions minimalOptions{0x123456, false, false, false};
  const QJsonObject minimalEmbed =
      QJsonDocument::fromJson(buildDiscordMessagePayload(message, live, minimalOptions))
          .object()
          .value(QStringLiteral("embeds"))
          .toArray()
          .first()
          .toObject();
  check(minimalEmbed.value(QStringLiteral("color")).toInt() == 0x123456);
  check(!minimalEmbed.contains(QStringLiteral("fields")));
  check(!minimalEmbed.contains(QStringLiteral("image")));
  check(replaceDiscordRoleTag(QStringLiteral("방송 {role} 시작 {role}"),
                              QStringLiteral("123456789012345678")) ==
        QStringLiteral("방송 <@&123456789012345678> 시작 <@&123456789012345678>"));
  check(replaceDiscordRoleTag(QStringLiteral("{role} 방송 시작"), QStringLiteral("role-name")) ==
        QStringLiteral(" 방송 시작"));
  check(replaceDiscordRoleTag(QStringLiteral("방송 시작"), QStringLiteral("123456789012345678")) ==
        QStringLiteral("방송 시작"));
  check(replaceDiscordRoleTagForPreview(QStringLiteral("{role} 방송 시작"),
                                        QStringLiteral("방송 알림")) ==
        QStringLiteral("@방송 알림 방송 시작"));
  check(replaceDiscordRoleTagForPreview(QStringLiteral("{role} 방송 시작"), QString()) ==
        QStringLiteral(" 방송 시작"));
  check(QJsonDocument::fromJson(buildDiscordMessagePayload(QString(2001, QLatin1Char('x')), live))
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

void handlesUpdateVersions()
{
  check(compareVersions(QStringLiteral("0.4.2"), QStringLiteral("0.5.0")) < 0);
  check(compareVersions(QStringLiteral("1.0.0"), QStringLiteral("0.9.9")) > 0);
  check(compareVersions(QStringLiteral("v0.4.2"), QStringLiteral("0.4.2")) == 0);

  const QByteArray manifest = R"({
    "schemaVersion": 1,
    "latestVersion": "0.5.0",
    "minimumSupportedVersion": "0.4.0",
    "urgency": "recommended",
    "title": "새 업데이트",
    "message": "기능이 개선되었습니다.",
    "downloadUrl": "https://github.com/TereBin/StreamPing-Release/releases/latest"
  })";
  UpdateInfo update;
  check(parseUpdateManifest(manifest, QStringLiteral("v0.5.0"), &update));
  check(update.latestVersion == QStringLiteral("0.5.0"));
  check(update.minimumSupportedVersion == QStringLiteral("0.4.0"));
  check(update.urgency == UpdateUrgency::Recommended);
  check(effectiveUpdateUrgency(QStringLiteral("0.3.9"), update) == UpdateUrgency::Required);
  check(effectiveUpdateUrgency(QStringLiteral("0.4.0"), update) == UpdateUrgency::Recommended);
  check(!parseUpdateManifest(manifest, QStringLiteral("0.5.1"), &update));
}
} // namespace

int main()
{
  normalizesChannelIds();
  validatesDiscordWebhooks();
  buildsDiscordMessagePayloads();
  rendersMessageVariables();
  buildsXComposeUrls();
  handlesUpdateVersions();
  return failures == 0 ? 0 : 1;
}
