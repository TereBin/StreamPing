#include "message-template.hpp"

#include "chzzk-client.hpp"

QString renderMessage(const QString &messageTemplate, const LiveInfo &live)
{
  QString result = messageTemplate;
  result.replace(QStringLiteral("{title}"), live.title);
  result.replace(QStringLiteral("{category}"), live.category);
  result.replace(QStringLiteral("{channel}"), live.channelName);
  result.replace(QStringLiteral("{url}"), live.channelUrl);
  return result;
}

QString renderTestMessage(const QString &messageTemplate, LiveInfo live, const QString &channelId)
{
  if (live.title.isEmpty())
    live.title = QStringLiteral("테스트 방송 제목");
  if (live.category.isEmpty())
    live.category = QStringLiteral("테스트 카테고리");
  if (live.channelName.isEmpty())
    live.channelName = QStringLiteral("테스트 채널");
  if (live.channelUrl.isEmpty())
    live.channelUrl = QStringLiteral("https://chzzk.naver.com/live/%1").arg(channelId);
  return renderMessage(messageTemplate, live);
}
