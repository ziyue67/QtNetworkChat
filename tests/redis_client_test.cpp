#include "redisclient.h"

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

int main() {
    bool ok = true;

    const QByteArray command = RedisClient::encodeCommand({
        QByteArrayLiteral("SET"),
        QByteArrayLiteral("qtchat:presence:10001"),
        QByteArrayLiteral("{\"userName\":\"Alice\"}"),
        QByteArrayLiteral("EX"),
        QByteArrayLiteral("90")
    });
    ok = expect(command.startsWith("*5\r\n$3\r\nSET\r\n"),
                "SET command should use RESP array framing") && ok;
    ok = expect(command.contains("$21\r\nqtchat:presence:10001\r\n"),
                "presence key should be encoded as a bulk string") && ok;
    ok = expect(command.endsWith("$2\r\n90\r\n"),
                "TTL argument should be encoded as a bulk string") && ok;

    RedisClient::Reply reply;
    int consumed = 0;
    QString error;
    ok = expect(RedisClient::parseReply("+PONG\r\ntrail", &reply, &consumed, &error),
                "simple string reply should parse") && ok;
    ok = expect(reply.type == RedisClient::ReplyType::SimpleString && reply.value == "PONG",
                "simple string reply should expose PONG value") && ok;
    ok = expect(consumed == 7, "parser should report consumed bytes") && ok;

    ok = expect(RedisClient::parseReply(":2\r\n", &reply, nullptr, &error),
                "integer reply should parse") && ok;
    ok = expect(reply.type == RedisClient::ReplyType::Integer && reply.integer == 2,
                "integer reply should expose numeric value") && ok;

    ok = expect(RedisClient::parseReply("$5\r\nhello\r\n", &reply, nullptr, &error),
                "bulk string reply should parse") && ok;
    ok = expect(reply.type == RedisClient::ReplyType::BulkString && reply.value == "hello",
                "bulk string reply should expose payload") && ok;

    ok = expect(RedisClient::parseReply("-NOAUTH Authentication required.\r\n", &reply, nullptr, &error),
                "error reply should parse") && ok;
    ok = expect(reply.isError() && reply.error.contains("NOAUTH"),
                "error reply should expose error text") && ok;

    ok = expect(RedisClient::parseReply("*2\r\n+OK\r\n:7\r\n", &reply, nullptr, &error),
                "array reply should parse") && ok;
    ok = expect(reply.type == RedisClient::ReplyType::Array
                    && reply.elements.size() == 2
                    && reply.elements[0].isSimpleString("OK")
                    && reply.elements[1].integer == 7,
                "array reply should expose nested replies") && ok;

    ok = expect(!RedisClient::parseReply("$5\r\nhe", &reply, nullptr, &error),
                "incomplete bulk string should be rejected") && ok;
    ok = expect(error == "incomplete",
                "incomplete reply should report incomplete parse state") && ok;

    return ok ? 0 : 1;
}
