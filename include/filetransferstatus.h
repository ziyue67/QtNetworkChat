#ifndef FILETRANSFERSTATUS_H
#define FILETRANSFERSTATUS_H

#include <QString>

struct FileTransferStatusInfo {
    QString category;
    QString title;
    QString detail;
    bool retryable = false;
};

FileTransferStatusInfo describeFileTransferReason(const QString& reason);
QString canonicalFileTransferDirection(const QString& category, const QString& direction = QString());
QString fileTransferUserMessage(const QString& reason, const QString& fallback = QString());
QString fileTransferStatusEventMessage(const QString& fileName,
                                       const QString& transferId,
                                       const QString& reason,
                                       qint64 receivedBytes = 0,
                                       qint64 totalBytes = 0);
QString fileTransferStatusDiagnostic(const QString& fileName,
                                     const QString& transferId,
                                     const QString& reason,
                                     qint64 receivedBytes = 0,
                                     qint64 totalBytes = 0);

#endif // FILETRANSFERSTATUS_H
