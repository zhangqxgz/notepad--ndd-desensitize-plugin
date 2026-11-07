#include "logutil.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

QString logFilePath()
{
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
    {
        base = QDir::homePath();
    }
    return QDir(base).filePath(QStringLiteral("ndd-desensitive/plugin.log"));
}

void logDebug(const QString &message)
{
    const QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        return;
    }
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
           << QLatin1Char(' ') << message << Qt::endl;
}
