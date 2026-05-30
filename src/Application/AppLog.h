#pragma once

#include <QObject>
#include <QString>

class AppLog : public QObject
{
    Q_OBJECT

public:
    static AppLog& instance();

    void info(const QString& message);
    void warning(const QString& message);
    void error(const QString& message);

signals:
    void messageLogged(const QString& line);

private:
    explicit AppLog(QObject* parent = nullptr);
    void log(const QString& level, const QString& message);
};
