#ifndef E2EENVELOPE_H
#define E2EENVELOPE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include "qtnetworkchat_e2e_provider_api.h"

struct E2EKeyAgreement {
    QString protocol;
    QString suite;
    QString senderId;
    QString receiverId;
    QString keyId;
    QByteArray publicKey;
    QString senderIdentityFingerprint;
    QString receiverIdentityFingerprint;
    QByteArray signature;

    bool isValid(QString* reason = nullptr) const;
    QJsonObject toJson() const;
    static E2EKeyAgreement fromJson(const QJsonObject& obj);
};

struct E2EEnvelope {
    QString protocol;
    QString suite;
    QString senderId;
    QString receiverId;
    QString keyId;
    QByteArray nonce;
    QByteArray ciphertext;
    QByteArray tag;
    QString aad;

    bool isValid(QString* reason = nullptr) const;
    QJsonObject toJson() const;
    static E2EEnvelope fromJson(const QJsonObject& obj);
};

QString normalizedE2EProtocol(QString protocol);
QString normalizedE2ESuite(QString suite);
QString e2eFingerprint(const QByteArray& value);
bool isSupportedE2EProtocol(const QString& protocol);
bool isSupportedE2ESuite(const QString& suite);
QString e2eDefaultSuite();
QString e2eCryptoBackendId();
QString e2eAgreementSignatureSuite();
bool e2eProductionCryptoRequired();
bool e2eCryptoBackendAvailable(QString* reason = nullptr);
QJsonObject e2eCryptoBackendStatus();
QJsonObject e2eProductionCryptoAcceptanceStatus();
QJsonObject e2eProductionCryptoOperationHarnessStatus();
QJsonObject e2eProductionCryptoOperationExecutionPlanStatus();
QJsonObject e2eProductionCryptoOperationInvocationStatus();
QJsonObject e2eProductionCryptoOperationSlotStatus();
QJsonObject e2eProductionCryptoOperationDispatchBindingStatus();
QJsonObject e2eProductionCryptoOperationCallableManifestStatus();
QJsonObject e2eProductionCryptoOperationExecutionResultStatus();
QJsonObject e2eProductionCryptoProviderTableStatus();
QJsonObject e2eProductionCryptoProviderTableBindingProbeStatus();
QJsonObject e2eProductionCryptoProviderTableRegistrationStatus();
QJsonObject e2eProductionCryptoProviderOperationPreflightStatus();
QJsonObject e2eProductionCryptoProviderCallFrameStatus();
QJsonObject e2eProductionCryptoProviderInvocationDryRunStatus();
QJsonObject e2eProductionCryptoProviderInvocationResultStatus();
QJsonObject e2eProductionCryptoProviderExecutionDecisionStatus();
QJsonObject e2eProductionCryptoProviderCallbackHarnessStatus();
QJsonObject e2eProductionCryptoProviderVectorSelfTestStatus();
QJsonObject e2eProductionCryptoProviderExecutionSlotBindingStatus();
QJsonObject e2eProductionCryptoProviderExecutionPathStatus();
QJsonObject e2eProductionCryptoProviderInvocationSandboxStatus();
QJsonObject e2eProductionCryptoProviderInvocationVectorResultStatus();
QJsonObject e2eProductionCryptoProviderInvocationExecutionStatus();
QJsonObject e2eProductionCryptoProviderReviewedExecutionCandidateStatus();
QJsonObject e2eProductionCryptoProviderReviewedCallHandoffStatus();
QJsonObject e2eProductionCryptoProviderReviewedOperationStubBoundaryStatus();
QJsonObject e2eProbeProductionCryptoProviderInvocationExecution();
QJsonObject e2eProbeProductionCryptoProviderReviewedExecutionCandidate();
QJsonObject e2eProbeProductionCryptoProviderReviewedCallHandoff();
QJsonObject e2eProbeProductionCryptoProviderReviewedOperationStubBoundary();
QJsonObject e2eValidateProductionProviderTable(const qnc_e2e_provider_table_v1* table);
QJsonObject e2eRegisterProductionProviderTable(const qnc_e2e_provider_table_v1* table);
QByteArray generateE2ESessionKey();
QByteArray generateE2EPrivateKey();
QByteArray e2ePublicKeyFromPrivateKey(const QByteArray& privateKey);
bool signE2EKeyAgreement(E2EKeyAgreement* agreement,
                         const QByteArray& identityPrivateKey,
                         QString* reason = nullptr);
bool verifyE2EKeyAgreementSignature(const E2EKeyAgreement& agreement,
                                    const QByteArray& identityPublicKey,
                                    QString* reason = nullptr);
QByteArray deriveE2EAuthenticatedSessionKey(const QByteArray& localPrivateKey,
                                            const E2EKeyAgreement& localAgreement,
                                            const E2EKeyAgreement& remoteAgreement,
                                            QString* reason = nullptr);
E2EEnvelope encryptE2EText(const QString& senderId,
                           const QString& receiverId,
                           const QString& keyId,
                           const QByteArray& sessionKey,
                           const QString& plaintext,
                           QString* reason = nullptr);
bool decryptE2EText(const E2EEnvelope& envelope,
                    const QByteArray& sessionKey,
                    QString* plaintext,
                    QString* reason = nullptr);
E2EEnvelope encryptE2EPayload(const QString& senderId,
                              const QString& receiverId,
                              const QString& keyId,
                              const QByteArray& sessionKey,
                              const QByteArray& plaintext,
                              const QString& aad,
                              QString* reason = nullptr);
bool decryptE2EPayload(const E2EEnvelope& envelope,
                       const QByteArray& sessionKey,
                       QByteArray* plaintext,
                       QString* reason = nullptr);

#endif // E2EENVELOPE_H
