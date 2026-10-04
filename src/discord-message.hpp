#pragma once

#include <QByteArray>
#include <QString>

struct LiveInfo;

struct DiscordEmbedOptions
{
  int color = 0x00ffa3;
  bool showChannel = true;
  bool showCategory = true;
  bool showThumbnail = true;
};

QByteArray buildDiscordMessagePayload(const QString &message, const LiveInfo &live,
                                      const DiscordEmbedOptions &options = {});
QString replaceDiscordRoleTag(const QString &message, const QString &roleId);
