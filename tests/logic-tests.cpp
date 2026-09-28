#include "chzzk-client.hpp"
#include "message-template.hpp"
#include "settings.hpp"
#include "x-client.hpp"

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
  rendersMessageVariables();
  buildsXComposeUrls();
  return failures == 0 ? 0 : 1;
}
