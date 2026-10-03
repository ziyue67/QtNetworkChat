#include "server.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cstdio>

namespace {
void stderrMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    const char* level = "debug";
    switch (type) {
    case QtInfoMsg:
        level = "info";
        break;
    case QtWarningMsg:
        level = "warn";
        break;
    case QtCriticalMsg:
        level = "critical";
        break;
    case QtFatalMsg:
        level = "fatal";
        break;
    default:
        break;
    }

    const QByteArray localMessage = message.toLocal8Bit();
    std::fprintf(stderr, "[%s] %s\n", level, localMessage.constData());
    std::fflush(stderr);
}

quint16 configuredPort(const QStringList& arguments) {
    for (int index = 1; index + 1 < arguments.size(); ++index) {
        if (arguments.at(index) == QLatin1String("--port")) {
            bool ok = false;
            const int port = arguments.at(index + 1).toInt(&ok);
            if (ok && port > 0 && port <= 65535) {
                return static_cast<quint16>(port);
            }
        }
    }

    bool ok = false;
    const int envPort = QString::fromLocal8Bit(qgetenv("QQNT_SERVER_PORT")).toInt(&ok);
    if (ok && envPort > 0 && envPort <= 65535) {
        return static_cast<quint16>(envPort);
    }

    return 8888;
}
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(stderrMessageHandler);

    QCoreApplication app(argc, argv);
    // Match the desktop client so both local executables resolve the same
    // AppData directory and therefore the same accounts.sqlite3 database.
    QCoreApplication::setApplicationName(QStringLiteral("QtNetworkChat"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    Server server;
    const quint16 port = configuredPort(app.arguments());
    if (!server.start(port)) {
        qCritical("QQNTServer failed to start; review the TLS, Redis, and database diagnostics above.");
        return 2;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &server, &Server::stop);
    return app.exec();
}
