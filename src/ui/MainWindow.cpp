#include "MainWindow.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>
#include <limits>

namespace {
QLabel *addValueRow(QVBoxLayout *layout, const QString &name, const QString &value)
{
    auto *row = new QHBoxLayout;
    auto *label = new QLabel(name);
    auto *valueLabel = new QLabel(value);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setObjectName("value");
    row->addWidget(label);
    row->addStretch();
    row->addWidget(valueLabel);
    layout->addLayout(row);
    return valueLabel;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("LWIR Target Tracking System");
    resize(1280, 720);
    setMinimumSize(800, 560);
    setStyleSheet(R"(
        QMainWindow, QWidget#central { background: #111720; color: #dbe3ee; }
        QLabel { color: #b6c1d0; }
        QLabel#value { color: #edf2f7; }
        QGroupBox { color: #edf2f7; font-weight: 600; border: 1px solid #303b4b;
                    border-radius: 6px; margin-top: 12px; padding: 20px 14px 14px; }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 5px; }
        QComboBox { background: #1c2532; color: #edf2f7; border: 1px solid #465469;
                    border-radius: 4px; padding: 7px 10px; min-height: 22px; }
        QComboBox QAbstractItemView { background: #1c2532; color: #edf2f7;
                                    selection-background-color: #35566a; }
        QPushButton { background: #263447; color: #edf2f7; border: 1px solid #465469;
                      border-radius: 4px; padding: 9px; }
        QPushButton:hover { background: #354961; }
        QPushButton:pressed { background: #182638; }
        QPushButton:disabled { color: #6c7787; background: #18202c; }
        QPushButton#autoButton { background: #245e51; border-color: #398975; }
        QPushButton#autoButton:hover { background: #307765; }
        QPushButton#autoButton:disabled { background: #182c28; color: #6c7787; }
    )");
    auto *central = new QWidget;
    central->setObjectName("central");
    setCentralWidget(central);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(20);
    videoWidget = new VideoWidget;
    layout->addWidget(videoWidget, 1);

    auto *panel = new QWidget;
    panel->setFixedWidth(280);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(10);
    layout->addWidget(panel);

    auto *statusGroup = new QGroupBox("SYSTEM STATUS");
    auto *statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->setSpacing(8);
    connectionLabel = addValueRow(statusLayout, "Control", "Disconnected");
    connectionLabel->setObjectName("controlConnection");
    modeLabel = addValueRow(statusLayout, "Mode", "--");
    modeLabel->setObjectName("controlMode");
    targetLabel = addValueRow(statusLayout, "Target", "--");
    gimbalLabel = addValueRow(statusLayout, "Gimbal", "--");
    gimbalLabel->setObjectName("gimbalState");
    errorLabel = addValueRow(statusLayout, "Error", "--");
    panelLayout->addWidget(statusGroup);

    auto *targetGroup = new QGroupBox("TARGET CONTROL");
    auto *targetLayout = new QVBoxLayout(targetGroup);
    targetLayout->setSpacing(12);
    targetLayout->addWidget(new QLabel("Track ID"));
    trackCombo = new QComboBox;
    trackCombo->setObjectName("trackCombo");
    trackCombo->setEditable(true);
    trackCombo->setInsertPolicy(QComboBox::NoInsert);
    trackCombo->lineEdit()->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9]{0,10}"), trackCombo));
    trackCombo->lineEdit()->setPlaceholderText("Select or enter Track ID");
    trackCombo->setToolTip("Track ID: 1 - 4294967295");
    targetLayout->addWidget(trackCombo);
    auto *buttons = new QHBoxLayout;
    autoButton = new QPushButton("AUTO");
    autoButton->setObjectName("autoButton");
    manualButton = new QPushButton("MANUAL");
    manualButton->setObjectName("manualButton");
    buttons->addWidget(autoButton);
    buttons->addWidget(manualButton);
    targetLayout->addLayout(buttons);
    auto *hint = new QLabel("Click video: arrow keys move, Space returns home");
    hint->setWordWrap(true);
    targetLayout->addWidget(hint);
    panelLayout->addWidget(targetGroup);

    panelLayout->addStretch();
    auto *diagnosticGroup = new QGroupBox("DIAGNOSTICS");
    auto *diagnosticLayout = new QVBoxLayout(diagnosticGroup);
    diagnosticLayout->setSpacing(8);
    videoCodecLabel = addValueRow(diagnosticLayout, "Video codec", "--");
    videoCodecLabel->setObjectName("videoCodec");
    videoFpsLabel = addValueRow(diagnosticLayout, "Video FPS", "--");
    videoFpsLabel->setObjectName("videoFps");
    videoProcessingLabel = addValueRow(diagnosticLayout, "Decode + delivery", "--");
    videoProcessingLabel->setObjectName("videoProcessingMs");
    videoProcessingLabel->setToolTip("Average ms: complete RTP frame after jitter buffer to owned image; includes polling wait. Excludes network and paint.");
    metadataFpsLabel = addValueRow(diagnosticLayout, "Metadata FPS", "--");
    metadataFpsLabel->setObjectName("metadataFps");
    for (const auto &name : {"Metadata Loss", "Video Missing", "Sync Miss"})
        addValueRow(diagnosticLayout, name, "--")->setToolTip("Not implemented yet.");
    trackCountLabel = addValueRow(diagnosticLayout, "Track Count", "--");
    trackCountLabel->setObjectName("trackCount");
    setTrackCounts(0, 0, false);
    panelLayout->addWidget(diagnosticGroup);

    connect(trackCombo, &QComboBox::editTextChanged, this, [this] {
        updateControlActions();
    });
    connect(autoButton, &QPushButton::clicked, this, [this] {
        requestAuto(enteredTrackId());
    });
    connect(manualButton, &QPushButton::clicked, this, &MainWindow::requestManual);
    connect(videoWidget, &VideoWidget::autoRequested, this, &MainWindow::requestAuto);
    connect(videoWidget, &VideoWidget::manualRequested, this, &MainWindow::requestManual);
    connect(videoWidget, &VideoWidget::framePainted, this, &MainWindow::framePainted);

    updateControlActions();
}

void MainWindow::setVideoFps(double fps)
{
    videoFpsLabel->setText(QString::number(fps, 'f', 1));
}

void MainWindow::setVideoProcessingTime(double milliseconds)
{
    videoProcessingLabel->setText(QString::number(milliseconds, 'f', 2) + " ms");
}

void MainWindow::setVideoCodec(VideoCodec codec)
{
    videoCodecLabel->setText(codec == VideoCodec::JPEG ? "JPEG" : "H.264");
}

void MainWindow::setMetadataFps(double fps)
{
    metadataFpsLabel->setText(QString::number(fps, 'f', 1));
}

void MainWindow::setTrackCounts(quint32 wireCount, qsizetype displayedCount, bool metadataAvailable)
{
    trackCountLabel->setText(metadataAvailable ? QString::number(wireCount) : "--");
    trackCountLabel->setToolTip(metadataAvailable
        ? QString("Wire records: %1\nDisplayed boxes: %2").arg(wireCount).arg(displayedCount)
        : QString("No metadata for the current frame."));
}

void MainWindow::setClassLabels(const ClassLabels &labels)
{
    videoWidget->setClassLabels(labels);
}

void MainWindow::setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks, quint64 frameId)
{
    videoWidget->setDisplayFrame(image, tracks, frameId);
    QSet<quint32> nextIds;
    QStringList items;
    if (!image.isNull()) {
        for (const auto &track : tracks) {
            if (track.trackId && !nextIds.contains(track.trackId)) {
                nextIds.insert(track.trackId);
                items.push_back(QString::number(track.trackId));
            }
        }
    }
    if (nextIds == availableTrackIds) return;
    availableTrackIds = std::move(nextIds);
    const QString typedId = trackCombo->currentText();
    const QSignalBlocker blocker(trackCombo);
    trackCombo->clear();
    trackCombo->addItems(items);
    trackCombo->setCurrentIndex(-1);
    trackCombo->setEditText(typedId);
    updateControlActions();
}

quint32 MainWindow::enteredTrackId() const
{
    const QString text = trackCombo->currentText();
    if (!QRegularExpression("^[0-9]{1,10}$").match(text).hasMatch())
        return 0;
    bool valid = false;
    const qulonglong id = text.toULongLong(&valid);
    return valid && id > 0 && id <= std::numeric_limits<quint32>::max()
        ? static_cast<quint32>(id) : 0;
}

void MainWindow::requestAuto(quint32 trackId)
{
    if (!trackId || !controlReady || !stateAvailable || blocked()) return;
    emit autoRequested(trackId);
}

void MainWindow::requestManual()
{
    if (controlReady && stateAvailable && !blocked()) emit manualRequested();
}

void MainWindow::resetReception()
{
    setDisplayFrame({}, {});
    setTrackCounts(0, 0, false);
    videoFpsLabel->setText("--");
    metadataFpsLabel->setText("--");
    videoProcessingLabel->setText("--");
}

QWidget *MainWindow::manualInputSurface() const { return videoWidget; }

bool MainWindow::blocked() const
{
    return board.gimbal == Control::GimbalState::Homing || board.gimbal == Control::GimbalState::Error;
}

void MainWindow::updateControlActions()
{
    const bool available = controlReady && stateAvailable && !blocked();
    autoButton->setEnabled(available && enteredTrackId() != 0);
    manualButton->setEnabled(available);
}

void MainWindow::setControlConnection(Control::Connection connection)
{
    controlReady = connection == Control::Connection::Ready;
    switch (connection) {
    case Control::Connection::Disconnected: connectionLabel->setText("Disconnected"); break;
    case Control::Connection::Connecting: connectionLabel->setText("Connecting"); break;
    case Control::Connection::AwaitingState: connectionLabel->setText("Waiting for state"); break;
    case Control::Connection::Ready: connectionLabel->setText("Connected"); break;
    }
    if (!controlReady) {
        stateAvailable = false;
        modeLabel->setText("--");
        targetLabel->setText("--");
        gimbalLabel->setText("--");
        errorLabel->setText("--");
    }
    updateControlActions();
}

void MainWindow::setBoardState(const Control::BoardState &state)
{
    board = state;
    stateAvailable = true;
    modeLabel->setText(state.mode == Control::Mode::Auto ? "AUTO" : "MANUAL");
    const char *target = state.target == Control::TargetState::Tracking ? "TRACKING" : "LOST";
    targetLabel->setText(state.selectedTargetId ? QString("%1 / %2").arg(state.selectedTargetId).arg(target) : "None");
    switch (state.gimbal) {
    case Control::GimbalState::Idle: gimbalLabel->setText("IDLE"); break;
    case Control::GimbalState::ManualMoving: gimbalLabel->setText("MANUAL MOVING"); break;
    case Control::GimbalState::AutoTracking: gimbalLabel->setText("AUTO TRACKING"); break;
    case Control::GimbalState::Homing: gimbalLabel->setText("HOMING"); break;
    case Control::GimbalState::Hold: gimbalLabel->setText("HOLD"); break;
    case Control::GimbalState::Error: gimbalLabel->setText("ERROR"); break;
    }
    errorLabel->setText(state.errorCode ? QString::number(state.errorCode) : "None");
    updateControlActions();
}

void MainWindow::showCommandResult(const Control::Ack &ack)
{
    if (ack.category == Control::Category::System) return;
    statusBar()->showMessage(Control::commandName(ack.category, ack.command) + ": " + Control::resultName(ack.result), 5000);
}

void MainWindow::showControlError(const QString &message) { statusBar()->showMessage(message, 5000); }
