#include "MainWindow.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
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
        QPushButton#trackButton { background: #245e51; border-color: #398975; }
        QPushButton#trackButton:hover { background: #307765; }
        QPushButton#trackButton:disabled { background: #182c28; color: #6c7787; }
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
    panelLayout->setSpacing(18);
    layout->addWidget(panel);

    auto *statusGroup = new QGroupBox("SYSTEM STATUS");
    auto *statusLayout = new QVBoxLayout(statusGroup);
    statusLayout->setSpacing(18);
    addValueRow(statusLayout, "Orange Pi", "Control unavailable");
    addValueRow(statusLayout, "Gimbal", "Unknown");
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
    trackButton = new QPushButton("Track");
    trackButton->setObjectName("trackButton");
    auto *clearButton = new QPushButton("Clear");
    clearButton->setObjectName("clearButton");
    buttons->addWidget(trackButton);
    buttons->addWidget(clearButton);
    targetLayout->addLayout(buttons);
    panelLayout->addWidget(targetGroup);

    panelLayout->addStretch();
    auto *diagnosticGroup = new QGroupBox("DIAGNOSTICS");
    auto *diagnosticLayout = new QVBoxLayout(diagnosticGroup);
    diagnosticLayout->setSpacing(14);
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
        trackButton->setEnabled(enteredTrackId() != 0);
    });
    connect(trackButton, &QPushButton::clicked, this, [this] {
        requestTargetSet(enteredTrackId());
    });
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::requestTargetClear);
    connect(videoWidget, &VideoWidget::targetSetRequested, this, &MainWindow::requestTargetSet);
    connect(videoWidget, &VideoWidget::targetClearRequested, this, &MainWindow::requestTargetClear);
    connect(videoWidget, &VideoWidget::framePainted, this, &MainWindow::framePainted);

    trackButton->setEnabled(enteredTrackId() != 0);
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
    trackButton->setEnabled(enteredTrackId() != 0);
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

void MainWindow::requestTargetSet(quint32 trackId)
{
    if (trackId == 0)
        return;
    emit targetSetRequested(trackId);
}

void MainWindow::requestTargetClear()
{
    emit targetClearRequested();
}

void MainWindow::resetReception()
{
    setDisplayFrame({}, {});
    setTrackCounts(0, 0, false);
    videoFpsLabel->setText("--");
    metadataFpsLabel->setText("--");
    videoProcessingLabel->setText("--");
}
