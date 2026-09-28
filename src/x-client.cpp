#include "x-client.hpp"

#include <QDesktopServices>
#include <QUrlQuery>

QUrl buildXComposeUrl(const QString &message)
{
  QUrl url(QStringLiteral("https://twitter.com/intent/tweet"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("text"), message);
  url.setQuery(query);
  return url;
}

bool XClient::openComposer(const QString &message) const
{
  return QDesktopServices::openUrl(buildXComposeUrl(message));
}
