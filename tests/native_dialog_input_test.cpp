#include "native_dialog_input.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

class NativeDialogInputTest : public QObject {
    Q_OBJECT
private slots:
    void saveToRequestedPath() {
        QVERIFY(!QApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
        QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString requested = root.filePath(QStringLiteral("requested-export.txt"));
        const QString initial = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("QtNetworkChat_公共聊天室_默认导出.txt"));
        QWidget parent;
        parent.show();
        NativeDialogInput input(this);
        input.choices = {{requested, false, true}};
        input.start();
        const QString selected = QFileDialog::getSaveFileName(&parent,
            QStringLiteral("原生保存路径验收"), initial, QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
        qInfo() << "NATIVE_SAVE_RESULT initial" << initial << "requested" << requested << "selected" << selected;
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(selected)), QDir::cleanPath(requested));
        QFile file(selected);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        QCOMPARE(file.write("native-save-path-verified"), qint64(25));
        file.close();
        QFile result(requested);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("native-save-path-verified"));
        QTRY_COMPARE_WITH_TIMEOUT(input.observed.load(), 1, 1000);
    }
};

QTEST_MAIN(NativeDialogInputTest)
#include "native_dialog_input_test.moc"
