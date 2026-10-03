#include "e2eenvelope.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>

#include <cstdio>

namespace {

bool writeFile(const QString& path, const QByteArray& payload, QString* error) {
    if (path.isEmpty()) {
        return true;
    }

    const QFileInfo info(path);
    const QString parentPath = info.absolutePath();
    if (!parentPath.isEmpty()) {
        QDir parent(parentPath);
        if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
            if (error) {
                *error = QStringLiteral("create-output-directory-failed");
            }
            return false;
        }
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("open-output-file-failed");
        }
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (error) {
            *error = QStringLiteral("write-output-file-failed");
        }
        return false;
    }
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("commit-output-file-failed");
        }
        return false;
    }
    return true;
}

QJsonObject copyFields(const QJsonObject& source, const QStringList& keys) {
    QJsonObject result;
    for (const QString& key : keys) {
        if (source.contains(key)) {
            result.insert(key, source.value(key));
        }
    }
    return result;
}

QJsonArray stringArray(std::initializer_list<const char*> values) {
    QJsonArray array;
    for (const char* value : values) {
        array.append(QString::fromLatin1(value));
    }
    return array;
}

QString inlineArray(const QJsonArray& values) {
    QStringList items;
    for (const QJsonValue& value : values) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            items.append(QStringLiteral("`") + text + QStringLiteral("`"));
        }
    }
    return items.isEmpty() ? QStringLiteral("`none`") : items.join(QStringLiteral(", "));
}

QJsonObject buildEvidence(const QJsonObject& backendStatus) {
    const QJsonObject rollout =
        backendStatus.value(QStringLiteral("productionRolloutObservability")).toObject();
    const QJsonObject acceptance =
        backendStatus.value(QStringLiteral("productionAcceptance")).toObject();

    const bool accepted = rollout.value(QStringLiteral("accepted")).toBool(false);
    const QString releaseGate = rollout.value(QStringLiteral("releaseGate")).toString();
    const QString operatorAction =
        rollout.value(QStringLiteral("operatorAction")).toString(QStringLiteral("unknown"));
    const bool noSensitiveExportProof =
        rollout.value(QStringLiteral("noSensitiveExportProof")).toBool(false);
    const bool sensitiveFieldsSuppressed =
        rollout.value(QStringLiteral("sensitiveFieldsSuppressed")).toBool(false)
        && !rollout.value(QStringLiteral("rawKeyExported")).toBool(true)
        && !rollout.value(QStringLiteral("privateMaterialExported")).toBool(true)
        && !rollout.value(QStringLiteral("sessionSecretExported")).toBool(true)
        && !rollout.value(QStringLiteral("privateIdentityMaterialExported")).toBool(true)
        && !rollout.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(true)
        && !rollout.value(QStringLiteral("plaintextBytesExported")).toBool(true)
        && !rollout.value(QStringLiteral("ciphertextBytesExported")).toBool(true);
    const bool ok = accepted && noSensitiveExportProof && sensitiveFieldsSuppressed;

    QJsonObject summary;
    summary[QStringLiteral("readiness")] =
        ok ? QStringLiteral("verified") : QStringLiteral("blocked");
    summary[QStringLiteral("operatorAction")] = operatorAction;
    summary[QStringLiteral("releaseRunObservable")] =
        rollout.value(QStringLiteral("releaseRunObservable")).toBool(false);
    summary[QStringLiteral("statusCapturePolicy")] =
        rollout.value(QStringLiteral("statusCapturePolicy")).toString();
    summary[QStringLiteral("filesystemObjectRecoveryReady")] =
        rollout.value(QStringLiteral("filesystemObjectRecoveryReady")).toBool(false);
    summary[QStringLiteral("filesystemObjectRecoveryReleaseGate")] =
        rollout.value(QStringLiteral("filesystemObjectRecoveryReleaseGate")).toString();
    summary[QStringLiteral("filesystemObjectRecoveryAction")] =
        rollout.value(QStringLiteral("filesystemObjectRecoveryAction")).toString();
    summary[QStringLiteral("filesystemObjectRecoveryNoSensitiveExportProof")] =
        rollout.value(QStringLiteral("filesystemObjectRecoveryNoSensitiveExportProof")).toBool(false);
    summary[QStringLiteral("offlineObjectRecoveryReady")] =
        rollout.value(QStringLiteral("offlineObjectRecoveryReady")).toBool(false);
    summary[QStringLiteral("offlineObjectRecoveryScope")] =
        rollout.value(QStringLiteral("offlineObjectRecoveryScope")).toString();
    summary[QStringLiteral("offlineObjectRecoveryReleaseGate")] =
        rollout.value(QStringLiteral("offlineObjectRecoveryReleaseGate")).toString();
    summary[QStringLiteral("offlineObjectRecoveryBlockedReason")] =
        rollout.value(QStringLiteral("offlineObjectRecoveryBlockedReason")).toString();
    summary[QStringLiteral("offlineObjectRecoveryAction")] =
        rollout.value(QStringLiteral("offlineObjectRecoveryAction")).toString();
    summary[QStringLiteral("offlineObjectRecoveryCapturePolicy")] =
        rollout.value(QStringLiteral("offlineObjectRecoveryCapturePolicy")).toString();
    summary[QStringLiteral("offlineObjectRecoveryNoSensitiveExportProof")] =
        rollout.value(QStringLiteral("offlineObjectRecoveryNoSensitiveExportProof")).toBool(false);

    QJsonObject auditSummary;
    auditSummary[QStringLiteral("releaseGate")] = releaseGate;
    auditSummary[QStringLiteral("auditFocus")] = stringArray({
        "production-crypto-acceptance",
        "rollout-observability",
        "no-sensitive-export-proof",
        "public-primitive-execution",
        "filesystem-object-ciphertext-readback",
        "reviewed-offline-ciphertext-readback",
    });
    auditSummary[QStringLiteral("evidenceBundle")] = stringArray({
        "e2e-rollout-observability.json",
        "e2e-rollout-observability.md",
    });
    auditSummary[QStringLiteral("writeIntent")] = QStringLiteral("sanitized-status-only");
    auditSummary[QStringLiteral("backupRequired")] = false;

    QJsonObject acceptanceSummary = copyFields(acceptance, {
        QStringLiteral("schema"),
        QStringLiteral("backendId"),
        QStringLiteral("providerId"),
        QStringLiteral("operationContractVersion"),
        QStringLiteral("linked"),
        QStringLiteral("productionReady"),
        QStringLiteral("accepted"),
        QStringLiteral("releaseGate"),
        QStringLiteral("blockedReason"),
        QStringLiteral("operatorAction"),
        QStringLiteral("requiredOperationCount"),
        QStringLiteral("availableOperationCount"),
        QStringLiteral("blockedOperationCount"),
        QStringLiteral("providerTableAccepted"),
        QStringLiteral("providerOperationPreflightAccepted"),
        QStringLiteral("providerInvocationExecutionAccepted"),
        QStringLiteral("providerDataPlaneBridgeReadyCount"),
        QStringLiteral("providerPublicPrimitiveExecutionReadyCount"),
        QStringLiteral("providerPublicPrimitiveExecutionBlockedCount"),
        QStringLiteral("rawKeyExported"),
        QStringLiteral("privateMaterialExported"),
    });

    QJsonObject sensitiveExportProof;
    sensitiveExportProof[QStringLiteral("noSensitiveExportProof")] = noSensitiveExportProof;
    sensitiveExportProof[QStringLiteral("sensitiveFieldsSuppressed")] =
        sensitiveFieldsSuppressed;
    sensitiveExportProof[QStringLiteral("rawKeyExported")] =
        rollout.value(QStringLiteral("rawKeyExported")).toBool(false);
    sensitiveExportProof[QStringLiteral("privateMaterialExported")] =
        rollout.value(QStringLiteral("privateMaterialExported")).toBool(false);
    sensitiveExportProof[QStringLiteral("sessionSecretExported")] =
        rollout.value(QStringLiteral("sessionSecretExported")).toBool(false);
    sensitiveExportProof[QStringLiteral("privateIdentityMaterialExported")] =
        rollout.value(QStringLiteral("privateIdentityMaterialExported")).toBool(false);
    sensitiveExportProof[QStringLiteral("fullPublicIdentityMaterialExported")] =
        rollout.value(QStringLiteral("fullPublicIdentityMaterialExported")).toBool(false);
    sensitiveExportProof[QStringLiteral("plaintextBytesExported")] =
        rollout.value(QStringLiteral("plaintextBytesExported")).toBool(false);
    sensitiveExportProof[QStringLiteral("ciphertextBytesExported")] =
        rollout.value(QStringLiteral("ciphertextBytesExported")).toBool(false);
    sensitiveExportProof[QStringLiteral("localPrivatePathExported")] = false;
    sensitiveExportProof[QStringLiteral("fullFingerprintExported")] = false;

    QJsonObject releaseRun;
    releaseRun[QStringLiteral("tool")] =
        QStringLiteral("e2e_rollout_observability_exporter");
    releaseRun[QStringLiteral("productionRequired")] = e2eProductionCryptoRequired();
    releaseRun[QStringLiteral("requestedBackendId")] =
        backendStatus.value(QStringLiteral("requestedBackendId")).toString();
    releaseRun[QStringLiteral("selectedBackendId")] =
        backendStatus.value(QStringLiteral("selectedBackendId")).toString();
    releaseRun[QStringLiteral("selectionSource")] =
        backendStatus.value(QStringLiteral("selectionSource")).toString();
    releaseRun[QStringLiteral("capturePolicy")] =
        QStringLiteral("release-gates-counts-actions-and-prompts-only");
    releaseRun[QStringLiteral("persisted")] = true;

    QJsonObject evidence;
    evidence[QStringLiteral("format")] =
        QStringLiteral("qtnetworkchat-e2e-production-rollout-observability-evidence-v1");
    evidence[QStringLiteral("generatedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    evidence[QStringLiteral("status")] =
        ok ? QStringLiteral("ready") : QStringLiteral("blocked");
    evidence[QStringLiteral("ok")] = ok;
    evidence[QStringLiteral("backendId")] = rollout.value(QStringLiteral("backendId")).toString();
    evidence[QStringLiteral("providerId")] =
        rollout.value(QStringLiteral("providerId")).toString();
    evidence[QStringLiteral("summary")] = summary;
    evidence[QStringLiteral("auditSummary")] = auditSummary;
    evidence[QStringLiteral("releaseRun")] = releaseRun;
    evidence[QStringLiteral("productionAcceptanceSummary")] = acceptanceSummary;
    evidence[QStringLiteral("productionRolloutObservability")] = rollout;
    evidence[QStringLiteral("sensitiveExportProof")] = sensitiveExportProof;
    return evidence;
}

QString renderMarkdown(const QJsonObject& evidence) {
    const QJsonObject summary = evidence.value(QStringLiteral("summary")).toObject();
    const QJsonObject auditSummary = evidence.value(QStringLiteral("auditSummary")).toObject();
    const QJsonObject acceptance =
        evidence.value(QStringLiteral("productionAcceptanceSummary")).toObject();
    const QJsonObject rollout =
        evidence.value(QStringLiteral("productionRolloutObservability")).toObject();
    const QJsonObject proof = evidence.value(QStringLiteral("sensitiveExportProof")).toObject();
    const QJsonObject releaseRun = evidence.value(QStringLiteral("releaseRun")).toObject();

    QString output;
    QTextStream stream(&output);
    stream << "# QtNetworkChat E2E Production Rollout Observability Evidence\n\n";
    stream << "- Format: `" << evidence.value(QStringLiteral("format")).toString() << "`\n";
    stream << "- Generated at: `" << evidence.value(QStringLiteral("generatedAt")).toString() << "`\n";
    stream << "- Status: `" << evidence.value(QStringLiteral("status")).toString() << "`\n";
    stream << "- Accepted: `" << (evidence.value(QStringLiteral("ok")).toBool(false) ? "true" : "false") << "`\n";
    stream << "- Release gate: `" << auditSummary.value(QStringLiteral("releaseGate")).toString() << "`\n";
    stream << "- Backend: `" << evidence.value(QStringLiteral("backendId")).toString() << "`\n";
    stream << "- Provider: `" << evidence.value(QStringLiteral("providerId")).toString() << "`\n";
    stream << "- Selected backend: `"
           << releaseRun.value(QStringLiteral("selectedBackendId")).toString()
           << "` via `" << releaseRun.value(QStringLiteral("selectionSource")).toString() << "`\n";
    stream << "- Capture policy: `"
           << summary.value(QStringLiteral("statusCapturePolicy")).toString() << "`\n";
    stream << "- Production acceptance: accepted=`"
           << (acceptance.value(QStringLiteral("accepted")).toBool(false) ? "true" : "false")
           << "`, releaseGate=`" << acceptance.value(QStringLiteral("releaseGate")).toString()
           << "`, ready=`"
           << (acceptance.value(QStringLiteral("productionReady")).toBool(false) ? "true" : "false")
           << "`\n";
    stream << "- Proof counts: materialExport=`"
           << rollout.value(QStringLiteral("materialExportProofCount")).toInt()
           << "`, outputShape=`" << rollout.value(QStringLiteral("outputShapeProofCount")).toInt()
           << "`, publicPrimitiveReady=`"
           << rollout.value(QStringLiteral("publicPrimitiveReadyCount")).toInt()
           << "`, publicPrimitiveBlocked=`"
           << rollout.value(QStringLiteral("publicPrimitiveBlockedCount")).toInt() << "`\n";
    stream << "- Sensitive export proof: noSensitiveExport=`"
           << (proof.value(QStringLiteral("noSensitiveExportProof")).toBool(false) ? "true" : "false")
           << "`, rawKey=`"
           << (proof.value(QStringLiteral("rawKeyExported")).toBool(false) ? "true" : "false")
           << "`, privateMaterial=`"
           << (proof.value(QStringLiteral("privateMaterialExported")).toBool(false) ? "true" : "false")
           << "`, sessionSecret=`"
           << (proof.value(QStringLiteral("sessionSecretExported")).toBool(false) ? "true" : "false")
           << "`, plaintext=`"
           << (proof.value(QStringLiteral("plaintextBytesExported")).toBool(false) ? "true" : "false")
           << "`, ciphertext=`"
           << (proof.value(QStringLiteral("ciphertextBytesExported")).toBool(false) ? "true" : "false")
           << "`\n";
    stream << "- Operator prompts: "
           << inlineArray(rollout.value(QStringLiteral("operatorRecoveryPrompts")).toArray())
           << "\n";
    stream << "- User prompts: "
           << inlineArray(rollout.value(QStringLiteral("userRecoveryPrompts")).toArray())
           << "\n";
    stream << "- Filesystem object recovery ready: `"
           << (summary.value(QStringLiteral("filesystemObjectRecoveryReady")).toBool(false)
                   ? "true"
                   : "false")
           << "`\n";
    stream << "- Filesystem object recovery gate: `"
           << summary.value(QStringLiteral("filesystemObjectRecoveryReleaseGate")).toString()
           << "`\n";
    stream << "- Filesystem object recovery action: `"
           << summary.value(QStringLiteral("filesystemObjectRecoveryAction")).toString()
           << "`\n";
    stream << "- Offline/object recovery ready: `"
           << (summary.value(QStringLiteral("offlineObjectRecoveryReady")).toBool(false) ? "true" : "false")
           << "`\n";
    stream << "- Offline/object recovery scope: `"
           << summary.value(QStringLiteral("offlineObjectRecoveryScope")).toString() << "`\n";
    stream << "- Offline/object recovery gate: `"
           << summary.value(QStringLiteral("offlineObjectRecoveryReleaseGate")).toString() << "`\n";
    stream << "- Offline/object recovery action: `"
           << summary.value(QStringLiteral("offlineObjectRecoveryAction")).toString() << "`\n";
    stream << "- Offline/object recovery capture policy: `"
           << summary.value(QStringLiteral("offlineObjectRecoveryCapturePolicy")).toString() << "`\n";
    return output;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("e2e_rollout_observability_exporter"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Write sanitized E2E production rollout observability release-run evidence."));
    parser.addHelpOption();
    QCommandLineOption backendOption(QStringLiteral("backend"),
                                     QStringLiteral("Set QTNETWORKCHAT_E2E_CRYPTO_BACKEND before capture."),
                                     QStringLiteral("backend"));
    QCommandLineOption jsonOption(QStringLiteral("json"),
                                  QStringLiteral("Write sanitized JSON evidence to path."),
                                  QStringLiteral("path"));
    QCommandLineOption markdownOption(QStringLiteral("markdown"),
                                      QStringLiteral("Write sanitized Markdown evidence to path."),
                                      QStringLiteral("path"));
    QCommandLineOption requireAcceptedOption(QStringLiteral("require-accepted"),
                                             QStringLiteral("Exit non-zero unless rollout evidence is accepted."));
    QCommandLineOption printJsonOption(QStringLiteral("print-json"),
                                       QStringLiteral("Print JSON evidence to stdout."));
    parser.addOption(backendOption);
    parser.addOption(jsonOption);
    parser.addOption(markdownOption);
    parser.addOption(requireAcceptedOption);
    parser.addOption(printJsonOption);
    parser.process(app);

    const QString backend = parser.value(backendOption).trimmed();
    if (!backend.isEmpty()) {
        qputenv("QTNETWORKCHAT_E2E_CRYPTO_BACKEND", backend.toUtf8());
    }

    const QJsonObject evidence = buildEvidence(e2eCryptoBackendStatus());
    const QByteArray json = QJsonDocument(evidence).toJson(QJsonDocument::Indented);
    const QByteArray markdown = renderMarkdown(evidence).toUtf8();

    QString error;
    if (!writeFile(parser.value(jsonOption), json, &error)
        || !writeFile(parser.value(markdownOption), markdown, &error)) {
        QTextStream(stderr) << error << "\n";
        return 2;
    }

    if (parser.isSet(printJsonOption)) {
        QTextStream(stdout) << QString::fromUtf8(json);
    }

    if (parser.isSet(requireAcceptedOption) && !evidence.value(QStringLiteral("ok")).toBool(false)) {
        QTextStream(stderr) << "production-rollout-observability-not-accepted\n";
        return 3;
    }

    return 0;
}
