#include "chzzk-client.hpp"

#include "http-client.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

ChzzkClient::ChzzkClient(QObject *parent) : QObject(parent), http_(new HttpClient(this)) {}

void ChzzkClient::fetchLive(const QString &channelId, Callback callback)
{
  const QUrl url(QStringLiteral("https://api.chzzk.naver.com/service/v3.3/channels/%1/live-detail")
                     .arg(channelId));
  http_->get(
      url,
      [channelId, callback = std::move(callback)](const HttpResponse &response)
      {
        if (!response.error.isEmpty() || response.statusCode < 200 || response.statusCode >= 300)
        {
          callback({}, QStringLiteral("치지직 응답 오류 (%1): %2")
                           .arg(response.statusCode)
                           .arg(response.error));
          return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(response.body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
          callback({}, QStringLiteral("치지직 응답을 해석할 수 없습니다."));
          return;
        }

        const QJsonObject root = document.object();
        const QJsonObject content = root.value(QStringLiteral("content")).toObject();
        if (content.isEmpty())
        {
          callback({}, QStringLiteral("치지직 응답에 방송 정보가 없습니다."));
          return;
        }

        LiveInfo live;
        const QString statusValue = content.value(QStringLiteral("status")).toString().toUpper();
        live.isLive = statusValue == QStringLiteral("OPEN");
        live.title = content.value(QStringLiteral("liveTitle")).toString();
        live.category = content.value(QStringLiteral("liveCategoryValue")).toString();
        live.channelName = content.value(QStringLiteral("channelName")).toString();
        if (live.channelName.isEmpty())
          live.channelName = content.value(QStringLiteral("channel"))
                                 .toObject()
                                 .value(QStringLiteral("channelName"))
                                 .toString();
        live.channelUrl = QStringLiteral("https://chzzk.naver.com/live/%1").arg(channelId);

        live.liveKey = content.value(QStringLiteral("chatChannelId")).toString();
        if (live.liveKey.isEmpty())
          live.liveKey = QString::number(content.value(QStringLiteral("liveId")).toInteger());
        if (live.liveKey == QStringLiteral("0"))
          live.liveKey.clear();
        if (live.liveKey.isEmpty())
          live.liveKey = content.value(QStringLiteral("openDate")).toString();

        callback(live, {});
      });
}
