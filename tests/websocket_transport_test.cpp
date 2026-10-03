#include "client.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>
#include <QTimer>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

#include <functional>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning("%s", message);
        return false;
    }
    return true;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 3000) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return predicate();
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    QWebSocketServer server(QStringLiteral("QtNetworkChat transport test"),
                            QWebSocketServer::NonSecureMode);
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        qWarning() << "Could not start test WebSocket server:" << server.errorString();
        return 1;
    }

    QWebSocket* peer = nullptr;
    QByteArray received;
    QJsonObject loginRequest;
    QObject::connect(&server, &QWebSocketServer::newConnection, &app, [&]() {
        peer = server.nextPendingConnection();
        QObject::connect(peer, &QWebSocket::binaryMessageReceived, &app,
                         [&](const QByteArray& message) {
            received.append(message);
            const int newline = received.indexOf('\n');
            if (newline < 0) {
                return;
            }
            const QJsonDocument document = QJsonDocument::fromJson(received.left(newline));
            if (document.isObject()) {
                loginRequest = document.object();
            }
        });
    });

    const QByteArray url = QStringLiteral("ws://127.0.0.1:%1/ws")
        .arg(server.serverPort()).toUtf8();
    qputenv("QTNETWORKCHAT_TRANSPORT", "ws");
    qputenv("QTNETWORKCHAT_WEBSOCKET_URL", url);
    qunsetenv("QTNETWORKCHAT_TLS_PINNED_SHA256");
    qunsetenv("QTNETWORKCHAT_TLS_PINNED_FINGERPRINT_SHA256");

    Client client;
    client.setUserInfo(QString(), QStringLiteral("WebSocketTest"));
    client.setAccountInfo(QStringLiteral("ws-test-account"),
                          QStringLiteral("test-password"),
                          false);
    client.setLoginMode(QStringLiteral("login"));

    bool connectedSignal = false;
    QString loginFailure;
    QObject::connect(&client, &Client::connected, &app, [&]() {
        connectedSignal = true;
    });
    QObject::connect(&client, &Client::loginFailed, &app,
                     [&](const QString& reason) {
        loginFailure = reason;
    });

    bool ok = true;
    ok = expect(client.connectToServer(QStringLiteral("ignored.example"), 443),
                "Client should connect through the configured WebSocket URL") && ok;
    ok = expect(waitUntil([&]() {
        return peer && !loginRequest.isEmpty();
    }), "WebSocket server should receive the automatic login frame") && ok;
    ok = expect(connectedSignal && client.isConnected(),
                "Client should expose the WebSocket connected state") && ok;
    ok = expect(loginRequest.value(QStringLiteral("type")).toString()
                    == QStringLiteral("login")
                && loginRequest.value(QStringLiteral("account")).toString()
                    == QStringLiteral("ws-test-account")
                && loginRequest.value(QStringLiteral("mode")).toString()
                    == QStringLiteral("login"),
                "Login payload should be preserved over the WebSocket transport") && ok;
    ok = expect(client.transportSecurityDescription()
                    == QStringLiteral("WebSocket 明文通道"),
                "Transport description should identify plain WebSocket") && ok;

    if (peer) {
        QJsonObject response;
        response[QStringLiteral("type")] = QStringLiteral("login_failed");
        response[QStringLiteral("reason")] = QStringLiteral("expected-ws-test-failure");
        peer->sendTextMessage(QString::fromUtf8(
            QJsonDocument(response).toJson(QJsonDocument::Compact)));
    }
    ok = expect(waitUntil([&]() {
        return loginFailure == QStringLiteral("expected-ws-test-failure");
    }), "A complete text frame without a newline should reach the JSON parser") && ok;

    client.disconnectFromServer();
    qunsetenv("QTNETWORKCHAT_TRANSPORT");
    qunsetenv("QTNETWORKCHAT_WEBSOCKET_URL");

    qputenv("QTNETWORKCHAT_TRANSPORT", "tls");
    qputenv("QTNETWORKCHAT_TLS_PINNED_SHA256", QByteArray(64, 'a'));
    Client pinnedTlsClient;
    QSslSocket* tlsSocket = pinnedTlsClient.findChild<QSslSocket*>();
    bool tlsConnectedSignal = false;
    QString tlsError;
    QObject::connect(&pinnedTlsClient, &Client::connected, &app,
                     [&]() { tlsConnectedSignal = true; });
    QObject::connect(&pinnedTlsClient, &Client::connectionError, &app,
                     [&](const QString& error) { tlsError = error; });
    ok = expect(tlsSocket && QMetaObject::invokeMethod(tlsSocket, "encrypted", Qt::DirectConnection),
                "TLS encrypted signal should reach the pin gate") && ok;
    ok = expect(!tlsConnectedSignal && !pinnedTlsClient.isConnected()
                    && tlsError.contains(QStringLiteral("指纹不匹配")),
                "a missing or mismatched TLS certificate must be rejected before connected") && ok;
    qunsetenv("QTNETWORKCHAT_TLS_PINNED_SHA256");
    qunsetenv("QTNETWORKCHAT_TRANSPORT");
    return ok ? 0 : 1;
}
