#include "update-checker.hpp"

#include "http-client.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace
{
const QUrl kLatestReleaseUrl(
    QStringLiteral("https://api.github.com/repos/TereBin/StreamPing-Release/releases/latest"));
const QUrl kUpdateManifestUrl(QStringLiteral(
    "https://raw.githubusercontent.com/TereBin/StreamPing-Release/main/update.json"));
const QString kFallbackDownloadUrl =
    QStringLiteral("https://github.com/TereBin/StreamPing-Release/releases/latest");

struct Version
{
  int major = 0;
  int minor = 0;
  int patch = 0;
  bool valid = false;
};

Version parseVersion(QString value)
{
  static const QRegularExpression pattern(QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)$"));
  const QRegularExpressionMatch match = pattern.match(value.trimmed());
  if (!match.hasMatch())
    return {};

  Version version;
  version.major = match.captured(1).toInt();
  version.minor = match.captured(2).toInt();
  version.patch = match.captured(3).toInt();
  version.valid = true;
  return version;
}

QString normalizedVersion(const QString &value)
{
  const Version version = parseVersion(value);
  return version.valid
             ? QStringLiteral("%1.%2.%3").arg(version.major).arg(version.minor).arg(version.patch)
             : QString();
}

bool isAllowedDownloadUrl(const QString &value)
{
  const QUrl url(value);
  return url.isValid() && url.scheme() == QStringLiteral("https") &&
         url.host() == QStringLiteral("github.com") &&
         url.path().startsWith(QStringLiteral("/TereBin/StreamPing-Release/releases/"));
}

UpdateUrgency urgencyFromString(const QString &value)
{
  if (value == QStringLiteral("optional"))
    return UpdateUrgency::Optional;
  if (value == QStringLiteral("required"))
    return UpdateUrgency::Required;
  return UpdateUrgency::Recommended;
}
} // namespace

int compareVersions(const QString &left, const QString &right)
{
  const Version leftVersion = parseVersion(left);
  const Version rightVersion = parseVersion(right);
  if (!leftVersion.valid || !rightVersion.valid)
    return 0;
  if (leftVersion.major != rightVersion.major)
    return leftVersion.major < rightVersion.major ? -1 : 1;
  if (leftVersion.minor != rightVersion.minor)
    return leftVersion.minor < rightVersion.minor ? -1 : 1;
  if (leftVersion.patch != rightVersion.patch)
    return leftVersion.patch < rightVersion.patch ? -1 : 1;
  return 0;
}

UpdateUrgency effectiveUpdateUrgency(const QString &currentVersion, const UpdateInfo &update)
{
  return compareVersions(currentVersion, update.minimumSupportedVersion) < 0
             ? UpdateUrgency::Required
             : update.urgency;
}

bool parseUpdateManifest(const QByteArray &body, const QString &releaseVersion, UpdateInfo *update)
{
  if (!update)
    return false;

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return false;

  const QJsonObject object = document.object();
  if (object.value(QStringLiteral("schemaVersion")).toInt() != 1)
    return false;

  const QString latestVersion =
      normalizedVersion(object.value(QStringLiteral("latestVersion")).toString());
  const QString minimumVersion =
      normalizedVersion(object.value(QStringLiteral("minimumSupportedVersion")).toString());
  if (latestVersion.isEmpty() || minimumVersion.isEmpty() ||
      latestVersion != normalizedVersion(releaseVersion) ||
      compareVersions(minimumVersion, latestVersion) > 0)
    return false;

  update->latestVersion = latestVersion;
  update->minimumSupportedVersion = minimumVersion;
  update->urgency = urgencyFromString(object.value(QStringLiteral("urgency")).toString());
  update->title = object.value(QStringLiteral("title")).toString().trimmed().left(120);
  update->message = object.value(QStringLiteral("message")).toString().trimmed().left(1000);
  const QString downloadUrl = object.value(QStringLiteral("downloadUrl")).toString().trimmed();
  update->downloadUrl = isAllowedDownloadUrl(downloadUrl) ? downloadUrl : kFallbackDownloadUrl;
  return true;
}

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), http_(new HttpClient(this)) {}

void UpdateChecker::check(const QString &currentVersion, Callback callback)
{
  const QString normalizedCurrent = normalizedVersion(currentVersion);
  if (normalizedCurrent.isEmpty())
  {
    callback({false, {}, QStringLiteral("현재 StreamPing 버전을 확인할 수 없습니다.")});
    return;
  }

  http_->get(
      kLatestReleaseUrl,
      [this, normalizedCurrent,
       callback = std::move(callback)](const HttpResponse &releaseResponse) mutable
      {
        if (!releaseResponse.error.isEmpty() || releaseResponse.statusCode != 200)
        {
          callback({false, {}, QStringLiteral("최신 버전 정보를 확인할 수 없습니다.")});
          return;
        }

        const QJsonObject release = QJsonDocument::fromJson(releaseResponse.body).object();
        const QString releaseVersion =
            normalizedVersion(release.value(QStringLiteral("tag_name")).toString());
        if (releaseVersion.isEmpty())
        {
          callback({false, {}, QStringLiteral("최신 버전 형식이 올바르지 않습니다.")});
          return;
        }

        UpdateInfo fallback;
        fallback.latestVersion = releaseVersion;
        fallback.minimumSupportedVersion = normalizedCurrent;
        fallback.title = release.value(QStringLiteral("name")).toString().trimmed().left(120);
        fallback.message = QStringLiteral("새 StreamPing 버전이 배포되었습니다.");
        const QString releaseUrl = release.value(QStringLiteral("html_url")).toString().trimmed();
        fallback.downloadUrl = isAllowedDownloadUrl(releaseUrl) ? releaseUrl : kFallbackDownloadUrl;

        http_->get(kUpdateManifestUrl,
                   [normalizedCurrent, releaseVersion, fallback,
                    callback = std::move(callback)](const HttpResponse &manifestResponse) mutable
                   {
                     UpdateInfo update = fallback;
                     if (manifestResponse.error.isEmpty() && manifestResponse.statusCode == 200)
                       parseUpdateManifest(manifestResponse.body, releaseVersion, &update);

                     if (compareVersions(normalizedCurrent, update.latestVersion) >= 0)
                     {
                       callback({false, update, {}});
                       return;
                     }
                     update.urgency = effectiveUpdateUrgency(normalizedCurrent, update);
                     callback({true, update, {}});
                   });
      });
}
