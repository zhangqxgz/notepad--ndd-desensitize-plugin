#include "settingsstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

QString SettingsStore::filePath()
{
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
    {
        base = QDir::homePath();
    }
    return QDir(base).filePath(QStringLiteral("ndd-desensitive/settings.json"));
}

AppSettings SettingsStore::load(QString *error)
{
    AppSettings settings;
    QFile file(filePath());
    if (!file.exists())
    {
        return settings;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("无法读取设置：%1").arg(filePath());
        }
        return settings;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("设置文件格式错误：%1").arg(parseError.errorString());
        }
        return settings;
    }

    const QJsonObject object = document.object();
    settings.hotkeyDesensitize = object.value(QStringLiteral("hotkeyDesensitize"))
                                     .toString(settings.hotkeyDesensitize);
    settings.hotkeyRestore = object.value(QStringLiteral("hotkeyRestore")).toString(settings.hotkeyRestore);
    settings.caseSensitive = object.value(QStringLiteral("caseSensitive")).toBool(settings.caseSensitive);
    settings.selectionOnly = object.value(QStringLiteral("selectionOnly")).toBool(settings.selectionOnly);
    settings.silent = object.value(QStringLiteral("silent")).toBool(settings.silent);
    return settings;
}

bool SettingsStore::save(const AppSettings &settings, QString *error)
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject object;
    object.insert(QStringLiteral("hotkeyDesensitize"), settings.hotkeyDesensitize);
    object.insert(QStringLiteral("hotkeyRestore"), settings.hotkeyRestore);
    object.insert(QStringLiteral("caseSensitive"), settings.caseSensitive);
    object.insert(QStringLiteral("selectionOnly"), settings.selectionOnly);
    object.insert(QStringLiteral("silent"), settings.silent);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("无法写入设置：%1").arg(path);
        }
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("保存设置失败：%1").arg(path);
        }
        return false;
    }
    return true;
}
