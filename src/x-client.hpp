#pragma once

#include <QString>
#include <QUrl>

QUrl buildXComposeUrl(const QString &message);

class XClient
{
public:
  bool openComposer(const QString &message) const;
};
