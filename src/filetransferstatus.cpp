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

    if (containsAny(trimmed, QStringList{QStringLiteral("timeout"), QString::fromUtf8("超时"), QStringLiteral("chunk-ack-timeout")})) {
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
    if (containsAny(trimmed, QStringList{QStringLiteral("fallback"), QString::fromUtf8("兜底")})) {
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
