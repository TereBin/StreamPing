#pragma once

#include <QByteArray>
#include <QString>

struct LiveInfo;

QByteArray buildDiscordMessagePayload(const QString &message, const LiveInfo &live);
QString replaceDiscordRoleTag(const QString &message, const QString &roleId);
