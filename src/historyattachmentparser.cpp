#include "historyattachmentparser.h"

#include "localfilemanager.h"

#include <QFileInfo>
#include <QList>
#include <QRegularExpression>

namespace HistoryAttachmentParser {

QString normalizeHistoryLine(const QString& value) {
    QString line = value;
    line.replace(QStringLiteral("　"), QStringLiteral(" "));
    line.replace(QStringLiteral("："), QStringLiteral(":"));
    return line.trimmed();
}

QString extractAttachmentName(const QString& line) {
    const QString normalizedLine = normalizeHistoryLine(line);
    QRegularExpression imagePattern(QStringLiteral("(?:\\[图片\\]|发送了图片:|群图片:|图片消息:|图片回执:)\\s*([^\\n·]+)"));
    QRegularExpression filePattern(QStringLiteral("(?:发送了文件:|发送了视频:|自动保存:|已保存到:|保存到:|文件已保存到:|已收到\\s*(?:图片|视频|文件)\\s*·|图片接收卡片\\s*·|文件接收卡片\\s*·|视频接收卡片\\s*·|群文件:|群视频:|附件消息:|回执话术\\s*·\\s*已收到(?:图片|视频|文件)\\s*)([^\\n·，]+)"));

    const QRegularExpressionMatch imageMatch = imagePattern.match(normalizedLine);
    if (imageMatch.hasMatch()) {
        return imageMatch.captured(1).trimmed();
    }

    const QRegularExpressionMatch fileMatch = filePattern.match(normalizedLine);
    if (fileMatch.hasMatch()) {
        return fileMatch.captured(1).trimmed();
    }

    const QRegularExpression explicitFileName(QStringLiteral("(?:文件名|附件名|图片名|视频名)\\s*[:：]\\s*([^\\n]+)"));
    const QRegularExpressionMatch explicitMatch = explicitFileName.match(line);
    if (explicitMatch.hasMatch()) {
        return explicitMatch.captured(1).section(QStringLiteral(" · "), 0, 0).trimmed();
    }
    return QString();
}

QString extractAttachmentPath(const QString& line, const QString& toolTipText) {
    QString path = LocalFileManager::extractSavePathFromChatText(line);
    if (path.isEmpty()) {
        path = LocalFileManager::extractSavePathFromChatText(toolTipText);
    }

    if (path.isEmpty()) {
        const QRegularExpression doubleClickPattern(QStringLiteral("双击打开：([^\\n]+)"));
        const QRegularExpressionMatch match = doubleClickPattern.match(toolTipText);
        if (match.hasMatch()) {
            path = match.captured(1).trimmed();
        }
    }

    if (path.isEmpty()) {
        const QString combined = line + QLatin1Char('\n') + toolTipText;
        const QList<QRegularExpression> patterns = {
            QRegularExpression(QStringLiteral("(?:路径|保存路径|落盘路径|本地路径|附件路径)\\s*[:：]\\s*([^\\n]+)")),
            QRegularExpression(QStringLiteral("(?:已保存到|保存到|自动保存到|写入到)\\s*[:：]?\\s*([^\\n]+)")),
            QRegularExpression(QStringLiteral("(?:双击打开|打开附件)\\s*[:：]\\s*([^\\n]+)"))
        };
        for (const QRegularExpression& pattern : patterns) {
            const QRegularExpressionMatch match = pattern.match(combined);
            if (match.hasMatch()) {
                path = match.captured(1).section(QStringLiteral(" · "), 0, 0).trimmed();
                if (!path.isEmpty()) {
                    break;
                }
            }
        }
    }
    return path;
}

QString detectMediaKind(const QString& line, QString* fileName) {
    const QString trimmedLine = normalizeHistoryLine(line);
    const QString detectedFileName = extractAttachmentName(trimmedLine);
    if (fileName) {
        *fileName = detectedFileName;
    }
    if (trimmedLine.contains(QStringLiteral("[图片]"))
        || trimmedLine.contains(QStringLiteral("发送了图片:"))
        || trimmedLine.contains(QStringLiteral("群图片:"))
        || trimmedLine.contains(QStringLiteral("图片消息:"))
        || trimmedLine.contains(QStringLiteral("图片回执:"))) {
        return QStringLiteral("image");
    }

    const QString suffix = QFileInfo(detectedFileName).suffix().toLower();
    if (QStringList{QStringLiteral("mp4"),
                    QStringLiteral("mov"),
                    QStringLiteral("avi"),
                    QStringLiteral("mkv"),
                    QStringLiteral("wmv"),
                    QStringLiteral("flv"),
                    QStringLiteral("webm")}.contains(suffix)) {
        return QStringLiteral("video");
    }
    if (QStringList{QStringLiteral("png"),
                    QStringLiteral("jpg"),
                    QStringLiteral("jpeg"),
                    QStringLiteral("bmp"),
                    QStringLiteral("gif"),
                    QStringLiteral("webp")}.contains(suffix)) {
        return QStringLiteral("image");
    }

    if (trimmedLine.contains(QStringLiteral("发送了文件:"))
        || trimmedLine.contains(QStringLiteral("发送了视频:"))
        || trimmedLine.contains(QStringLiteral("发送了图片:"))
        || trimmedLine.contains(QStringLiteral("自动保存:"))
        || trimmedLine.contains(QStringLiteral("已保存到:"))
        || trimmedLine.contains(QStringLiteral("群文件:"))
        || trimmedLine.contains(QStringLiteral("群视频:"))
        || trimmedLine.contains(QStringLiteral("附件消息:"))
        || trimmedLine.contains(QStringLiteral("文件已保存到:"))) {
        if (trimmedLine.contains(QStringLiteral("发送了图片:"))
            || trimmedLine.contains(QStringLiteral("群图片:"))) {
            return QStringLiteral("image");
        }
        if (trimmedLine.contains(QStringLiteral("发送了视频:"))
            || trimmedLine.contains(QStringLiteral("群视频:"))) {
            return QStringLiteral("video");
        }
        const QString fileSuffix = QFileInfo(detectedFileName).suffix().toLower();
        if (QStringList{QStringLiteral("png"),
                        QStringLiteral("jpg"),
                        QStringLiteral("jpeg"),
                        QStringLiteral("bmp"),
                        QStringLiteral("gif"),
                        QStringLiteral("webp")}.contains(fileSuffix)) {
            return QStringLiteral("image");
        }
        return QStringLiteral("file");
    }

    if (trimmedLine.contains(QStringLiteral("视频接收卡片")) || trimmedLine.contains(QStringLiteral("已收到视频"))) {
        return QStringLiteral("video");
    }
    if (trimmedLine.contains(QStringLiteral("图片接收卡片")) || trimmedLine.contains(QStringLiteral("已收到图片"))) {
        return QStringLiteral("image");
    }
    if (trimmedLine.contains(QStringLiteral("文件接收卡片"))
        || trimmedLine.contains(QStringLiteral("已收到 文件"))
        || trimmedLine.contains(QStringLiteral("已收到文件"))
        || trimmedLine.contains(QStringLiteral("回执话术"))) {
        return QStringLiteral("file");
    }
    return QString();
}

}
