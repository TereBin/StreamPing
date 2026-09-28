#include "discord-connection-client.hpp"

#include "http-client.hpp"
#include "settings.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QUrl>

namespace
{
QString responseError(const HttpResponse &response)
{
  if (!response.error.isEmpty())
    return response.error;

  const QJsonDocument document = QJsonDocument::fromJson(response.body);
  const QString detail = document.object().value(QStringLiteral("error")).toString();
  return detail.isEmpty() ? QStringLiteral("연결 서비스 응답 오류 (%1)").arg(response.statusCode)
                          : detail;
}
} // namespace

DiscordConnectionClient::DiscordConnectionClient(QObject *parent)
    : QObject(parent), serviceUrl_(QString::fromUtf8(STREAMPING_CONNECT_SERVICE_URL)),
      http_(new HttpClient(this))
{
  while (serviceUrl_.endsWith(QLatin1Char('/')))
    serviceUrl_.chop(1);
}

bool DiscordConnectionClient::isConfigured() const
{
  const QUrl url(serviceUrl_);
  return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty();
}

void DiscordConnectionClient::start(StartCallback callback)
{
  if (!isConfigured())
  {
    callback({}, {}, {}, QStringLiteral("이 빌드에는 Discord 연결 서비스가 설정되지 않았습니다."));
    return;
  }

  http_->postJson(QUrl(serviceUrl_ + QStringLiteral("/v1/discord/sessions")), QByteArray("{}"),
                  [callback = std::move(callback)](const HttpResponse &response)
                  {
                    if (!response.error.isEmpty() || response.statusCode != 201)
                    {
                      callback({}, {}, {}, responseError(response));
                      return;
                    }
                    const QJsonObject object = QJsonDocument::fromJson(response.body).object();
                    const QString authorizationUrl = object.value("authorizationUrl").toString();
                    const QString sessionId = object.value("sessionId").toString();
                    const QString pollToken = object.value("pollToken").toString();
                    if (authorizationUrl.isEmpty() || sessionId.isEmpty() || pollToken.isEmpty())
                    {
                      callback({}, {}, {},
                               QStringLiteral("연결 서비스의 응답 형식이 올바르지 않습니다."));
                      return;
                    }
                    callback(authorizationUrl, sessionId, pollToken, {});
                  });
}

void DiscordConnectionClient::poll(const QString &sessionId, const QString &pollToken,
                                   PollCallback callback)
{
  QMap<QString, QString> headers;
  headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer ") + pollToken);
  const QUrl url(serviceUrl_ + QStringLiteral("/v1/discord/sessions/") + sessionId);
  http_->get(url, headers,
             [callback = std::move(callback)](const HttpResponse &response)
             {
               if (response.statusCode == 202)
               {
                 callback(true, {}, {}, {});
                 return;
               }
               if (!response.error.isEmpty() || response.statusCode != 200)
               {
                 callback(false, {}, {}, responseError(response));
                 return;
               }
               const QJsonObject object = QJsonDocument::fromJson(response.body).object();
               const QString webhookUrl = object.value("webhookUrl").toString();
               if (!isValidDiscordWebhook(webhookUrl))
               {
                 callback(false, {}, {},
                          QStringLiteral("연결 서비스가 올바르지 않은 Webhook을 반환했습니다."));
                 return;
               }
               callback(false, webhookUrl, object.value("channelName").toString(), {});
             });
}
