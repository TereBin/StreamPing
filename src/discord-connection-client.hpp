#pragma once

#include <QObject>
#include <QString>

#include <functional>

class HttpClient;

class DiscordConnectionClient : public QObject
{
  Q_OBJECT

public:
  using StartCallback =
      std::function<void(const QString &authorizationUrl, const QString &sessionId,
                         const QString &pollToken, const QString &error)>;
  using PollCallback =
      std::function<void(bool pending, const QString &webhookUrl, const QString &channelName,
                         const QString &roleId, const QString &roleName, const QString &error)>;

  explicit DiscordConnectionClient(QObject *parent = nullptr);

  bool isConfigured() const;
  void start(StartCallback callback);
  void poll(const QString &sessionId, const QString &pollToken, PollCallback callback);

private:
  QString serviceUrl_;
  HttpClient *http_;
};
