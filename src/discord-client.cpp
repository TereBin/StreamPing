#include "discord-client.hpp"

#include "http-client.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

DiscordClient::DiscordClient(QObject *parent) : QObject(parent), http_(new HttpClient(this)) {}

void DiscordClient::send(const QString &webhookUrl, const QString &message, Callback callback)
{
  QJsonObject allowedMentions;
  allowedMentions.insert(QStringLiteral("parse"), QJsonArray());

  QJsonObject payload;
  payload.insert(QStringLiteral("content"), message.left(2000));
  payload.insert(QStringLiteral("allowed_mentions"), allowedMentions);

  http_->postJson(QUrl(webhookUrl), QJsonDocument(payload).toJson(QJsonDocument::Compact),
                  [callback = std::move(callback)](const HttpResponse &response)
                  {
                    if (!response.error.isEmpty() || response.statusCode < 200 ||
                        response.statusCode >= 300)
                    {
                      QString detail = QString::fromUtf8(response.body).trimmed();
                      if (detail.size() > 300)
                        detail = detail.left(300);
                      callback(QStringLiteral("Discord 응답 오류 (%1): %2 %3")
                                   .arg(response.statusCode)
                                   .arg(response.error, detail));
                      return;
                    }

                    callback({});
                  });
}

void DiscordClient::remove(const QString &webhookUrl, Callback callback)
{
  http_->deleteResource(QUrl(webhookUrl),
                        [callback = std::move(callback)](const HttpResponse &response)
                        {
                          if (!response.error.isEmpty() ||
                              (response.statusCode != 204 && response.statusCode != 404))
                          {
                            callback(QStringLiteral("Discord 연결 해제 오류 (%1): %2")
                                         .arg(response.statusCode)
                                         .arg(response.error));
                            return;
                          }
                          callback({});
                        });
}
