#ifndef QQNT_BACKEND_SERVICE_H
#define QQNT_BACKEND_SERVICE_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

class QQNTBackendService {
public:
    static QStringList commands();
    static bool isCommand(const QString& op);
    static bool handle(const QString& op,
                       const QJsonObject& payload,
                       QJsonObject* response,
                       QString* errorCode,
                       QString* errorMessage);
};

#endif // QQNT_BACKEND_SERVICE_H
