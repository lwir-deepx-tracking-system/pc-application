#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

namespace Control {
constexpr quint32 Magic = 0x4c574952;
constexpr int HeaderSize = 8;
enum class Category : quint8 { System = 1, Mode = 2, Gimbal = 3 };
enum class System : quint8 { Ack = 1, Init = 2, Status = 3, Ping = 4 };
enum class ModeCommand : quint8 { Auto = 1, Manual = 2 };
enum class GimbalCommand : quint8 { Move = 1, Home = 3 };
enum class Result : quint8 { Ok, InvalidCommand, InvalidPayload, InvalidState, InvalidTarget, NotReady };
enum class Mode : quint8 { Manual, Auto };
enum class TargetState : quint8 { None, Tracking, Lost };
enum class GimbalState : quint8 { Idle, ManualMoving, AutoTracking, Homing, Hold, Error };
enum class Connection : quint8 { Disconnected, Connecting, AwaitingState, Ready };

struct BoardState {
    Mode mode = Mode::Manual;
    TargetState target = TargetState::None;
    GimbalState gimbal = GimbalState::Idle;
    quint8 errorCode = 0;
    quint32 selectedTargetId = 0;
    bool operator==(const BoardState &) const = default;
};
struct Message {
    Category category = Category::System;
    quint8 command = 0;
    QByteArray payload;
};
struct Ack {
    Category category;
    quint8 command;
    Result result;
};
enum class ParseResult { Incomplete, Complete, Invalid };

int payloadSize(Category category, quint8 command);
bool needsAck(Category category, quint8 command);
QByteArray encode(const Message &message);
ParseResult takeMessage(QByteArray &buffer, Message &message, QString &error);
QByteArray encodeState(const BoardState &state);
bool decodeState(const QByteArray &payload, BoardState &state);
bool decodeAck(const QByteArray &payload, Ack &ack);
QString resultName(Result result);
QString commandName(Category category, quint8 command);
}
Q_DECLARE_METATYPE(Control::BoardState)
Q_DECLARE_METATYPE(Control::Connection)
Q_DECLARE_METATYPE(Control::Message)
Q_DECLARE_METATYPE(Control::Ack)
