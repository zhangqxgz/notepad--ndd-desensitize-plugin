#pragma once

#include <QString>

struct AppSettings
{
    QString hotkeyDesensitize = QStringLiteral("Ctrl+Alt+D");
    QString hotkeyRestore = QStringLiteral("Ctrl+Alt+R");
    bool caseSensitive = true;
    bool selectionOnly = false;
    bool silent = true;
};

namespace SettingsStore
{
QString filePath();
AppSettings load(QString *error = nullptr);
bool save(const AppSettings &settings, QString *error = nullptr);
}
