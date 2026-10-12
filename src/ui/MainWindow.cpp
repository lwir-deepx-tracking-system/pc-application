#include "MainWindow.h"

#include <QComboBox>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
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

QImage makeDummyImage()
{
    QImage image(640, 480, QImage::Format_RGB32);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, 0, 480);
    gradient.setColorAt(0, QColor(28, 28, 28));
    gradient.setColorAt(1, QColor(87, 87, 87));
    painter.fillRect(image.rect(), gradient);
    painter.setPen(QColor(99, 99, 99));
    for (int y = 300; y < 480; y += 30)
        painter.drawLine(0, y, 640, y);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    for (const QPoint &position : {QPoint(120, 160), QPoint(315, 205), QPoint(510, 135)}) {
        painter.setBrush(QColor(194, 194, 194));
        painter.drawEllipse(QRectF(position.x() - 13, position.y(), 26, 26));
        painter.drawRoundedRect(QRectF(position.x() - 20, position.y() + 30, 40, 70), 12, 12);
        painter.setBrush(QColor(155, 155, 155));
        painter.drawRoundedRect(QRectF(position.x() - 18, position.y() + 96, 14, 45), 5, 5);
        painter.drawRoundedRect(QRectF(position.x() + 4, position.y() + 96, 14, 45), 5, 5);
    }
    painter.setPen(QColor(190, 190, 190));
    painter.drawText(QRect(16, 14, 300, 24), "DUMMY FRAME  /  640 x 480");
    return image;
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
    addValueRow(statusLayout, "Orange Pi", QString::fromUtf8("\xe2\x97\x8f Offline"));
    addValueRow(statusLayout, "Gimbal", QString::fromUtf8("\xe2\x97\x8f Unknown"));
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
    for (const auto &name : {"Video FPS", "Metadata FPS", "Metadata Loss", "Video Missing",
                             "Sync Miss", "Detection", "Track Count"})
        addValueRow(diagnosticLayout, name, "--");
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

    setDisplayFrame(makeDummyImage(), {{3, QRectF(82, 150, 76, 162)},
                                     {15, QRectF(277, 195, 76, 162)},
                                     {22, QRectF(472, 125, 76, 162)}});
    trackButton->setEnabled(enteredTrackId() != 0);
}

void MainWindow::setDisplayFrame(const QImage &image, const QVector<TrackBox> &tracks)
{
    videoWidget->setDisplayFrame(image, tracks);
    const QString typedId = trackCombo->currentText();
    const QSignalBlocker blocker(trackCombo);
    trackCombo->clear();
    if (!image.isNull()) {
        for (const auto &track : tracks) {
            const QString id = QString::number(track.trackId);
            if (track.trackId != 0 && trackCombo->findText(id) == -1)
                trackCombo->addItem(id);
        }
    }
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
    qInfo().noquote() << QString("TARGET_SET(%1) [GUI mock]").arg(trackId);
    emit targetSetRequested(trackId);
}

void MainWindow::requestTargetClear()
{
    qInfo() << "TARGET_CLEAR [GUI mock]";
    emit targetClearRequested();
}
