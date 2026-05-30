#include "AppLog.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

namespace {

QMutex g_logMutex;

} // namespace

AppLog::AppLog(QObject* parent)
    : QObject(parent)
{
}

AppLog& AppLog::instance()
{
    static AppLog log;
    return log;
}

void AppLog::info(const QString& message)
{
    log(QStringLiteral("INFO"), message);
}

void AppLog::warning(const QString& message)
{
    log(QStringLiteral("WARN"), message);
}

void AppLog::error(const QString& message)
{
    log(QStringLiteral("ERROR"), message);
}

void AppLog::log(const QString& level, const QString& message)
{
    const QString line =
        QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss")) + QStringLiteral(" - [") + level +
        QStringLiteral("] ") + message;

    QMutexLocker lock(&g_logMutex);
    emit messageLogged(line);
}
