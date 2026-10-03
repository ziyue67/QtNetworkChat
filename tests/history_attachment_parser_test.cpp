#include "historyattachmentparser.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) {
        qWarning() << message;
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;

    ok = expect(HistoryAttachmentParser::normalizeHistoryLine(QStringLiteral("  群图片：demo.png  "))
                    == QStringLiteral("群图片:demo.png"),
                "normalizeHistoryLine should trim and normalize full-width punctuation") && ok;

    ok = expect(HistoryAttachmentParser::extractAttachmentName(QStringLiteral("群图片: travel-shot.png"))
                    == QStringLiteral("travel-shot.png"),
                "group image history should expose attachment name") && ok;

    ok = expect(HistoryAttachmentParser::extractAttachmentName(QStringLiteral("回执话术 · 已收到文件 archive-report.zip"))
                    == QStringLiteral("archive-report.zip"),
                "receipt-style file history should expose attachment name") && ok;

    ok = expect(HistoryAttachmentParser::extractAttachmentName(QStringLiteral("附件消息 文件名：weekly-demo.mp4 · 大小 12MB"))
                    == QStringLiteral("weekly-demo.mp4"),
                "explicit attachment name fields should parse legacy attachment names") && ok;

    ok = expect(HistoryAttachmentParser::extractAttachmentPath(QStringLiteral("附件路径：C:/chat/cache/demo.png"))
                    == QStringLiteral("C:/chat/cache/demo.png"),
                "direct attachment path fields should be parsed") && ok;

    ok = expect(HistoryAttachmentParser::extractAttachmentPath(QString(),
                                                               QStringLiteral("双击打开：D:/offline/archive/video-preview.mp4"))
                    == QStringLiteral("D:/offline/archive/video-preview.mp4"),
                "double-click tooltip paths should be parsed") && ok;

    ok = expect(HistoryAttachmentParser::extractAttachmentPath(QStringLiteral("回执话术 · 已收到文件 archive.zip"),
                                                               QStringLiteral("打开附件：E:/history/archive.zip · 来自离线恢复"))
                    == QStringLiteral("E:/history/archive.zip"),
                "legacy open-attachment tooltips should be parsed") && ok;

    QString fileName;
    ok = expect(HistoryAttachmentParser::detectMediaKind(QStringLiteral("群视频: sprint-review.mp4"), &fileName)
                    == QStringLiteral("video")
                    && fileName == QStringLiteral("sprint-review.mp4"),
                "group video history should classify as video") && ok;

    ok = expect(HistoryAttachmentParser::detectMediaKind(QStringLiteral("图片接收卡片 · banner.webp"), &fileName)
                    == QStringLiteral("image")
                    && fileName == QStringLiteral("banner.webp"),
                "image receipt cards should classify as image") && ok;

    ok = expect(HistoryAttachmentParser::detectMediaKind(QStringLiteral("回执话术 · 已收到文件 quarterly-data.csv"), &fileName)
                    == QStringLiteral("file")
                    && fileName == QStringLiteral("quarterly-data.csv"),
                "receipt-style non-media attachments should classify as file") && ok;

    return ok ? 0 : 1;
}
