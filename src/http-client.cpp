#include "http-client.hpp"

#include <QMetaObject>
#include <QPointer>

#include <windows.h>
#include <winhttp.h>

#include <utility>

namespace
{
#define STREAMPING_WIDEN_IMPL(value) L##value
#define STREAMPING_WIDEN(value) STREAMPING_WIDEN_IMPL(value)

class InternetHandle
{
public:
  explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
  ~InternetHandle()
  {
    if (handle_)
      WinHttpCloseHandle(handle_);
  }

  InternetHandle(const InternetHandle &) = delete;
  InternetHandle &operator=(const InternetHandle &) = delete;

  operator HINTERNET() const { return handle_; }
  bool valid() const { return handle_ != nullptr; }

private:
  HINTERNET handle_;
};

QString windowsError(DWORD code)
{
  wchar_t *message = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
  QString result = length && message
                       ? QString::fromWCharArray(message, static_cast<int>(length)).trimmed()
                       : QStringLiteral("Windows 오류 %1").arg(code);
  if (message)
    LocalFree(message);
  return result;
}

HttpResponse performRequest(const QString &method, const QUrl &url, const QByteArray &body,
                            const QMap<QString, QString> &extraHeaders)
{
  HttpResponse response;
  const std::wstring host = url.host().toStdWString();
  QString resource = url.path(QUrl::FullyEncoded);
  if (!url.query(QUrl::FullyEncoded).isEmpty())
    resource += QStringLiteral("?") + url.query(QUrl::FullyEncoded);
  const std::wstring resourcePath = resource.toStdWString();
  const std::wstring methodName = method.toStdWString();

  InternetHandle session(WinHttpOpen(
      L"StreamPing/" STREAMPING_WIDEN(STREAMPING_VERSION) L" OBS-Plugin",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session.valid())
  {
    response.error = windowsError(GetLastError());
    return response;
  }
  WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);

  InternetHandle connection(WinHttpConnect(session, host.c_str(), url.port(443), 0));
  if (!connection.valid())
  {
    response.error = windowsError(GetLastError());
    return response;
  }

  const DWORD flags = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
                          ? WINHTTP_FLAG_SECURE
                          : 0;
  InternetHandle request(WinHttpOpenRequest(connection, methodName.c_str(), resourcePath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
  if (!request.valid())
  {
    response.error = windowsError(GetLastError());
    return response;
  }

  QString headers = QStringLiteral("Accept: application/json\r\n");
  if (method == QStringLiteral("POST"))
    headers += QStringLiteral("Content-Type: application/json\r\n");
  for (auto it = extraHeaders.cbegin(); it != extraHeaders.cend(); ++it)
    headers += QStringLiteral("%1: %2\r\n").arg(it.key(), it.value());
  const std::wstring wideHeaders = headers.toStdWString();
  const LPVOID requestBody =
      body.isEmpty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(body.constData());
  if (!WinHttpSendRequest(request, wideHeaders.c_str(), static_cast<DWORD>(-1L), requestBody,
                          static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
      !WinHttpReceiveResponse(request, nullptr))
  {
    response.error = windowsError(GetLastError());
    return response;
  }

  DWORD statusCode = 0;
  DWORD statusSize = sizeof(statusCode);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                          WINHTTP_NO_HEADER_INDEX))
  {
    response.statusCode = static_cast<int>(statusCode);
  }

  for (;;)
  {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available))
    {
      response.error = windowsError(GetLastError());
      return response;
    }
    if (available == 0)
      break;

    const qsizetype offset = response.body.size();
    response.body.resize(offset + static_cast<qsizetype>(available));
    DWORD bytesRead = 0;
    if (!WinHttpReadData(request, response.body.data() + offset, available, &bytesRead))
    {
      response.error = windowsError(GetLastError());
      return response;
    }
    response.body.resize(offset + static_cast<qsizetype>(bytesRead));
  }

  return response;
}
} // namespace

HttpClient::HttpClient(QObject *parent) : QObject(parent), worker_(new QObject)
{
  worker_->moveToThread(&workerThread_);
  connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
  workerThread_.start();
}

HttpClient::~HttpClient()
{
  workerThread_.quit();
  workerThread_.wait();
}

void HttpClient::get(const QUrl &url, Callback callback) { get(url, {}, std::move(callback)); }

void HttpClient::get(const QUrl &url, const QMap<QString, QString> &headers, Callback callback)
{
  request(QStringLiteral("GET"), url, {}, headers, std::move(callback));
}

void HttpClient::postJson(const QUrl &url, const QByteArray &body, Callback callback)
{
  request(QStringLiteral("POST"), url, body, {}, std::move(callback));
}

void HttpClient::deleteResource(const QUrl &url, Callback callback)
{
  request(QStringLiteral("DELETE"), url, {}, {}, std::move(callback));
}

void HttpClient::request(const QString &method, const QUrl &url, const QByteArray &body,
                         const QMap<QString, QString> &headers, Callback callback)
{
  QPointer<HttpClient> self(this);
  QMetaObject::invokeMethod(
      worker_,
      [self, method, url, body, headers, callback = std::move(callback)]() mutable
      {
        const HttpResponse response = performRequest(method, url, body, headers);
        if (!self)
          return;
        QMetaObject::invokeMethod(
            self,
            [self, response, callback = std::move(callback)]() mutable
            {
              if (self)
                callback(response);
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}
