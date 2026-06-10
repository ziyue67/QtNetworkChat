#include "filetransferstatus.h"

#include <QStringList>

namespace {
bool containsAny(const QString& text, const QStringList& tokens) {
    for (const QString& token : tokens) {
        if (text.contains(token, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}
}

FileTransferStatusInfo describeFileTransferReason(const QString& reason) {
    const QString trimmed = reason.trimmed();
    if (trimmed.isEmpty()) {
        return {QStringLiteral("unknown"),
                QStringLiteral("文件传输状态未知"),
                QStringLiteral("服务端没有返回明确原因，可稍后重试或重新发送。"),
                true};
    }

    if (containsAny(trimmed, QStringList{QStringLiteral("receive-started"), QStringLiteral("receiving"), QString::fromUtf8("开始接收")})) {
        return {QStringLiteral("receive-started"),
                QStringLiteral("文件接收已开始"),
                QStringLiteral("接收端已收到文件分片元数据，正在按传输编号汇总分片。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("receive-completed"), QStringLiteral("assembled"), QString::fromUtf8("组包完成")})) {
        return {QStringLiteral("receive-completed"),
                QStringLiteral("文件分片已接收完成"),
                QStringLiteral("接收端已拿到全部分片并完成组包，随后会进入本机保存和完整性提示。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("receive-saved"), QStringLiteral("save-completed"), QString::fromUtf8("保存完成"), QString::fromUtf8("已保存")})) {
        return {QStringLiteral("receive-saved"),
                QStringLiteral("文件已保存到本机"),
                QStringLiteral("接收端已把文件写入下载目录，可从聊天记录双击打开或复制保存路径。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("save-failed"), QStringLiteral("receive-save-failed"), QString::fromUtf8("保存失败"), QString::fromUtf8("写入失败")})) {
        return {QStringLiteral("receive-save-failed"),
                QStringLiteral("文件保存到本机失败"),
                QStringLiteral("接收端已拿到文件数据，但写入下载目录失败。请检查目录权限、磁盘空间或安全软件拦截。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("open-failed"), QStringLiteral("receive-open-failed"), QString::fromUtf8("打开失败"), QString::fromUtf8("无法打开")})) {
        return {QStringLiteral("receive-open-failed"),
                QStringLiteral("保存文件无法打开"),
                QStringLiteral("文件可能已移动、删除或被系统拒绝打开。可从聊天记录复制保存路径后手动检查。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("prepared"), QString::fromUtf8("清单")})) {
        return {QStringLiteral("prepared"),
                QStringLiteral("文件校验清单已生成"),
                QStringLiteral("客户端已完成本地读取、分片规划和 SHA-256 校验清单生成，准备开始发送。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("resume"), QStringLiteral("resumed"), QString::fromUtf8("续传"), QString::fromUtf8("恢复")})) {
        return {QStringLiteral("resumed"),
                QStringLiteral("文件传输已按续传状态恢复"),
                QStringLiteral("客户端已确认服务端保存的进度，并会从最早缺失分片继续发送。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("completed"), QStringLiteral("delivered"), QString::fromUtf8("完成"), QString::fromUtf8("送达")})) {
        return {QStringLiteral("completed"),
                QStringLiteral("文件传输已完成"),
                QStringLiteral("所有分片都已确认，客户端已清理本地未完成发送记录。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("chunk-rejected"),
                                         QStringLiteral("chunk_ack_rejected"),
                                         QStringLiteral("chunk-ack-rejected"),
                                         QStringLiteral("chunk-ack-timeout"),
                                         QStringLiteral("chunk_ack_timeout")})) {
        return {QStringLiteral("chunk-delivery-failed"),
                QStringLiteral("文件分片未被接收端确认"),
                QStringLiteral("接收端拒绝分片或 ACK 超时。客户端会优先保留续传/离线兜底证据，必要时可稍后重试。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("timeout"), QString::fromUtf8("超时")})) {
        return {QStringLiteral("timeout"),
                QStringLiteral("文件传输等待确认超时"),
                QStringLiteral("网络或接收端响应较慢，客户端会优先查询续传状态，必要时可稍后重试。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("receiver-disconnected"), QString::fromUtf8("断开"), QString::fromUtf8("离线")})) {
        return {QStringLiteral("receiver-disconnected"),
                QStringLiteral("接收端暂时不可达"),
                QStringLiteral("对方断开或离线时，服务端会尽量保留离线兜底，待对方重新登录后回放。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("object-store-unavailable"),
                                         QStringLiteral("object-read-failed"),
                                         QStringLiteral("object-seek-failed"),
                                         QStringLiteral("read_failed"),
                                         QStringLiteral("read-failed")})) {
        return {QStringLiteral("object-readback-unavailable"),
                QStringLiteral("大文件对象暂时无法读取"),
                QStringLiteral("跨实例或 S3/MinIO 对象读回失败时，客户端会保留离线兜底状态；请稍后重试或让发送方重新发送。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("write_failed"),
                                         QStringLiteral("write-failed"),
                                         QStringLiteral("upload-failed"),
                                         QStringLiteral("put-failed")})) {
        return {QStringLiteral("object-write-failed"),
                QStringLiteral("大文件对象写入失败"),
                QStringLiteral("服务端未能写入对象存储，通常会保留源实例离线兜底；请稍后重试或切回普通文件发送路径。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("hash"), QString::fromUtf8("哈希"), QString::fromUtf8("校验")})) {
        return {QStringLiteral("integrity"),
                QStringLiteral("文件完整性校验失败"),
                QStringLiteral("文件内容或校验信息不一致。为避免错误文件送达，请重新选择文件发送。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("size"), QString::fromUtf8("大小"), QString::fromUtf8("超过")})) {
        return {QStringLiteral("size"),
                QStringLiteral("文件大小或分片大小不符合要求"),
                QStringLiteral("请确认文件未被修改，且大小在当前服务端允许范围内。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("auth"), QStringLiteral("forbidden"), QString::fromUtf8("权限"), QString::fromUtf8("发送者身份")})) {
        return {QStringLiteral("auth"),
                QStringLiteral("文件传输被权限保护拦截"),
                QStringLiteral("当前账号或会话权限不满足发送条件，请重新登录或联系管理员确认。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("not_found"), QStringLiteral("missing"), QString::fromUtf8("不存在"), QString::fromUtf8("丢失")})) {
        return {QStringLiteral("missing"),
                QStringLiteral("文件或对象已不存在"),
                QStringLiteral("服务端找不到对应文件数据，通常需要发送方重新发送。"),
                false};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("network"), QStringLiteral("retryable"), QStringLiteral("busy"), QString::fromUtf8("临时"), QString::fromUtf8("繁忙"), QString::fromUtf8("重试")})) {
        return {QStringLiteral("retryable"),
                QStringLiteral("文件传输遇到临时问题"),
                QStringLiteral("这是可重试状态，客户端会按当前策略重试，仍失败时可稍后再次发送。"),
                true};
    }
    if (containsAny(trimmed, QStringList{QStringLiteral("large_file_failed"),
                                         QStringLiteral("offer_delivery"),
                                         QStringLiteral("failed_received"),
                                         QStringLiteral("fallback"),
                                         QString::fromUtf8("兜底")})) {
        return {QStringLiteral("fallback-retained"),
                QStringLiteral("离线兜底已保留"),
                QStringLiteral("跨实例直达未完成时，源实例会保留离线兜底，避免文件丢失。"),
                true};
    }

    return {QStringLiteral("generic"),
            QStringLiteral("文件传输未完成"),
            QStringLiteral("原因：") + trimmed,
            false};
}

QString fileTransferUserMessage(const QString& reason, const QString& fallback) {
    const FileTransferStatusInfo info = describeFileTransferReason(reason);
    if (info.category == QLatin1String("unknown") && !fallback.trimmed().isEmpty()) {
        return fallback;
    }
    return QStringLiteral("%1：%2").arg(info.title, info.detail);
}

QString fileTransferStatusEventMessage(const QString& fileName,
                                       const QString& transferId,
                                       const QString& reason,
                                       qint64 receivedBytes,
                                       qint64 totalBytes) {
    const FileTransferStatusInfo info = describeFileTransferReason(reason);
    QStringList parts;
    parts << QStringLiteral("文件状态") << info.title;
    const QString trimmedFileName = fileName.trimmed();
    if (!trimmedFileName.isEmpty()) {
        parts << trimmedFileName;
    }
    if (totalBytes > 0) {
        const qint64 boundedReceived = qBound<qint64>(0, receivedBytes, totalBytes);
        parts << QStringLiteral("%1/%2 字节").arg(boundedReceived).arg(totalBytes);
    } else if (receivedBytes > 0) {
        parts << QStringLiteral("已确认 %1 字节").arg(receivedBytes);
    }
    if (info.retryable) {
        parts << QStringLiteral("可重试或等待回放");
    }
    const QString trimmedTransferId = transferId.trimmed();
    if (!trimmedTransferId.isEmpty()) {
        parts << QStringLiteral("ID:%1").arg(trimmedTransferId.left(12));
    }
    return parts.join(QStringLiteral(" · "));
}

QString fileTransferStatusDiagnostic(const QString& fileName,
                                     const QString& transferId,
                                     const QString& reason,
                                     qint64 receivedBytes,
                                     qint64 totalBytes) {
    const FileTransferStatusInfo info = describeFileTransferReason(reason);
    QStringList rows;
    rows << QStringLiteral("QtNetworkChat 文件传输诊断");
    rows << QStringLiteral("category=%1").arg(info.category);
    rows << QStringLiteral("retryable=%1").arg(info.retryable ? QStringLiteral("true") : QStringLiteral("false"));
    rows << QStringLiteral("title=%1").arg(info.title);
    rows << QStringLiteral("detail=%1").arg(info.detail);
    if (!fileName.trimmed().isEmpty()) {
        rows << QStringLiteral("fileName=%1").arg(fileName.trimmed());
    }
    if (!transferId.trimmed().isEmpty()) {
        rows << QStringLiteral("transferId=%1").arg(transferId.trimmed());
    }
    if (!reason.trimmed().isEmpty()) {
        rows << QStringLiteral("reason=%1").arg(reason.trimmed());
    }
    if (totalBytes > 0) {
        rows << QStringLiteral("receivedBytes=%1").arg(qBound<qint64>(0, receivedBytes, totalBytes));
        rows << QStringLiteral("totalBytes=%1").arg(totalBytes);
    } else if (receivedBytes > 0) {
        rows << QStringLiteral("receivedBytes=%1").arg(receivedBytes);
    }
    return rows.join('\n');
}
