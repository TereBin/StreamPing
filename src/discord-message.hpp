#pragma once

#include <QByteArray>
#include <QString>

QByteArray buildDiscordMessagePayload(const QString &message);
QString replaceDiscordRoleTag(const QString &message, const QString &roleId);
