#include "http-client.hpp"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QTimer>

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  if (argc != 2)
    return 64;

  const QString channelId = QString::fromLocal8Bit(argv[1]).trimmed().toLower();
  static const QRegularExpression channelIdPattern(QStringLiteral("^[0-9a-f]{32}$"));
  if (!channelIdPattern.match(channelId).hasMatch())
    return 64;

  HttpClient client;

  QTimer::singleShot(15000, &application, [&application] { application.exit(2); });
  client.get(QUrl(QStringLiteral("https://api.chzzk.naver.com/polling/v3.1/channels/%1/live-status")
                      .arg(channelId)),
             [&application](const HttpResponse &response)
             { application.exit(response.error.isEmpty() && response.statusCode == 200 ? 0 : 1); });

  return application.exec();
}
