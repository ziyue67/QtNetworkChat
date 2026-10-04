#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "sessionitemdelegate.h"
#include "sessionlistbuilder.h"
#include "chatbubbledelegate.h"
#include "qqnt_log.h"
#include "qqnt_backend_service.h"
#include "dialogs/friendmanagerdialog.h"
#include "dialogs/creategroupdialog.h"
#include "dialogs/addfrienddialog.h"
#include "dialogs/globalsearchdialog.h"
#include "dialogs/memberprofilecard.h"
#include "dialogs/groupnicknamedialog.h"
#include "dialogs/essencepanel.h"
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QApplication>
#include <QDateTime>
#include <QDebug>

#include "chatsessionmanager.h"
#include "chatcontextmanager.h"
#include "composermanager.h"
#include "filetransferstatus.h"
#include "localfilemanager.h"
#include "notificationpanelmanager.h"
#include "qtnetworkchat_version.h"
#include "transferchatitemrenderer.h"
#include "windowstatemanager.h"
#include "views/messagesview.h"
#include "views/contactsview.h"
#include "widgets/contactlistwidget.h"
#include "views/favoritesview.h"
#include "views/settingsview.h"
#include "views/profileview.h"
#include "widgets/appnav.h"
#include "widgets/titlebar.h"
#include "widgets/composerwidget.h"
#include "widgets/groupmembersidebar.h"
#include "widgets/avatarlabel.h"
#include "widgets/dialogtitlebar.h"
#include "theme/thememanager.h"
#include "theme/dialogstyle.h"
#include "windows/screenshotcapturewindow.h"
#include "screenshotgeometry.h"
#include "windows/imagepreviewwindow.h"
#include "windows/forwardwindow.h"
#include "dialogs/mutedurationdialog.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QMenu>
#include <QAction>
#include <QCloseEvent>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QListView>
#include <QStatusBar>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QLinearGradient>
#include <QPolygonF>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QTabWidget>
#include <QShortcut>
#include <QUrl>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QProgressDialog>
#include <QCheckBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QComboBox>
#include <QFrame>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QBuffer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QScreen>
#include <QStyleHints>
#include <QPalette>
#include <QWindow>
#include <functional>
#include <algorithm>

#include "mainwindow_support.h"

using namespace MainWindowSupport;

bool MainWindow::sendTransferWithProgress(const QString& filePath,
                                          const QString& receiverId,
                                          const QString& targetName,
                                          const QString& kind,
                                          bool asImage,
                                          QString* transferSummary,
                                          bool* canceled) {
    if (!m_client) return false;

    const QFileInfo info(filePath);
    constexpr int maxAttempts = 3;
    if (transferSummary) transferSummary->clear();
    if (canceled) *canceled = false;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        QString preparedSummary;
        bool cancelRequested = false;
        QProgressDialog progress(this);
        progress.setWindowTitle(QString("发送%1").arg(kind));
        progress.setLabelText(m_transferManager.sendingInitialState(kind, info.fileName(), targetName).labelText);
        progress.setRange(0, 100);
        progress.setValue(0);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        progress.setCancelButtonText("取消发送");
        progress.show();
        QApplication::processEvents();
        setTransferWorkspaceState(m_transferManager.preparingSendWorkspaceState(kind,
                                                                                info.fileName(),
                                                                                LocalFileManager::humanFileSize(info.size()),
                                                                                targetName));

        QMetaObject::Connection cancelConnection = connect(
            &progress,
            &QProgressDialog::canceled,
            this,
            [this, &progress, &info, &kind, &cancelRequested, canceled, &targetName]() {
                cancelRequested = true;
                if (canceled) *canceled = true;
                progress.setLabelText(m_transferManager.sendingCancelState(kind, info.fileName()).labelText);
                if (m_client) m_client->cancelCurrentOutgoingTransfer();
                setTransferWorkspaceState(m_transferManager.canceledSendWorkspaceState(kind, info.fileName(), targetName));
                ui->statusbar->showMessage(QString("正在取消发送%1：%2").arg(kind, info.fileName()), 1600);
                QApplication::processEvents();
            });
        QMetaObject::Connection progressConnection = connect(
            m_client,
            &Client::fileTransferProgress,
            this,
            [this, &progress, &info, &targetName, &kind](const QString& fileName, qint64 bytesPrepared, qint64 totalBytes) {
                if (fileName != info.fileName()) return;
                const TransferProgressUiState state = m_transferManager.sendingProgressState(kind, fileName, targetName, bytesPrepared, totalBytes);
                progress.setValue(state.percent);
                progress.setLabelText(state.labelText);
                setTransferWorkspaceState(m_transferManager.sendingProgressWorkspaceState(kind,
                                                                                          fileName,
                                                                                          targetName,
                                                                                          bytesPrepared,
                                                                                          totalBytes));
                QApplication::processEvents();
            });
        QMetaObject::Connection preparedConnection = connect(
            m_client,
            &Client::fileTransferPrepared,
            this,
            [this, &progress, &info, &targetName, &kind, &preparedSummary](const QString& fileName,
                                                                      qint64 totalBytes,
                                                                      qint64 chunkSize,
                                                                      qint64 chunkCount,
                                                                      const QString& fileHash) {
                if (fileName != info.fileName()) return;
                const TransferProgressUiState state = m_transferManager.sendingPreparedState(kind, fileName, targetName, totalBytes, chunkSize, chunkCount, fileHash);
                preparedSummary = state.manifestSummary;
                progress.setLabelText(state.labelText);
                setTransferWorkspaceState(m_transferManager.sendingPreparedWorkspaceState(kind,
                                                                                          fileName,
                                                                                          targetName,
                                                                                          totalBytes,
                                                                                          chunkSize,
                                                                                          chunkCount,
                                                                                          fileHash));
                QApplication::processEvents();
            });

        const bool ok = asImage
            ? m_client->sendImage(filePath, receiverId)
            : m_client->sendFile(filePath, receiverId);

        QObject::disconnect(progressConnection);
        QObject::disconnect(preparedConnection);
        QObject::disconnect(cancelConnection);
        progress.setValue(ok ? 100 : progress.value());
        QApplication::processEvents();
        progress.close();

        if (cancelRequested && !ok) {
            if (transferSummary) *transferSummary = "已取消";
            if (canceled) *canceled = true;
            return false;
        }

        if (ok) {
            if (transferSummary) *transferSummary = preparedSummary;
            return true;
        }

        if (attempt < maxAttempts) {
            const QMessageBox::StandardButton retry = QMessageBox::warning(
                this,
                QString("%1发送失败").arg(kind),
                QString("%1“%2”发送失败，是否立即重试？\n当前为第 %3 次，共最多 %4 次。")
                    .arg(kind, info.fileName())
                    .arg(attempt)
                    .arg(maxAttempts),
                QMessageBox::Retry | QMessageBox::Cancel,
                QMessageBox::Retry);
            if (retry == QMessageBox::Retry) {
                ui->statusbar->showMessage(QString("正在重试发送%1：%2").arg(kind, info.fileName()), 1800);
                setTransferWorkspaceState(m_transferManager.failedSendWorkspaceState(kind,
                                                                                     info.fileName(),
                                                                                     LocalFileManager::humanFileSize(info.size()),
                                                                                     targetName));
                continue;
            }
        }

        return false;
    }

    return false;
}

void MainWindow::onFileTransferStatusChanged(const QString& fileName,
                                             const QString& transferId,
                                             const QString& reason,
                                             qint64 receivedBytes,
                                             qint64 totalBytes) {
    showFileTransferStatusEvent(fileName, transferId, reason, receivedBytes, totalBytes);
}

void MainWindow::showFileTransferStatusEvent(const QString& fileName,
                                             const QString& transferId,
                                             const QString& reason,
                                             qint64 receivedBytes,
                                             qint64 totalBytes) {
    const TransferStatusEvent event = m_transferManager.statusEvent(fileName, transferId, reason, receivedBytes, totalBytes);
    m_lastTransferStatusDiagnostic = event.diagnostic;
    applyTransferActionState(m_copyLastTransferStatusAction, event.copyDiagnostic.action);
    setTransferWorkspaceState(m_transferManager.statusWorkspaceState(fileName,
                                                                     transferId,
                                                                     reason,
                                                                     receivedBytes,
                                                                     totalBytes));
    // Transfer progress is useful in the status bar/workspace card, but placing
    // every image chunk event in the chat created the dark diagnostic bars seen
    // above image and screenshot bubbles.
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    const bool image = QStringList{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                   QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp")}.contains(suffix);
    if (!image) {
        appendSystemMessage(event.message);
    }
    ui->chatHintLabel->setText(event.chatHintText);
    ui->statusbar->showMessage(event.statusBarMessage, event.statusBarTimeoutMs);
}

void MainWindow::updateSavedOutgoingTransferRecoveryUi(bool announce) {
    if (!m_resumeSavedTransferAction || !m_clearSavedTransferAction) return;

    QJsonObject state;
    const bool hasSavedTransfer = m_client && m_client->loadOutgoingTransferState(&state);
    const QJsonObject recoveryStatus = hasSavedTransfer ? m_client->savedOutgoingTransferRecoveryStatus() : QJsonObject();
    const TransferRecoveryUiState uiState = m_transferManager.recoveryUiState(
        hasSavedTransfer,
        m_client && m_client->isConnected(),
        state,
        recoveryStatus,
        announce);
    setTransferWorkspaceState(hasSavedTransfer
        ? m_transferManager.recoveryWorkspaceState(uiState)
        : m_transferManager.idleWorkspaceState(m_client && m_client->isConnected(), false, false));
    applyTransferActionState(m_resumeSavedTransferAction, uiState.resumeAction);
    applyTransferActionState(m_clearSavedTransferAction, uiState.clearAction);

    if (announce && !uiState.announceMessage.isEmpty()) {
        appendSystemMessage(uiState.announceMessage);
        ui->statusbar->showMessage(uiState.statusMessage, uiState.canAutoResume ? 3200 : 3600);
    }
}

void MainWindow::onClearSavedOutgoingTransfer() {
    if (!m_client) return;

    QJsonObject state;
    if (!m_client->loadOutgoingTransferState(&state)) {
        updateSavedOutgoingTransferRecoveryUi(false);
        ui->statusbar->showMessage(m_transferManager.clearRecoveryPrompt(QJsonObject()).noSavedStatusMessage, 2200);
        return;
    }

    const TransferClearRecoveryPrompt prompt = m_transferManager.clearRecoveryPrompt(state);
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        prompt.title,
        prompt.message,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        ui->statusbar->showMessage(prompt.keptStatusMessage, 1800);
        return;
    }

    if (m_client->clearOutgoingTransferState()) {
        appendSystemMessage(prompt.clearedSystemMessage);
        setTransferWorkspaceState(m_transferManager.clearedRecoveryWorkspaceState(prompt.fileName));
        ui->statusbar->showMessage(prompt.clearedStatusMessage, 2200);
    } else {
        appendSystemMessage(prompt.clearFailedSystemMessage);
        ui->statusbar->showMessage(prompt.clearFailedStatusMessage, 2600);
    }
    updateSavedOutgoingTransferRecoveryUi(false);
}

void MainWindow::onResumeSavedOutgoingTransfer() {
    if (!m_client) return;

    QJsonObject state;
    if (!m_client->loadOutgoingTransferState(&state)) {
        updateSavedOutgoingTransferRecoveryUi(false);
        ui->statusbar->showMessage("暂无可恢复的未完成发送", 2200);
        return;
    }

    const QFileInfo info(state["filePath"].toString());
    const QString fileName = info.fileName().isEmpty() ? "未命名文件" : info.fileName();
    const QString receiverId = state["receiverId"].toString().trimmed();
    const QString targetName = receiverId.isEmpty() ? "公共聊天室" : QString("QQ:%1").arg(receiverId);
    const QJsonObject recoveryStatus = m_client->savedOutgoingTransferRecoveryStatus();
    if (!recoveryStatus.value("canAutoResume").toBool(false)) {
        const TransferResumeBlockedPrompt prompt = m_transferManager.resumeBlockedPrompt(state, recoveryStatus);
        appendSystemMessage(prompt.systemMessage);
        ui->chatHintLabel->setText(prompt.hintText);
        setTransferWorkspaceState(m_transferManager.resumeBlockedWorkspaceState(prompt));
        ui->statusbar->showMessage(prompt.statusMessage, 3600);
        const QMessageBox::StandardButton choice = QMessageBox::information(
            this,
            prompt.title,
            prompt.message,
            QMessageBox::Ok | QMessageBox::Discard,
            QMessageBox::Ok);
        if (choice == QMessageBox::Discard && m_client->clearOutgoingTransferState()) {
            appendSystemMessage(prompt.clearedSystemMessage);
            ui->chatHintLabel->setText(prompt.clearedHintText);
            setTransferWorkspaceState(m_transferManager.clearedRecoveryWorkspaceState(prompt.fileName));
            ui->statusbar->showMessage(prompt.clearedStatusMessage, 2200);
        }
        updateSavedOutgoingTransferRecoveryUi(false);
        return;
    }

    QProgressDialog progress(this);
    progress.setWindowTitle("恢复未完成发送");
    progress.setLabelText(m_transferManager.resumeInitialState(fileName, targetName).labelText);
    progress.setCancelButtonText("取消");
    progress.setRange(0, 100);
    progress.setValue(0);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);

    bool cancelRequested = false;
    QMetaObject::Connection cancelConnection = connect(
        &progress,
        &QProgressDialog::canceled,
        this,
        [this, &progress, &cancelRequested, &fileName]() {
            cancelRequested = true;
            progress.setLabelText(m_transferManager.resumeCancelState(fileName).labelText);
            if (m_client) m_client->cancelCurrentOutgoingTransfer();
            ui->statusbar->showMessage("正在取消恢复发送：" + fileName, 1600);
            QApplication::processEvents();
        });
    QMetaObject::Connection progressConnection = connect(
        m_client,
        &Client::fileTransferProgress,
        this,
        [this, &progress, &fileName, &targetName](const QString& currentFileName, qint64 bytesPrepared, qint64 totalBytes) {
            if (currentFileName != fileName) return;
            const TransferProgressUiState state = m_transferManager.resumeProgressState(fileName, targetName, bytesPrepared, totalBytes);
            progress.setValue(state.percent);
            progress.setLabelText(state.labelText);
            setTransferWorkspaceState(m_transferManager.sendingProgressWorkspaceState(QStringLiteral("文件"),
                                                                                      fileName,
                                                                                      targetName,
                                                                                      bytesPrepared,
                                                                                      totalBytes,
                                                                                      true));
            QApplication::processEvents();
        });
    QMetaObject::Connection preparedConnection = connect(
        m_client,
        &Client::fileTransferPrepared,
        this,
        [this, &progress, &fileName, &targetName](const QString& currentFileName,
                                             qint64 totalBytes,
                                             qint64 chunkSize,
                                             qint64 chunkCount,
                                             const QString& fileHash) {
            if (currentFileName != fileName) return;
            progress.setLabelText(m_transferManager.resumePreparedState(fileName, targetName, totalBytes, chunkSize, chunkCount, fileHash).labelText);
            setTransferWorkspaceState(m_transferManager.sendingPreparedWorkspaceState(QStringLiteral("文件"),
                                                                                      fileName,
                                                                                      targetName,
                                                                                      totalBytes,
                                                                                      chunkSize,
                                                                                      chunkCount,
                                                                                      fileHash,
                                                                                      true));
            QApplication::processEvents();
        });

    ui->statusbar->showMessage("正在恢复未完成发送：" + fileName, 1800);
    setTransferWorkspaceState(m_transferManager.recoveryWorkspaceState(
        m_transferManager.recoveryUiState(true, true, state, recoveryStatus, false)));
    QString rejectReason;
    const bool resumed = m_client->resumeSavedOutgoingTransfer(&rejectReason, 5000);

    QObject::disconnect(progressConnection);
    QObject::disconnect(preparedConnection);
    QObject::disconnect(cancelConnection);
    progress.setValue(resumed ? 100 : progress.value());
    QApplication::processEvents();
    progress.close();

    const TransferResumeResultState resultState = m_transferManager.resumeResultState(
        fileName,
        targetName,
        resumed,
        cancelRequested,
        rejectReason);
    if (resultState.succeeded) {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        setTransferWorkspaceState(m_transferManager.resumeResultWorkspaceState(resultState));
        ui->statusbar->showMessage(resultState.statusMessage, 2600);
    } else if (resultState.canceled) {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        setTransferWorkspaceState(m_transferManager.resumeResultWorkspaceState(resultState));
        ui->statusbar->showMessage(resultState.statusMessage, 2200);
    } else {
        appendSystemMessage(resultState.systemMessage);
        ui->chatHintLabel->setText(resultState.hintText);
        setTransferWorkspaceState(m_transferManager.resumeResultWorkspaceState(resultState));
        ui->statusbar->showMessage(resultState.statusMessage, 3200);
        const QMessageBox::StandardButton choice = QMessageBox::warning(
            this,
            resultState.failureTitle,
            resultState.failureMessage,
            QMessageBox::Ok | QMessageBox::Discard,
            QMessageBox::Ok);
        if (choice == QMessageBox::Discard && m_client->clearOutgoingTransferState()) {
            appendSystemMessage(resultState.clearedSystemMessage);
            ui->chatHintLabel->setText(resultState.clearedHintText);
            setTransferWorkspaceState(m_transferManager.clearedRecoveryWorkspaceState(resultState.fileName));
            ui->statusbar->showMessage(resultState.clearedStatusMessage, 2200);
        }
    }

    if (!resultState.succeeded && !resultState.canceled) {
        updateSavedOutgoingTransferRecoveryUi(false);
    }
}

void MainWindow::onSendFile() {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    if (!ensureTransferTargetReady(QStringLiteral("文件"), targetName, isLocalGroup)) {
        return;
    }

    const TransferSelectionPlan selectionPlan = m_transferManager.fileSelectionPlan();
    SelectedTransferFile selectedFile;
    if (!selectTransferFileContext(selectionPlan, &selectedFile)) {
        return;
    }

    sendSelectedTransfer(selectedFile, false);
}

void MainWindow::sendSelectedTransfer(const SelectedTransferFile& selectedFile, bool media) {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    const TransferSelectionPlan selectionPlan = media
        ? m_transferManager.mediaSelectionPlan()
        : m_transferManager.fileSelectionPlan();
    const QString kind = media
        ? m_transferManager.mediaSelection(selectedFile.info).mediaType
        : selectionPlan.preparingKind;
    const bool isVideo = media && m_transferManager.mediaSelection(selectedFile.info).isVideo;

    const TransferSendUiState preparingState =
        m_transferManager.preparingSendState(kind,
                                             selectedFile.info.fileName(),
                                             selectedFile.fileSize,
                                             targetName);
    applyTransferSendState(preparingState);
    const QString completedAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    if (isLocalGroup) {
        if (media) {
            appendLocalGroupMediaTransferCompletion(selectedFile.filePath,
                                                    selectedFile.info,
                                                    selectedFile.fileSize,
                                                    kind,
                                                    isVideo,
                                                    targetName,
                                                    completedAt);
        } else {
            appendLocalGroupFileTransferCompletion(selectionPlan,
                                                   selectedFile.info,
                                                   selectedFile.fileSize,
                                                   targetName,
                                                   completedAt);
        }
        return;
    }

    // Images should behave like chat messages: send immediately and render the
    // local bubble without opening the file-transfer progress dialog. Videos
    // and regular files keep the resumable progress workflow below.
    if (media && !isVideo) {
        const int optimisticRow = m_chatModel ? m_chatModel->rowCount() : -1;
        const QString optimisticTime = QDateTime::currentDateTime().toString("hh:mm:ss");
        const TransferSendUiState optimisticState = m_transferManager.remoteSendCompletedState(
            kind,
            selectedFile.info.fileName(),
            selectedFile.fileSize,
            targetName,
            optimisticTime,
            QString());
        const TransferWorkspaceCardState optimisticWorkspace = m_transferManager.remoteSendCompletedWorkspaceState(
            kind,
            selectedFile.info.fileName(),
            selectedFile.fileSize,
            targetName,
            optimisticTime,
            QString());
        appendRemoteMediaTransferCompletion(selectedFile.filePath,
                                            optimisticState,
                                            false,
                                            optimisticWorkspace);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("direct image send start row=%1 file=%2")
                .arg(QString::number(optimisticRow), selectedFile.info.fileName()));

        const bool ok = m_client && m_client->sendImage(selectedFile.filePath, m_privateChatTarget);
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("direct image send finished ok=%1 file=%2")
                .arg(ok ? QStringLiteral("true") : QStringLiteral("false"), selectedFile.info.fileName()));
        updateSavedOutgoingTransferRecoveryUi(!ok);
        if (!ok) {
            if (m_chatModel && optimisticRow >= 0 && optimisticRow < m_chatModel->rowCount()) {
                m_chatModel->removeRow(optimisticRow);
            }
            handleRemoteTransferResult(false,
                                       false,
                                       selectedFile.filePath,
                                       selectedFile.info,
                                       selectedFile.fileSize,
                                       targetName,
                                       kind,
                                       true,
                                       false,
                                       QString());
        }
        return;
    }

    QString transferSummary;
    bool transferCanceled = false;
    bool ok = sendTransferWithProgress(selectedFile.filePath,
                                       m_privateChatTarget,
                                       targetName,
                                       kind,
                                       media && !isVideo,
                                       &transferSummary,
                                       &transferCanceled);
    updateSavedOutgoingTransferRecoveryUi(!ok && !transferCanceled);
    handleRemoteTransferResult(ok,
                               transferCanceled,
                               selectedFile.filePath,
                               selectedFile.info,
                               selectedFile.fileSize,
                               targetName,
                                kind,
                                media,
                                isVideo,
                                transferSummary);
}

void MainWindow::onSendImage() {
    const QString targetName = m_privateChatTarget.isEmpty() ? "公共聊天室" : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = !m_privateChatTarget.isEmpty() && m_privateChatTarget.startsWith("local_group_");
    if (!ensureTransferTargetReady(QStringLiteral("图片/视频"), targetName, isLocalGroup)) {
        return;
    }

    const TransferSelectionPlan selectionPlan = m_transferManager.mediaSelectionPlan();
    SelectedTransferFile selectedFile;
    if (!selectTransferFileContext(selectionPlan, &selectedFile)) {
        return;
    }
    sendSelectedTransfer(selectedFile, true);
}

void MainWindow::onComposerFilesDropped(const QStringList& paths) {
    if (paths.isEmpty()) return;

    const QString path = paths.first();
    const QFileInfo info(path);
    if (!info.isFile()) return;

    const TransferMediaSelection mediaSelection = m_transferManager.mediaSelection(info);
    const bool media = mediaSelection.mediaType == QStringLiteral("图片")
        || mediaSelection.mediaType == QStringLiteral("视频");
    const QString targetName = m_privateChatTarget.isEmpty() ? QStringLiteral("公共聊天室") : contactDisplayName(m_privateChatTarget);
    const bool isLocalGroup = m_privateChatTarget.startsWith(QStringLiteral("local_group_"));
    if (!ensureTransferTargetReady(media ? mediaSelection.mediaType : QStringLiteral("文件"), targetName, isLocalGroup)) {
        return;
    }

    SelectedTransferFile selected;
    selected.filePath = path;
    selected.info = info;
    selected.fileSize = LocalFileManager::humanFileSize(info.size());
    sendSelectedTransfer(selected, media);
}

void MainWindow::setTransferWorkspaceState(const TransferWorkspaceCardState& state) {
    ui->transferOverviewStageLabel->setText(state.stageText);
    ui->transferOverviewSummaryLabel->setText(state.summaryText);
    ui->transferOverviewDetailLabel->setText(state.detailText);
    ui->transferOverviewActionLabel->setText(state.actionText);
}

bool MainWindow::ensureTransferTargetReady(const QString& kind, const QString& targetName, bool isLocalGroup) {
    if (m_privateChatTarget.isEmpty() && isCurrentUserRemovedFromPublicGroup()) {
        const TransferSendUiState state = m_transferManager.publicGroupRemovedState(kind);
        applyTransferSendState(state);
        refreshComposerState();
        return false;
    }
    if (!isLocalGroup && (!m_client || !m_client->isConnected())) {
        const TransferSendUiState state = m_transferManager.disconnectedSendState(kind, targetName);
        applyTransferSendState(state);
        refreshComposerState();
        return false;
    }
    return true;
}

bool MainWindow::selectTransferFile(const TransferSelectionPlan& selectionPlan,
                                    QString* filePath,
                                    QFileInfo* fileInfo,
                                    QString* fileSize) {
    if (!filePath || !fileInfo) {
        return false;
    }

    const QString selectedPath = QFileDialog::getOpenFileName(this,
                                                              selectionPlan.dialogTitle,
                                                              LocalFileManager::lastTransferDirectory(),
                                                              selectionPlan.filters);
    TransferSelectionUiState selectionState =
        m_transferManager.transferSelectionUiState(selectionPlan, selectedPath);
    if (!handleTransferSelectionUiState(&selectionState)) {
        return false;
    }

    *filePath = selectionState.filePath;
    *fileInfo = selectionState.fileInfo;
    if (fileSize) {
        *fileSize = selectionState.fileSize;
    }
    return true;
}

bool MainWindow::selectTransferFileContext(const TransferSelectionPlan& selectionPlan,
                                           SelectedTransferFile* selectedFile) {
    if (!selectedFile) {
        return false;
    }

    return selectTransferFile(selectionPlan,
                              &selectedFile->filePath,
                              &selectedFile->info,
                              &selectedFile->fileSize);
}

bool MainWindow::handleTransferSelectionUiState(TransferSelectionUiState* selectionState) {
    if (!selectionState) {
        return false;
    }

    while (true) {
        const TransferSelectionFeedbackPlan feedbackPlan =
            m_transferManager.transferSelectionFeedbackPlan(*selectionState);

        if (feedbackPlan.dialogKind == TransferSelectionFeedbackPlan::DialogKind::Warning) {
            QMessageBox::warning(this, feedbackPlan.dialogTitle, feedbackPlan.dialogMessage);
        }

        if (!feedbackPlan.hintText.isEmpty()) {
            ui->chatHintLabel->setText(feedbackPlan.hintText);
        }
        if (!feedbackPlan.statusMessage.isEmpty()) {
            ui->statusbar->showMessage(feedbackPlan.statusMessage, feedbackPlan.statusTimeoutMs);
        }

        if (feedbackPlan.requiresConfirmation) {
            const bool confirmed = QMessageBox::question(this,
                                                         feedbackPlan.dialogTitle,
                                                         feedbackPlan.dialogMessage,
                                                         QMessageBox::Yes | QMessageBox::No,
                                                         QMessageBox::No) == QMessageBox::Yes;
            *selectionState = m_transferManager.resolveTransferSelectionUiState(*selectionState, confirmed);
            continue;
        }

        return !feedbackPlan.stopSelection && selectionState->accepted;
    }
}

void MainWindow::applyTransferSendState(const TransferSendUiState& state) {
    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
}

void MainWindow::appendTransferCompletionState(const TransferSendUiState& state,
                                               bool includeSystemMessage,
                                               bool includeCard,
                                               const QColor& cardForeground,
                                               const QColor& cardBackground) {
    if (includeSystemMessage && !state.systemMessage.isEmpty()) {
        appendSystemMessage(state.systemMessage);
    }

    if (includeCard && !state.cardText.isEmpty()) {
        QStandardItem* cardItem = new QStandardItem(state.cardText);
        cardItem->setEditable(false);
        cardItem->setForeground(cardForeground);
        cardItem->setBackground(cardBackground);
        cardItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(cardItem);
    }

    if (!state.receiptText.isEmpty()) {
        QStandardItem* receiptItem = new QStandardItem(state.receiptText);
        receiptItem->setEditable(false);
        receiptItem->setForeground(QColor(86, 116, 130));
        receiptItem->setBackground(QColor(246, 251, 253));
        receiptItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_chatModel->appendRow(receiptItem);
    }

    ui->chatHintLabel->setText(state.hintText);
    ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
    scrollActiveChatToBottom();
}

void MainWindow::appendLocalGroupFileTransferCompletion(const TransferSelectionPlan& selectionPlan,
                                                        const QFileInfo& info,
                                                        const QString& fileSize,
                                                        const QString& targetName,
                                                        const QString& completedAt) {
    const TransferSendUiState completedState = m_transferManager.localSendCompletedState(
        selectionPlan.preparingKind,
        info.fileName(),
        fileSize,
        targetName,
        completedAt);
    const QString line = QString("[%1] <%2> 发送了文件: %3 · %4")
        .arg(completedAt, m_currentUserName, info.fileName(), fileSize);
    saveHistory(m_privateChatTarget, line);
    QStandardItem* item = new QStandardItem(QStringLiteral("发送了文件: %1 · %2").arg(info.fileName(), fileSize));
    item->setEditable(false);
    item->setData(bubbleTimestamp(completedAt), ChatBubbleTimestampRole);
    decorateChatItem(item, m_currentUserId, m_currentUserName, true);
    m_chatModel->appendRow(item);
    setTransferWorkspaceState(m_transferManager.localSendCompletedWorkspaceState(selectionPlan.preparingKind,
                                                                                 info.fileName(),
                                                                                 fileSize,
                                                                                 targetName,
                                                                                 completedAt));
    appendTransferCompletionState(completedState, true, true, QColor(0, 121, 107), QColor(232, 248, 245));
}

void MainWindow::appendLocalGroupMediaTransferCompletion(const QString& filePath,
                                                         const QFileInfo& info,
                                                         const QString& fileSize,
                                                         const QString& mediaType,
                                                         bool isVideo,
                                                         const QString& targetName,
                                                         const QString& completedAt) {
    const TransferSendUiState completedState = m_transferManager.localSendCompletedState(
        mediaType,
        info.fileName(),
        fileSize,
        targetName,
        completedAt);
    const QString line = QString("[%1] <%2> [%3] %4 · %5")
        .arg(completedAt, m_currentUserName, mediaType, info.fileName(), fileSize);
    saveHistory(m_privateChatTarget, line);

    QPixmap pixmap;
    if (!isVideo) {
        pixmap.load(filePath);
    }
    if ((!isVideo && !pixmap.isNull()) || isVideo) {
        const TransferMediaPreviewPlan previewPlan = m_transferManager.localMediaPreviewPlan(
            info.fileName(),
            fileSize,
            isVideo);
        Q_UNUSED(previewPlan)
        appendMediaPreviewItem(completedState.cardText,
                               pixmap,
                               isVideo,
                               true,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
    }
    // A media bubble is the chat record; do not append a second generic
    // completion card underneath it.
    setTransferWorkspaceState(m_transferManager.localSendCompletedWorkspaceState(mediaType,
                                                                                 info.fileName(),
                                                                                 fileSize,
                                                                                 targetName,
                                                                                 completedAt));
    ui->chatHintLabel->setText(completedState.hintText);
    ui->statusbar->showMessage(completedState.statusMessage, completedState.statusTimeoutMs);
    scrollActiveChatToBottom();
}

void MainWindow::appendRemoteMediaTransferCompletion(const QString& filePath,
                                                     const TransferSendUiState& completedState,
                                                     bool isVideo,
                                                     const TransferWorkspaceCardState& workspaceState) {
    setTransferWorkspaceState(workspaceState);
    const TransferMediaPreviewPlan previewPlan = m_transferManager.remoteMediaPreviewPlan(completedState.cardText, isVideo);
    if (!isVideo) {
        const QPixmap pixmap = loadChatImagePreview(filePath);
        qtnetworkchat::logDebug("MainWindow", QStringLiteral("image preview load ok=%1 path=%2")
                .arg(pixmap.isNull() ? QStringLiteral("false") : QStringLiteral("true"), filePath));
        appendMediaPreviewItem(QString(),
                               pixmap,
                               previewPlan.isVideo,
                               previewPlan.alignRight,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
    } else {
        appendMediaPreviewItem(previewPlan.text,
                               QPixmap(),
                               previewPlan.isVideo,
                               previewPlan.alignRight,
                               filePath,
                               m_currentUserId,
                               m_currentUserName);
    }
    ui->chatHintLabel->setText(completedState.hintText);
    ui->statusbar->showMessage(completedState.statusMessage, completedState.statusTimeoutMs);
    scrollActiveChatToBottom();
}

void MainWindow::handleRemoteTransferResult(bool ok,
                                            bool transferCanceled,
                                            const QString& filePath,
                                            const QFileInfo& info,
                                            const QString& fileSize,
                                            const QString& targetName,
                                            const QString& kind,
                                            bool media,
                                            bool isVideo,
                                            const QString& transferSummary) {
    if (ok) {
        const TransferSendUiState completedState = m_transferManager.remoteSendCompletedState(
            kind,
            info.fileName(),
            fileSize,
            targetName,
            QDateTime::currentDateTime().toString("hh:mm:ss"),
            transferSummary);
        const TransferWorkspaceCardState workspaceState = m_transferManager.remoteSendCompletedWorkspaceState(
            kind,
            info.fileName(),
            fileSize,
            targetName,
            QDateTime::currentDateTime().toString("hh:mm:ss"),
            transferSummary);
        if (media) {
            appendRemoteMediaTransferCompletion(filePath, completedState, isVideo, workspaceState);
        } else {
            setTransferWorkspaceState(workspaceState);
    appendTransferCompletionState(completedState, false, false, QColor(0, 121, 107), QColor(232, 248, 245));
        }
        return;
    }

    if (transferCanceled) {
        appendSystemMessage(QString("已取消发送%1: %2 · 到 %3").arg(kind, info.fileName(), targetName));
        const TransferSendUiState state = m_transferManager.canceledSendState(kind, info.fileName());
        setTransferWorkspaceState(m_transferManager.canceledSendWorkspaceState(kind, info.fileName(), targetName));
        ui->chatHintLabel->setText(QString("%1 · %2").arg(state.hintText, targetName));
        ui->statusbar->showMessage(state.statusMessage, state.statusTimeoutMs);
        refreshComposerState();
        return;
    }

    const TransferSendUiState state = m_transferManager.failedSendState(kind, info.fileName(), fileSize, targetName);
    setTransferWorkspaceState(m_transferManager.failedSendWorkspaceState(kind, info.fileName(), fileSize, targetName));
    applyTransferSendState(state);
    QMessageBox::warning(this, state.warningTitle, state.warningMessage);
    refreshComposerState();
}

void MainWindow::appendMediaPreviewItem(const QString& text,
                                        const QPixmap& pixmap,
                                        bool isVideo,
                                        bool alignRight,
                                        const QString& openPath,
                                        const QString& senderId,
                                        const QString& senderName) {
    QStandardItem* previewItem = new QStandardItem(text);
    if (!isVideo) {
        previewItem->setData(QStringLiteral("image"), ChatBubbleMediaKindRole);
        if (!pixmap.isNull()) {
            const QPixmap preview = pixmap.scaled(360, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            previewItem->setData(preview, ChatBubbleMediaPreviewRole);
        } else {
            previewItem->setData(true, ChatBubbleMediaPreviewUnavailableRole);
        }
    } else if (isVideo) {
        previewItem->setData(QStringLiteral("video"), ChatBubbleMediaKindRole);
    }
    if (!openPath.trimmed().isEmpty()) {
        previewItem->setData(openPath.trimmed(), ChatBubbleMediaOpenPathRole);
        previewItem->setData(QStringLiteral("双击打开文件；右键可复制保存路径或打开目录\n%1").arg(openPath.trimmed()), Qt::ToolTipRole);
    }
    previewItem->setEditable(false);
    previewItem->setBackground(isVideo ? QColor(245, 240, 255) : QColor(246, 250, 253));
    if (isVideo) {
        previewItem->setForeground(QColor(126, 87, 194));
    }
    if (alignRight) {
        previewItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    decorateChatItem(previewItem,
                     senderId.isEmpty() ? m_currentUserId : senderId,
                     senderName.isEmpty() ? m_currentUserName : senderName,
                     alignRight);
    m_chatModel->appendRow(previewItem);
    if (m_messagesView) {
        m_messagesView->setEmptyStateVisible(false);
    }
    qtnetworkchat::logDebug("MainWindow", QStringLiteral("media bubble appended kind=%1 row=%2 path=%3")
            .arg(isVideo ? QStringLiteral("video") : QStringLiteral("image"),
                 QString::number(m_chatModel->rowCount() - 1),
                 openPath));
}

void MainWindow::applyReceivedTransferRenderPlan(const TransferReceiveRenderPlan& plan,
                                                 const QString& fileName,
                                                 const QString& transferId,
                                                 qint64 receivedBytes,
                                                 qint64 totalBytes) {
    for (const TransferChatListItemUiState& itemState : plan.chatItems) {
        appendTransferChatListItem(itemState);
    }
    showFileTransferStatusEvent(fileName,
                                transferId,
                                plan.eventReason,
                                receivedBytes,
                                totalBytes);
    ui->chatHintLabel->setText(plan.hintText);
    ui->statusbar->showMessage(plan.statusMessage, plan.statusTimeoutMs);
}

bool MainWindow::persistReceivedTransferPayload(const ReceivedTransferContext& context,
                                                const QString& displayName,
                                                const QString& transferId,
                                                const QByteArray& fileData,
                                                qint64 totalBytes) {
    const bool saved = LocalFileManager::writeReceivedTransferPayload(context.savePath, fileData);
    setTransferWorkspaceState(m_transferManager.receivedTransferWorkspaceState(context.kind,
                                                                               context.receivedName,
                                                                               context.receivedSize,
                                                                               displayName,
                                                                               context.manifestSuffix,
                                                                               context.integrityText,
                                                                               context.integritySuffix,
                                                                               context.savePath,
                                                                               saved));
    const TransferReceiveRenderPlan plan = receivedTransferPersistencePlan(context, displayName, saved);
    if (context.kind == QStringLiteral("图片") && saved) {
        ui->chatHintLabel->setText(plan.hintText);
        ui->statusbar->showMessage(plan.statusMessage, plan.statusTimeoutMs);
    } else {
        applyReceivedTransferRenderPlan(plan,
                                        context.receivedName,
                                        transferId,
                                        fileData.size(),
                                        totalBytes);
    }
    return saved;
}

bool MainWindow::handleReceivedTransferMessage(const Message& msg,
                                               const QString& displayName) {
    if (msg.fileData.isEmpty()) {
        return false;
    }

    const bool image = msg.type == MessageType::Image;
    const ReceivedTransferContext context = receivedTransferContext(
        msg,
        image ? QStringLiteral("图片") : QStringLiteral("文件"),
        image ? QStringLiteral("received_image") : QStringLiteral("received_file"),
        image ? QStringLiteral("Images") : QStringLiteral("Files"),
        displayName);
    if (image) {
        QPixmap pixmap;
        if (pixmap.loadFromData(msg.fileData)) {
            const TransferMediaPreviewPlan previewPlan = m_transferManager.receivedMediaPreviewPlan(
                context.receivedName,
                context.receivedSize,
                context.manifestSuffix);
            appendMediaPreviewItem(previewPlan.text,
                                   pixmap,
                                   previewPlan.isVideo,
                                   previewPlan.alignRight,
                                   context.savePath,
                                   msg.senderId,
                                   displayName);
        }
    }
    return persistReceivedTransferPayload(context,
                                          displayName,
                                          msg.transferId,
                                          msg.fileData,
                                          msg.fileSize > 0 ? msg.fileSize : msg.fileData.size());
}

TransferReceiveRenderPlan MainWindow::receivedTransferPersistencePlan(const ReceivedTransferContext& context,
                                                                      const QString& displayName,
                                                                      bool saved) const {
    return m_transferManager.receivedTransferPersistenceRenderPlan(context.kind,
                                                                  context.receivedName,
                                                                  context.receivedSize,
                                                                  displayName,
                                                                  context.manifestSuffix,
                                                                  context.integrityText,
                                                                  context.integritySuffix,
                                                                  context.savePath,
                                                                  saved);
}

void MainWindow::appendTransferChatListItem(const TransferChatListItemUiState& itemState) {
    if (itemState.text.isEmpty()) {
        return;
    }

    QStandardItem* item = TransferChatItemRenderer::createItem(itemState);
    m_chatModel->appendRow(item);
}

MainWindow::ReceivedTransferContext MainWindow::receivedTransferContext(const Message& msg,
                                                                        const QString& kind,
                                                                        const QString& fallbackName,
                                                                        const QString& downloadSubdir,
                                                                        const QString& displayName) const {
    ReceivedTransferContext context;
    context.kind = kind;
    const LocalReceivedTransferPlan localPlan = LocalFileManager::receivedTransferPlan(msg.fileName,
                                                                                       fallbackName,
                                                                                       msg.fileData.size(),
                                                                                       downloadSubdir);
    context.receivedName = localPlan.receivedName;
    context.receivedSize = localPlan.receivedSize;
    context.savePath = localPlan.savePath;
    context.integrityText = transferIntegritySummary(msg);
    context.integritySuffix = context.integrityText.isEmpty()
        ? QString()
        : QString(" · %1").arg(context.integrityText);
    const QString manifestText = m_transferManager.sendingPreparedState(kind,
                                                                        context.receivedName,
                                                                        displayName,
                                                                        msg.fileSize > 0 ? msg.fileSize : msg.fileData.size(),
                                                                        msg.chunkSize,
                                                                        msg.chunkCount,
                                                                        msg.fileHash).manifestSummary;
    context.manifestSuffix = manifestText.isEmpty() ? QString() : QString(" · %1").arg(manifestText);
    return context;
}
