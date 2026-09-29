#pragma once

#include <QObject>
#include <QString>

#include <functional>

class HttpClient;

enum class UpdateUrgency
{
  Optional,
  Recommended,
  Required,
};

struct UpdateInfo
{
  QString latestVersion;
  QString minimumSupportedVersion;
  QString title;
  QString message;
  QString downloadUrl;
  UpdateUrgency urgency = UpdateUrgency::Recommended;
};

struct UpdateCheckResult
{
  bool updateAvailable = false;
  UpdateInfo update;
  QString error;
};

int compareVersions(const QString &left, const QString &right);
UpdateUrgency effectiveUpdateUrgency(const QString &currentVersion, const UpdateInfo &update);
bool parseUpdateManifest(const QByteArray &body, const QString &releaseVersion, UpdateInfo *update);

class UpdateChecker : public QObject
{
  Q_OBJECT

public:
  using Callback = std::function<void(const UpdateCheckResult &result)>;

  explicit UpdateChecker(QObject *parent = nullptr);
  void check(const QString &currentVersion, Callback callback);

private:
  HttpClient *http_;
};
