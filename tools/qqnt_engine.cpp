#include "qqnt_client_bridge.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <cstdio>
#include <iostream>
#include <string>

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

class StdinReader : public QThread {
public:
    explicit StdinReader(QQNTClientBridge* bridge, QObject* parent = nullptr)
        : QThread(parent)
        , m_bridge(bridge)
    {
    }

protected:
    void run() override {
        std::string line;
        while (std::getline(std::cin, line)) {
            const QByteArray bytes(line.data(), static_cast<int>(line.size()));
            QMetaObject::invokeMethod(m_bridge, [bridge = m_bridge, bytes]() {
                bridge->handleCommandLine(bytes);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
    }

private:
    QQNTClientBridge* m_bridge;
};
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(stderrMessageHandler);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QQNTEngine"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QQNTClientBridge bridge;
    StdinReader reader(&bridge);
    reader.start();
    bridge.start();

    const int result = app.exec();
    reader.wait(1000);
    return result;
}
