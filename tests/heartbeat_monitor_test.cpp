#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QEventLoop>
#include "heartbeatmonitor.h"

class HeartbeatMonitorTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qDebug() << "Initializing HeartbeatMonitor test case";
    }

    void testClientRegistration() {
        HeartbeatMonitor monitor;
        monitor.registerClient("client1");
        monitor.registerClient("client2");

        QCOMPARE(monitor.isClientActive("client1"), true);
        QCOMPARE(monitor.isClientActive("client2"), true);
        QCOMPARE(monitor.isClientActive("client3"), false);
    }

    void testClientUnregistration() {
        HeartbeatMonitor monitor;
        monitor.registerClient("client1");
        monitor.unregisterClient("client1");

        QCOMPARE(monitor.isClientActive("client1"), false);
    }

    void testClientActivityUpdate() {
        HeartbeatMonitor monitor;
        monitor.registerClient("client1");

        QDateTime before = monitor.lastActivityTime("client1");
        QTest::qSleep(10);
        monitor.updateClientActivity("client1");
        QDateTime after = monitor.lastActivityTime("client1");

        QVERIFY(after >= before);
    }

    void testTimeoutDetection() {
        HeartbeatMonitor monitor;
        monitor.setTimeoutMs(100); // 100ms timeout for testing

        QSignalSpy spy(&monitor, &HeartbeatMonitor::clientTimedOut);
        QVERIFY(spy.isValid());

        monitor.registerClient("client1");
        monitor.start(50); // Check every 50ms

        // Wait for timeout
        QTest::qSleep(200);
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("client1"));
    }

    void testStats() {
        HeartbeatMonitor monitor;
        monitor.registerClient("client1");
        monitor.registerClient("client2");

        HeartbeatStats stats = monitor.stats();
        QCOMPARE(stats.totalClients, 2);
        QCOMPARE(stats.activeClients, 2);
        QCOMPARE(stats.timedOutClients, 0);
    }

    void testStatsAfterTimeout() {
        HeartbeatMonitor monitor;
        monitor.setTimeoutMs(100); // 100ms timeout

        monitor.registerClient("client1");
        monitor.registerClient("client2");

        // Wait for timeout
        QTest::qSleep(150);
        QCoreApplication::processEvents();

        // Check that client1 is now inactive
        QCOMPARE(monitor.isClientActive("client1"), false);
        QCOMPARE(monitor.isClientActive("client2"), false);
    }

    void testTimeoutCallback() {
        HeartbeatMonitor monitor;
        monitor.setTimeoutMs(100); // 100ms timeout

        QStringList timedOutClients;
        monitor.setTimeoutCallback([&timedOutClients](const QString& clientId) {
            timedOutClients.append(clientId);
        });

        monitor.registerClient("client1");
        monitor.start(50); // Check every 50ms

        // Wait for timeout
        QTest::qSleep(200);
        QCoreApplication::processEvents();

        QCOMPARE(timedOutClients.size(), 1);
        QCOMPARE(timedOutClients.at(0), QString("client1"));
    }

    void testMultipleClients() {
        HeartbeatMonitor monitor;
        monitor.setTimeoutMs(300);

        QSignalSpy spy(&monitor, &HeartbeatMonitor::clientTimedOut);
        QVERIFY(spy.isValid());

        monitor.registerClient("client1");
        monitor.registerClient("client3");

        QTest::qSleep(200);
        monitor.registerClient("client2");

        monitor.start(50);

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 250);

        // client1 and client3 should timeout, client2 should still be active.
        QCOMPARE(spy.count(), 2);
        QVERIFY(monitor.isClientActive("client2"));
    }

    void cleanupTestCase() {
        qDebug() << "HeartbeatMonitor test case completed";
    }
};

QTEST_MAIN(HeartbeatMonitorTest)
#include "heartbeat_monitor_test.moc"
