#pragma once

#include <QObject>
#include <QString>

#include <functional>

class HttpClient;

struct LiveInfo
{
  bool isLive = false;
  QString liveKey;
  QString title;
  QString category;
  QString channelName;
  QString channelUrl;
  QString thumbnailUrl;
};

class ChzzkClient : public QObject
{
  Q_OBJECT

public:
  using Callback = std::function<void(const LiveInfo &, const QString &error)>;

  explicit ChzzkClient(QObject *parent = nullptr);
  void fetchLive(const QString &channelId, Callback callback);

private:
  HttpClient *http_;
};
