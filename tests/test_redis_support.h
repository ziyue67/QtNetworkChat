#ifndef TEST_REDIS_SUPPORT_H
#define TEST_REDIS_SUPPORT_H

#include <QString>

class TestRedisServerEnvironment {
public:
    explicit TestRedisServerEnvironment(QString prefix);
    ~TestRedisServerEnvironment();

    bool start(QString* error = nullptr);
    void stop();

    quint16 port() const;
    QString prefix() const;

    void applyEnvironment() const;
    void clearEnvironment() const;

private:
    QString m_prefix;
    class Impl;
    Impl* m_impl;
};

#endif // TEST_REDIS_SUPPORT_H
