#pragma once

#include <QString>

namespace HistoryAttachmentParser {

QString normalizeHistoryLine(const QString& value);
QString extractAttachmentName(const QString& line);
QString extractAttachmentPath(const QString& line, const QString& toolTipText = QString());
QString detectMediaKind(const QString& line, QString* fileName = nullptr);

}
