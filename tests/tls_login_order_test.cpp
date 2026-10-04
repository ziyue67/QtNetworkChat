#include "client.h"
#include "tlssecurity.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QThread>
#include <QWebSocket>
#include <QWebSocketServer>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <functional>
#include <memory>

namespace {
bool waitFor(const std::function<bool()>& condition, int ms = 2500) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return condition();
}

// Generate a fresh, short-lived test trust root. No production key or fixed
// certificate validity window is embedded in the repository.
bool makeIdentity(QSslCertificate& certificate, QSslKey& key) {
    using Ctx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    using Pkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using Cert = std::unique_ptr<X509, decltype(&X509_free)>;
    using Bio = std::unique_ptr<BIO, decltype(&BIO_free)>;
    Ctx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    EVP_PKEY* generated = nullptr;
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) <= 0
        || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) <= 0
        || EVP_PKEY_keygen(ctx.get(), &generated) <= 0) return false;
    Pkey privateKey(generated, EVP_PKEY_free);
    Cert cert(X509_new(), X509_free);
    if (!cert || X509_set_version(cert.get(), 2) != 1
        || ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) != 1
        || !X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60)
        || !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 86400)
        || X509_set_pubkey(cert.get(), privateKey.get()) != 1) return false;
    X509_NAME* name = X509_get_subject_name(cert.get());
    if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0) != 1
        || X509_set_issuer_name(cert.get(), name) != 1) return false;
    for (const auto& extension : {
            std::pair<int, const char*>{NID_basic_constraints, "critical,CA:TRUE"},
            {NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1"}}) {
        X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, nullptr, extension.first,
                                                const_cast<char*>(extension.second));
        if (!ext) return false;
        const bool added = X509_add_ext(cert.get(), ext, -1) == 1;
        X509_EXTENSION_free(ext);
        if (!added) return false;
    }
    if (X509_sign(cert.get(), privateKey.get(), EVP_sha256()) <= 0) return false;
    Bio certBio(BIO_new(BIO_s_mem()), BIO_free), keyBio(BIO_new(BIO_s_mem()), BIO_free);
    if (!certBio || !keyBio || PEM_write_bio_X509(certBio.get(), cert.get()) != 1
        || PEM_write_bio_PrivateKey(keyBio.get(), privateKey.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1) return false;
    char* data = nullptr;
    auto size = BIO_get_mem_data(certBio.get(), &data);
    certificate = QSslCertificate(QByteArray(data, int(size)), QSsl::Pem);
    size = BIO_get_mem_data(keyBio.get(), &data);
    key = QSslKey(QByteArray(data, int(size)), QSsl::Rsa, QSsl::Pem);
    return !certificate.isNull() && !key.isNull();
}

class TlsServer final : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslKey key;
    std::function<void(const QByteArray&, const std::function<void(const QByteArray&)>&)> receive;
protected:
    void incomingConnection(qintptr descriptor) override {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        socket->setLocalCertificate(certificate);
        socket->setPrivateKey(key);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        connect(socket, &QSslSocket::readyRead, socket, [this, socket] {
            receive(socket->readAll(), [socket](const QByteArray& reply) {
                QMetaObject::invokeMethod(socket, [socket, reply] { socket->write(reply); }, Qt::QueuedConnection);
            });
        });
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        socket->startServerEncryption();
    }
};

bool runCase(const QString& transport, const QString& label, const QSslCertificate& certificate,
             const QSslKey& key, bool trustCa, const QByteArray& pin, bool wrongHost, bool accepted) {
    qputenv("QTNETWORKCHAT_TRANSPORT", transport.toUtf8());
    qputenv("QTNETWORKCHAT_TLS_VERIFY", "1");
    qunsetenv("QTNETWORKCHAT_WEBSOCKET_URL");
    qunsetenv("QTNETWORKCHAT_TLS_PINNED_FINGERPRINT_SHA256");
    if (pin.isEmpty()) qunsetenv("QTNETWORKCHAT_TLS_PINNED_SHA256");
    else qputenv("QTNETWORKCHAT_TLS_PINNED_SHA256", pin);
    auto config = QSslConfiguration::defaultConfiguration();
    config.setCaCertificates(trustCa ? QList<QSslCertificate>{certificate} : QList<QSslCertificate>{});
    QSslConfiguration::setDefaultConfiguration(config);
    int loginFrames = 0, connectedSignals = 0;
    QByteArray received;
    auto receive = [&](const QByteArray& bytes, const std::function<void(const QByteArray&)>& reply) {
        received.append(bytes);
        while (received.contains('\n')) {
            const int newline = received.indexOf('\n');
            const auto frame = QJsonDocument::fromJson(received.left(newline)).object();
            received.remove(0, newline + 1);
            if (frame.value("type").toString() != "login") continue;
            ++loginFrames;
            if (frame.value("account").toString() != "tls-test-account"
                || frame.value("password").toString() != "test-only-password") continue;
            reply(QByteArrayLiteral("{\"type\":\"login_success\",\"userId\":\"tls-test-account\",\"account\":\"tls-test-account\",\"userName\":\"TlsProbe\"}\n"));
        }
    };
    TlsServer tcp;
    QThread tcpThread;
    QWebSocketServer web(QStringLiteral("TLS ordering fixture"), QWebSocketServer::SecureMode);
    quint16 port = 0;
    if (transport == "tls") {
        tcp.certificate = certificate; tcp.key = key;
        tcp.receive = [&](const QByteArray& bytes, const std::function<void(const QByteArray&)>& reply) {
            QMetaObject::invokeMethod(&tcpThread, [&, bytes, reply] { receive(bytes, reply); }, Qt::QueuedConnection);
        };
        // Client waits synchronously for the TLS handshake. A separate server
        // thread models an external peer and keeps that handshake progressing.
        tcp.moveToThread(&tcpThread);
        tcpThread.start();
        QMetaObject::invokeMethod(&tcp, [&] {
            if (tcp.listen(QHostAddress::AnyIPv4, 0)) port = tcp.serverPort();
        }, Qt::BlockingQueuedConnection);
    } else {
        config.setLocalCertificate(certificate); config.setPrivateKey(key);
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        web.setSslConfiguration(config);
        QObject::connect(&web, &QWebSocketServer::newConnection, &web, [&] {
            auto* socket = web.nextPendingConnection();
            QObject::connect(socket, &QWebSocket::textMessageReceived, socket, [&, socket](const QString& text) {
                receive(text.toUtf8(), [socket](const QByteArray& bytes) { socket->sendTextMessage(QString::fromUtf8(bytes)); });
            });
            QObject::connect(socket, &QWebSocket::binaryMessageReceived, socket, [&, socket](const QByteArray& bytes) {
                receive(bytes, [socket](const QByteArray& reply) { socket->sendBinaryMessage(reply); });
            });
            QObject::connect(socket, &QWebSocket::disconnected, socket, &QObject::deleteLater);
        });
        if (!web.listen(QHostAddress::AnyIPv4, 0)) return false;
        port = web.serverPort();
    }
    Client client;
    client.setAccountInfo("tls-test-account", "test-only-password", false);
    QObject::connect(&client, &Client::connected, &client, [&] { ++connectedSignals; });
    const bool connected = client.connectToServer(wrongHost ? "127.0.0.2" : "127.0.0.1", port);
    bool valid = connected == accepted;
    if (accepted) valid = client.waitForLoginResult() && waitFor([&] { return loginFrames == 1; })
                            && connectedSignals == 1 && valid;
    else {
        // Give both server transports time to observe any erroneously sent login frame.
        QElapsedTimer drain; drain.start();
        waitFor([&] { return drain.elapsed() >= 250; }, 500);
        valid = loginFrames == 0 && connectedSignals == 0 && received.isEmpty() && !client.isConnected() && valid;
    }
    client.disconnectFromServer();
    if (tcpThread.isRunning()) {
        QMetaObject::invokeMethod(&tcp, [&] {
            tcp.close();
            qDeleteAll(tcp.findChildren<QSslSocket*>());
            tcp.moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
        tcpThread.quit();
        tcpThread.wait();
    }
    qInfo().noquote() << transport << label << (valid ? "PASS" : "FAIL")
                     << "connected=" << connected << "loginFrames=" << loginFrames;
    return valid;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid() || !QSslSocket::supportsSsl()) return 1;
    qputenv("QTNETWORKCHAT_APPDATA_DIR", temp.path().toUtf8());
    const auto originalConfig = QSslConfiguration::defaultConfiguration();
    QSslCertificate certificate; QSslKey key;
    if (!makeIdentity(certificate, key)) return 1;
    const auto pin = certificateSha256Fingerprint(certificate).toUtf8();
    bool ok = true;
    for (const auto& transport : {QStringLiteral("tls"), QStringLiteral("wss")}) {
        ok = runCase(transport, "correct pin", certificate, key, false, pin, false, true) && ok;
        ok = runCase(transport, "wrong pin blocks login", certificate, key, false, QByteArray(64, '0'), false, false) && ok;
        ok = runCase(transport, "untrusted certificate blocks login", certificate, key, false, {}, false, false) && ok;
        ok = runCase(transport, "trusted CA", certificate, key, true, {}, false, true) && ok;
        ok = runCase(transport, "wrong hostname blocks login", certificate, key, true, {}, true, false) && ok;
    }
    QSslConfiguration::setDefaultConfiguration(originalConfig);
    return ok ? 0 : 1;
}
