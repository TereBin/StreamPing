#pragma once

#include <QByteArray>
#include <QString>

QByteArray buildDiscordMessagePayload(const QString &message);
QString prependDiscordRoleMention(const QString &message, const QString &roleId);
