#ifndef QQNT_LOG_H
#define QQNT_LOG_H

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QString>
#include <QTextStream>

namespace qtnetworkchat {

inline void logDebug(const QString& tag, const QString& message)
{
    const QString line = QStringLiteral("[%1][%2] %3")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")))
                             .arg(tag)
                             .arg(message);
    qDebug().noquote() << line;

    QFile file(QStringLiteral("qqnt-debug.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << line << '\n';
    }
}

} // namespace qtnetworkchat

#endif // QQNT_LOG_H
