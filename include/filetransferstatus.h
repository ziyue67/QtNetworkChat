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
QString fileTransferUserMessage(const QString& reason, const QString& fallback = QString());

#endif // FILETRANSFERSTATUS_H
