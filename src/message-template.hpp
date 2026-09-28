#pragma once

#include <QString>

struct LiveInfo;

QString renderMessage(const QString &messageTemplate, const LiveInfo &live);
QString renderTestMessage(const QString &messageTemplate, LiveInfo live, const QString &channelId);
