#include <QtTest>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTcpServer>
#include <stdexcept>
#include "app/ApplicationController.h"
#include "config/AppConfig.h"
#include "control/ManualInputHandler.h"
#include "ui/MainWindow.h"
#include "MockControlServer.h"

namespace {
const QByteArray manualStatus = QByteArray::fromHex("4c574952010300080000000000000000");
const QByteArray autoStatus = QByteArray::fromHex("4c574952010300080101020000000007");
const QByteArray autoAck = QByteArray::fromHex("4c57495201010003020100");
ControlSettings settingsFor(quint16 port)
{
    ControlSettings settings;
    settings.enabled = true;
    settings.port = port;
    settings.pingIntervalMs = 10000;
    settings.ackTimeoutMs = 500;
    settings.connectTimeoutMs = 1000;
    settings.reconnectIntervalMs = 80;
    return settings;
}
class RawBoard : public QObject {
public:
    RawBoard() {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            auto *socket = server.nextPendingConnection();
            peers.push_back(socket);
            if (initialState) socket->write(manualStatus);
        });
    }
    QTcpSocket *peer() const { return peers.isEmpty() ? nullptr : peers.last().data(); }
    QTcpServer server;
    QVector<QPointer<QTcpSocket>> peers;
    bool initialState = true;
};
}

class ControlTest : public QObject
{
    Q_OBJECT
private slots:
    void wireFixtures()
    {
        using namespace Control;
        QCOMPARE(encode({Category::Mode, quint8(ModeCommand::Auto), QByteArray::fromHex("ffffffff")}),
                 QByteArray::fromHex("4c57495202010004ffffffff"));
        QCOMPARE(encode({Category::Mode, quint8(ModeCommand::Manual), {}}), QByteArray::fromHex("4c57495202020000"));
        QCOMPARE(encode({Category::Gimbal, quint8(GimbalCommand::Move), QByteArray::fromHex("ff01")}),
                 QByteArray::fromHex("4c57495203010002ff01"));
        QCOMPARE(encode({Category::Gimbal, quint8(GimbalCommand::Home), {}}), QByteArray::fromHex("4c57495203030000"));
        QVERIFY(encode({Category::Gimbal, 2, {}}).isEmpty()); // Reserved STOP.
        QVERIFY(encode({Category::Mode, 1, {}}).isEmpty());
        BoardState state;
        QVERIFY(decodeState(autoStatus.mid(8), state));
        QCOMPARE(state.mode, Mode::Auto);
        QCOMPARE(state.selectedTargetId, quint32(7));
        QCOMPARE(encodeState(state), autoStatus.mid(8));
        auto bad = autoStatus.mid(8);
        bad[0] = char(2);
        QVERIFY(!decodeState(bad, state));
        QCOMPARE(state.selectedTargetId, quint32(7));
        bad = autoStatus.mid(8);
        bad.replace(4, 4, QByteArray(4, '\0'));
        QVERIFY(!decodeState(bad, state));
        Ack ack;
        QVERIFY(decodeAck(autoAck.mid(8), ack));
        QCOMPARE(ack.result, Result::Ok);
        QVERIFY(!decodeAck(QByteArray::fromHex("030100"), ack)); // MOVE has no ACK.
    }

    void splitAndCoalescedFrames()
    {
        using namespace Control;
        for (int split = 1; split < manualStatus.size(); ++split) {
            auto bytes = manualStatus.left(split);
            Message message;
            QString error;
            QCOMPARE(takeMessage(bytes, message, error), ParseResult::Incomplete);
            bytes += manualStatus.mid(split) + QByteArray::fromHex("4c57495202020000");
            QCOMPARE(takeMessage(bytes, message, error), ParseResult::Complete);
            QCOMPARE(message.payload, manualStatus.mid(8));
            QCOMPARE(takeMessage(bytes, message, error), ParseResult::Complete);
            QCOMPARE(message.category, Category::Mode);
            QCOMPARE(message.command, quint8(ModeCommand::Manual));
            QVERIFY(bytes.isEmpty());
        }
        for (const auto &hex : {"4c57494d01030008", "4c5749520103ffff", "4c57495203020000", "4c574952ff010000"}) {
            auto bytes = QByteArray::fromHex(hex);
            Message message;
            QString error;
            QCOMPARE(takeMessage(bytes, message, error), ParseResult::Invalid);
            QVERIFY(!error.isEmpty());
        }
    }

    void ackDoesNotChangeModeAndInitClearsPending()
    {
        RawBoard board;
        board.initialState = false;
        QVERIFY(board.server.listen(QHostAddress::LocalHost, 0));
        ControlClient client;
        QSignalSpy states(&client, &ControlClient::boardStateChanged);
        QSignalSpy acks(&client, &ControlClient::commandFinished);
        QSignalSpy resets(&client, &ControlClient::resetRequested);
        client.start(settingsFor(board.server.serverPort()));
        QTRY_COMPARE(board.peers.size(), 1);
        board.peer()->write(manualStatus.left(3));
        QTest::qWait(20);
        QVERIFY(!client.canControl());
        board.peer()->write(manualStatus.mid(3));
        QTRY_VERIFY(client.canControl());
        QCOMPARE(resets.count(), 1);
        QVERIFY(!client.requestAuto(0));
        QVERIFY(client.requestAuto(7));
        QVERIFY(!client.requestAuto(15)); // Same command pending: no queue or replacement.
        QTRY_VERIFY(board.peer()->bytesAvailable() >= 12);
        QCOMPARE(board.peer()->readAll(), QByteArray::fromHex("4c5749520201000400000007"));
        board.peer()->write(autoAck.left(9));
        QTest::qWait(15);
        QCOMPARE(acks.count(), 0);
        board.peer()->write(autoAck.mid(9));
        QTRY_COMPARE(acks.count(), 1);
        QCOMPARE(states.count(), 1);
        QCOMPARE(client.boardState().mode, Control::Mode::Manual);
        board.peer()->write(autoStatus);
        QTRY_COMPARE(client.boardState().mode, Control::Mode::Auto);
        QVERIFY(client.requestManual());
        QTRY_VERIFY(board.peer()->bytesAvailable() >= 8);
        board.peer()->readAll();
        board.peer()->write(QByteArray::fromHex("4c574952010200080000000000000000")
                            + QByteArray::fromHex("4c57495201010003020200"));
        QTRY_COMPARE(resets.count(), 2);
        QCOMPARE(client.boardState().mode, Control::Mode::Manual);
        QCOMPARE(acks.count(), 1); // ACK after INIT has no matching pending command.
        QVERIFY(client.requestManual());
    }

    void timeoutReconnectDoesNotReplay()
    {
        RawBoard board;
        QVERIFY(board.server.listen(QHostAddress::LocalHost, 0));
        ControlClient client;
        auto settings = settingsFor(board.server.serverPort());
        settings.ackTimeoutMs = 120;
        QSignalSpy errors(&client, &ControlClient::errorOccurred);
        client.start(settings);
        QTRY_VERIFY(client.canControl());
        QVERIFY(client.requestHome());
        QTRY_VERIFY(board.peer()->bytesAvailable() >= 8);
        QCOMPARE(board.peer()->readAll(), QByteArray::fromHex("4c57495203030000"));
        auto *old = board.peer();
        QTRY_VERIFY_WITH_TIMEOUT(board.peers.size() >= 2, 2000);
        QTRY_VERIFY(client.canControl());
        QVERIFY(!errors.isEmpty());
        QCOMPARE(old->state(), QAbstractSocket::UnconnectedState);
        QTest::qWait(40);
        QCOMPARE(board.peer()->bytesAvailable(), qint64(0)); // No old HOME/MOVE replay.
        QVERIFY(client.requestHome()); // Pending state was cleared on reconnect.
    }

    void badPacketAndMissingSnapshotReconnect()
    {
        RawBoard board;
        QVERIFY(board.server.listen(QHostAddress::LocalHost, 0));
        ControlClient client;
        auto settings = settingsFor(board.server.serverPort());
        settings.connectTimeoutMs = 150;
        client.start(settings);
        QTRY_VERIFY(client.canControl());
        board.initialState = false;
        board.peer()->write(QByteArray::fromHex("4c5749520103ffff"));
        QTRY_VERIFY_WITH_TIMEOUT(board.peers.size() >= 2, 1500);
        QVERIFY(!client.canControl());
        QTRY_VERIFY_WITH_TIMEOUT(board.peers.size() >= 3, 1500);
        QVERIFY(!client.canControl()); // A connected socket alone is not board readiness.
        board.peer()->write(manualStatus);
        QTRY_VERIFY(client.canControl());
    }

    void mockModesAndManualIntervention()
    {
        MockControlServer board;
        QVERIFY(board.listen(QHostAddress::LocalHost, 0));
        ControlClient client;
        QSignalSpy acks(&client, &ControlClient::commandFinished);
        QSignalSpy states(&client, &ControlClient::boardStateChanged);
        client.start(settingsFor(board.port()));
        QTRY_VERIFY(client.canControl());
        QVERIFY(client.requestAuto(999));
        QTRY_COMPARE(acks.count(), 1);
        QCOMPARE(qvariant_cast<Control::Ack>(acks.last()[0]).result, Control::Result::InvalidTarget);
        QCOMPARE(client.boardState().mode, Control::Mode::Manual);
        QVERIFY(client.requestAuto(7));
        QTRY_COMPARE(client.boardState().selectedTargetId, quint32(7));
        QVERIFY(client.requestAuto(15));
        QTRY_COMPARE(client.boardState().selectedTargetId, quint32(15));
        QVERIFY(client.sendMove(0, 0));
        QTest::qWait(30);
        QCOMPARE(board.boardState().mode, Control::Mode::Auto);
        QVERIFY(client.sendMove(-1, 1));
        QTRY_COMPARE(client.boardState().gimbal, Control::GimbalState::ManualMoving);
        QCOMPARE(client.boardState().mode, Control::Mode::Manual);
        QVERIFY(client.sendMove(0, 0));
        QTRY_COMPARE(client.boardState().gimbal, Control::GimbalState::Idle);
        const auto count = states.count();
        const auto ackCount = acks.count();
        QVERIFY(client.requestManual());
        QTRY_COMPARE(acks.count(), ackCount + 1);
        QCOMPARE(states.count(), count); // Idempotent MANUAL does not invent a STATUS change.
        Control::BoardState lost{Control::Mode::Auto, Control::TargetState::Lost, Control::GimbalState::Hold, 0, 7};
        board.publishState(lost);
        QTRY_COMPARE(client.boardState().target, Control::TargetState::Lost);
        QCOMPARE(client.boardState().selectedTargetId, quint32(7));
    }

    void homingRejectsAndDoesNotDefer()
    {
        MockControlServer board;
        board.setHomeDuration(300);
        QVERIFY(board.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, board.port());
        QTRY_VERIFY(socket.bytesAvailable() >= 16);
        socket.readAll();
        socket.write(QByteArray::fromHex("4c57495203030000")
                     + QByteArray::fromHex("4c5749520201000400000007")
                     + QByteArray::fromHex("4c57495202020000")
                     + QByteArray::fromHex("4c574952030100020101")
                     + QByteArray::fromHex("4c57495203030000"));
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::Homing);
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::Idle);
        QTRY_VERIFY(socket.bytesAvailable() >= 76);
        auto bytes = socket.readAll();
        int ok = 0, rejected = 0, states = 0;
        while (!bytes.isEmpty()) {
            Control::Message message;
            QString error;
            QCOMPARE(Control::takeMessage(bytes, message, error), Control::ParseResult::Complete);
            if (message.command == quint8(Control::System::Ack)) {
                Control::Ack ack;
                QVERIFY(Control::decodeAck(message.payload, ack));
                if (ack.result == Control::Result::Ok) ++ok;
                if (ack.result == Control::Result::InvalidState) ++rejected;
            } else ++states;
        }
        QCOMPARE(ok, 1);
        QCOMPARE(rejected, 3);
        QCOMPARE(states, 2);
        QCOMPARE(board.boardState().mode, Control::Mode::Manual);
        QCOMPARE(board.boardState().selectedTargetId, quint32(0));
    }

    void pingTimeoutAndBoardLiveness()
    {
        MockControlServer board;
        board.setPingReplies(false);
        board.setHeartbeatTimeout(150);
        QVERIFY(board.listen(QHostAddress::LocalHost, 0));
        ControlClient client;
        auto settings = settingsFor(board.port());
        settings.pingIntervalMs = 40;
        settings.ackTimeoutMs = 120;
        QSignalSpy errors(&client, &ControlClient::errorOccurred);
        client.start(settings);
        QTRY_VERIFY(client.canControl());
        QVERIFY(client.sendMove(1, 0));
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::ManualMoving);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 1500);
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::Idle);
        client.stop();
        board.setPingReplies(true);
        client.start(settings);
        QTRY_VERIFY(client.canControl());
        QVERIFY(client.requestAuto(7));
        QTRY_COMPARE(board.boardState().mode, Control::Mode::Auto);
        client.stop();
        QTest::qWait(200);
        QCOMPARE(board.boardState().mode, Control::Mode::Auto); // Healthy AUTO survives PC loss.
    }

    void manualHeartbeatWithoutPing()
    {
        MockControlServer board;
        board.setHeartbeatTimeout(100);
        QVERIFY(board.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, board.port());
        QTRY_VERIFY(socket.bytesAvailable() >= 16);
        socket.write(QByteArray::fromHex("4c574952030100020100"));
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::ManualMoving);
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::Idle);
        QCOMPARE(socket.state(), QAbstractSocket::ConnectedState);
    }

    void keyMasks_data()
    {
        QTest::addColumn<int>("mask");
        QTest::addColumn<int>("x");
        QTest::addColumn<int>("y");
        const int expected[16][2] = {{0,0},{-1,0},{1,0},{0,0},{0,1},{-1,1},{1,1},{0,1},
                                     {0,-1},{-1,-1},{1,-1},{0,-1},{0,0},{-1,0},{1,0},{0,0}};
        for (int mask = 0; mask < 16; ++mask)
            QTest::newRow(qPrintable(QString::number(mask))) << mask << expected[mask][0] << expected[mask][1];
    }

    void pipelineErrorRequiresExplicitNewAuto()
    {
        MockControlServer board;
        board.setHomeDuration(120);
        QVERIFY(board.listen(QHostAddress::LocalHost, 0));
        ControlClient client;
        client.start(settingsFor(board.port()));
        QTRY_VERIFY(client.canControl());
        QVERIFY(client.requestAuto(7));
        QTRY_COMPARE(client.boardState().mode, Control::Mode::Auto);
        Control::BoardState failed;
        failed.gimbal = Control::GimbalState::Error;
        failed.errorCode = 1; // A synthetic fault, not a production error-code assignment.
        board.publishState(failed);
        QTRY_COMPARE(client.boardState().gimbal, Control::GimbalState::Error);
        QVERIFY(!client.canControl());
        QVERIFY(!client.requestAuto(7));
        QVERIFY(!client.sendMove(1, 0));
        board.publishState({});
        QTRY_VERIFY(client.canControl());
        QCOMPARE(client.boardState().mode, Control::Mode::Manual);
        QCOMPARE(client.boardState().selectedTargetId, quint32(0));
        QVERIFY(client.requestAuto(7));
        QTRY_COMPARE(client.boardState().mode, Control::Mode::Auto);
        QVERIFY(client.requestHome());
        QTRY_COMPARE(client.boardState().gimbal, Control::GimbalState::Homing);
        board.publishState(failed);
        QTest::qWait(180);
        QCOMPARE(board.boardState().gimbal, Control::GimbalState::Error);
        QCOMPARE(client.boardState().gimbal, Control::GimbalState::Error);
    }

    void keyMasks()
    {
        QFETCH(int, mask);
        QFETCH(int, x);
        QFETCH(int, y);
        QWidget surface;
        ManualInputHandler input(&surface);
        input.setEnabled(true);
        QSignalSpy moves(&input, &ManualInputHandler::moveRequested);
        const int keys[] = {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down};
        for (int i = 0; i < 4; ++i) if (mask & (1 << i)) QTest::keyPress(&surface, Qt::Key(keys[i]));
        if (mask) {
            QVERIFY(!moves.isEmpty());
            QCOMPARE(moves.last()[0].toInt(), x);
            QCOMPARE(moves.last()[1].toInt(), y);
        } else QCOMPARE(moves.count(), 0);
        for (int i = 0; i < 4; ++i) if (mask & (1 << i)) QTest::keyRelease(&surface, Qt::Key(keys[i]));
        if (mask) {
            QCOMPARE(moves.last()[0].toInt(), 0);
            QCOMPARE(moves.last()[1].toInt(), 0);
        }
    }

    void inputRepeatHomeAndFocus()
    {
        QWidget surface;
        QLineEdit editor;
        ManualInputHandler input(&surface);
        input.setEnabled(true);
        QSignalSpy moves(&input, &ManualInputHandler::moveRequested);
        QSignalSpy homes(&input, &ManualInputHandler::homeRequested);
        QTest::keyPress(&surface, Qt::Key_Left);
        QTest::keyPress(&surface, Qt::Key_Left);
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier, {}, true);
        QApplication::sendEvent(&surface, &repeat);
        QCOMPARE(moves.count(), 1);
        QTest::keyPress(&surface, Qt::Key_Space);
        QTest::keyPress(&surface, Qt::Key_Space);
        QCOMPARE(homes.count(), 1);
        QTest::keyRelease(&surface, Qt::Key_Left);
        QCOMPARE(moves.count(), 1); // HOME cleared the old key; no obsolete MOVE(0,0).
        QTest::keyRelease(&surface, Qt::Key_Space);
        QTest::keyPress(&editor, Qt::Key_Up);
        QTest::keyClick(&editor, Qt::Key_Space);
        QCOMPARE(moves.count(), 1);
        QCOMPARE(homes.count(), 1);
        QTest::keyPress(&surface, Qt::Key_Right);
        QFocusEvent focusOut(QEvent::FocusOut);
        QApplication::sendEvent(&surface, &focusOut);
        QCOMPARE(moves.last()[0].toInt(), 0);
        const auto count = moves.count();
        input.setEnabled(false);
        QTest::keyRelease(&surface, Qt::Key_Right);
        QTest::keyPress(&surface, Qt::Key_Left);
        QCOMPARE(moves.count(), count);
        input.setEnabled(true);
        QTest::keyRelease(&surface, Qt::Key_Left);
        QCOMPARE(moves.count(), count); // Re-enable must not restore a held key.
    }

    void controllerAndGuiUseBoardState()
    {
        MockControlServer board;
        board.setHomeDuration(300);
        QVERIFY(board.listen(QHostAddress::LocalHost, 0));
        ApplicationController controller;
        MainWindow window;
        QImage image(640, 480, QImage::Format_RGB32);
        image.fill(Qt::gray);
        ClassLabels labels;
        QVERIFY(labels.load(QFINDTESTDATA("../../config/class_names.txt")));
        window.setClassLabels(labels);
        window.setDisplayFrame(image, {{7, QRectF(100, 50, 200, 100), 0}}, 1);
        window.show();
        ManualInputHandler input(window.manualInputSurface());
        connect(&controller, &ApplicationController::controlConnectionChanged, &window, &MainWindow::setControlConnection);
        connect(&controller, &ApplicationController::boardStateChanged, &window, &MainWindow::setBoardState);
        connect(&controller, &ApplicationController::commandFinished, &window, &MainWindow::showCommandResult);
        connect(&controller, &ApplicationController::inputResetRequested, &input, &ManualInputHandler::reset);
        connect(&controller, &ApplicationController::manualInputEnabledChanged, &input, &ManualInputHandler::setEnabled);
        connect(&input, &ManualInputHandler::moveRequested, &controller, &ApplicationController::sendMove);
        connect(&input, &ManualInputHandler::homeRequested, &controller, &ApplicationController::requestHome);
        connect(&window, &MainWindow::autoRequested, &controller, &ApplicationController::requestAuto);
        connect(&window, &MainWindow::manualRequested, &controller, &ApplicationController::requestManual);
        auto *mode = window.findChild<QLabel *>("controlMode");
        QCOMPARE(mode->text(), QString("--"));
        controller.startControl(settingsFor(board.port()));
        QTRY_COMPARE(mode->text(), QString("MANUAL"));
        window.findChild<QComboBox *>("trackCombo")->setEditText("7");
        window.findChild<QPushButton *>("autoButton")->click();
        QTRY_COMPARE(mode->text(), QString("AUTO"));
        QTest::qWait(50); // Let the layouts resize values after the state snapshot.
        window.grab().save(QCoreApplication::applicationDirPath() + "/control-gui.png");
        window.manualInputSurface()->setFocus();
        QTest::keyPress(window.manualInputSurface(), Qt::Key_Left);
        QTest::keyPress(window.manualInputSurface(), Qt::Key_Up);
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::ManualMoving);
        QTRY_COMPARE(mode->text(), QString("MANUAL"));
        QTest::keyRelease(window.manualInputSurface(), Qt::Key_Left);
        QTest::keyRelease(window.manualInputSurface(), Qt::Key_Up);
        QTRY_COMPARE(board.boardState().gimbal, Control::GimbalState::Idle);
        QTest::keyClick(window.manualInputSurface(), Qt::Key_Space);
        QTRY_COMPARE(window.findChild<QLabel *>("gimbalState")->text(), QString("HOMING"));
        QVERIFY(!window.findChild<QPushButton *>("autoButton")->isEnabled());
        QTRY_COMPARE(window.findChild<QLabel *>("gimbalState")->text(), QString("IDLE"));
        controller.stop();
        QCOMPARE(mode->text(), QString("--"));
        QCOMPARE(window.findChild<QLabel *>("controlConnection")->text(), QString("Disconnected"));
    }

    void yamlAndRealApplicationConnection()
    {
        MockControlServer board;
        QVERIFY(board.listen(QHostAddress::LocalHost, 0));
        QSignalSpy requests(&board, &MockControlServer::commandReceived);
        QTemporaryDir directory;
        const auto path = directory.filePath("pc.yaml");
        QFile config(path);
        QVERIFY(config.open(QIODevice::WriteOnly));
        const QByteArray bytes = "video:\n  codec: h264\ncontrol:\n  enabled: true\n  host: 127.0.0.1\n  port: "
            + QByteArray::number(board.port()) + "\n  ping_interval_ms: 50\n";
        config.write(bytes);
        config.close();
        const auto settings = loadAppConfig(path).control;
        QVERIFY(settings.enabled);
        QCOMPARE(settings.port, board.port());
        QCOMPARE(settings.pingIntervalMs, 50);
        QProcess app;
        app.setProgram(QCoreApplication::applicationDirPath() + "/pc-application");
        app.setArguments({"--config", path, "--video-port", "15020", "--metadata-port", "15021"});
        app.start();
        QVERIFY(app.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!requests.isEmpty(), 5000);
        QCOMPARE(qvariant_cast<Control::Message>(requests.first()[0]).command, quint8(Control::System::Ping));
        QCOMPARE(app.state(), QProcess::Running);
        app.terminate();
        QVERIFY(app.waitForFinished(5000));
        QCOMPARE(app.exitCode(), 0);
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        config.write(bytes + "  ack_timeout_ms: 0\n");
        config.close();
        QVERIFY_EXCEPTION_THROWN(loadAppConfig(path), std::runtime_error);
    }
};
QTEST_MAIN(ControlTest)
#include "ControlTest.moc"
