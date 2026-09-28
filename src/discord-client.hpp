#pragma once

#include <QObject>
#include <QString>

#include <functional>

class HttpClient;

class DiscordClient : public QObject
{
  Q_OBJECT

public:
  using Callback = std::function<void(const QString &error)>;

  explicit DiscordClient(QObject *parent = nullptr);
  void send(const QString &webhookUrl, const QString &message, Callback callback);
  void remove(const QString &webhookUrl, Callback callback);

private:
  HttpClient *http_;
};
