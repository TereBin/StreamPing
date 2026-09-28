#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <QThread>
#include <QUrl>

#include <functional>

struct HttpResponse
{
  int statusCode = 0;
  QByteArray body;
  QString error;
};

class HttpClient : public QObject
{
  Q_OBJECT

public:
  using Callback = std::function<void(const HttpResponse &response)>;

  explicit HttpClient(QObject *parent = nullptr);
  ~HttpClient() override;

  void get(const QUrl &url, Callback callback);
  void get(const QUrl &url, const QMap<QString, QString> &headers, Callback callback);
  void postJson(const QUrl &url, const QByteArray &body, Callback callback);
  void deleteResource(const QUrl &url, Callback callback);

private:
  void request(const QString &method, const QUrl &url, const QByteArray &body,
               const QMap<QString, QString> &headers, Callback callback);

  QThread workerThread_;
  QObject *worker_;
};
