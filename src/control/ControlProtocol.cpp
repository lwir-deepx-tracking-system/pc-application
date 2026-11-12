#include "ControlProtocol.h"
#include <QtEndian>

namespace Control {
int payloadSize(Category category, quint8 command)
{
    switch (category) {
    case Category::System:
        switch (System(command)) {
        case System::Ack: return 3;
        case System::Init: case System::Status: return 8;
        case System::Ping: return 0;
        }
        break;
    case Category::Mode:
        if (command == quint8(ModeCommand::Auto)) return 4;
        if (command == quint8(ModeCommand::Manual)) return 0;
        break;
    case Category::Gimbal:
        if (command == quint8(GimbalCommand::Move)) return 2;
        if (command == quint8(GimbalCommand::Home)) return 0;
        break;
    case Category::Pipeline:
        switch (PipelineCommand(command)) {
        case PipelineCommand::ModelCount: case PipelineCommand::TrackerCount: return 4;
        case PipelineCommand::ChangeModel: case PipelineCommand::ChangeTracker:
        case PipelineCommand::ModelCountReply: case PipelineCommand::TrackerCountReply: return 8;
        case PipelineCommand::ModelResult: case PipelineCommand::TrackerResult: return 6;
        }
        break;
    }
    return -1;
}

bool needsAck(Category category, quint8 command)
{
    return (category == Category::System && command == quint8(System::Ping))
        || (category == Category::Mode && payloadSize(category, command) >= 0)
        || (category == Category::Gimbal && command == quint8(GimbalCommand::Home));
}

QByteArray encode(const Message &message)
{
    if (payloadSize(message.category, message.command) != message.payload.size()) return {};
    QByteArray bytes(HeaderSize, '\0');
    auto *data = reinterpret_cast<uchar *>(bytes.data());
    qToBigEndian(Magic, data);
    data[4] = quint8(message.category);
    data[5] = message.command;
    qToBigEndian(quint16(message.payload.size()), data + 6);
    bytes += message.payload;
    return bytes;
}

ParseResult takeMessage(QByteArray &buffer, Message &message, QString &error)
{
    if (buffer.size() < HeaderSize) return ParseResult::Incomplete;
    const auto *data = reinterpret_cast<const uchar *>(buffer.constData());
    const auto category = Category(data[4]);
    const int size = payloadSize(category, data[5]);
    if (qFromBigEndian<quint32>(data) != Magic || size < 0
        || qFromBigEndian<quint16>(data + 6) != size) {
        error = "Invalid control magic, command or payload size";
        return ParseResult::Invalid;
    }
    if (buffer.size() < HeaderSize + size) return ParseResult::Incomplete;
    message = {category, data[5], buffer.mid(HeaderSize, size)};
    buffer.remove(0, HeaderSize + size);
    return ParseResult::Complete;
}

QByteArray encodeState(const BoardState &state)
{
    QByteArray payload(8, '\0');
    auto *data = reinterpret_cast<uchar *>(payload.data());
    data[0] = quint8(state.mode);
    data[1] = quint8(state.target);
    data[2] = quint8(state.gimbal);
    data[3] = state.errorCode;
    qToBigEndian(state.selectedTargetId, data + 4);
    return payload;
}

bool decodeState(const QByteArray &payload, BoardState &state)
{
    if (payload.size() != 8) return false;
    const auto *data = reinterpret_cast<const uchar *>(payload.constData());
    if (data[0] > quint8(Mode::Auto) || data[1] > quint8(TargetState::Lost)
        || data[2] > quint8(GimbalState::Error)) return false;
    const auto id = qFromBigEndian<quint32>(data + 4);
    if ((data[1] == quint8(TargetState::None)) != (id == 0)) return false;
    if (data[0] == quint8(Mode::Manual) && (id || data[1])) return false;
    if (data[0] == quint8(Mode::Auto) && !id) return false;
    state = {Mode(data[0]), TargetState(data[1]), GimbalState(data[2]), data[3], id};
    return true;
}

bool decodeAck(const QByteArray &payload, Ack &ack)
{
    if (payload.size() != 3) return false;
    const auto *data = reinterpret_cast<const uchar *>(payload.constData());
    if (!needsAck(Category(data[0]), data[1]) || data[2] > quint8(Result::NotReady)) return false;
    ack = {Category(data[0]), data[1], Result(data[2])};
    return true;
}

QString resultName(Result result)
{
    switch (result) {
    case Result::Ok: return "OK";
    case Result::InvalidCommand: return "INVALID_COMMAND";
    case Result::InvalidPayload: return "INVALID_PAYLOAD";
    case Result::InvalidState: return "INVALID_STATE";
    case Result::InvalidTarget: return "INVALID_TARGET";
    case Result::NotReady: return "NOT_READY";
    }
    return "Unknown";
}

QString commandName(Category category, quint8 command)
{
    if (category == Category::Mode) return command == quint8(ModeCommand::Auto) ? "AUTO" : "MANUAL";
    if (category == Category::Gimbal) return command == quint8(GimbalCommand::Move) ? "MOVE" : "HOME";
    return "PING";
}

QString pipelineResultName(PipelineResult result, bool tracker)
{
    switch (result) {
    case PipelineResult::Ok: return tracker ? "Tracker changed" : "Model changed";
    case PipelineResult::InvalidId: return tracker ? "Invalid tracker ID" : "Invalid model ID";
    case PipelineResult::Busy: return "Board busy; change rejected";
    case PipelineResult::CreationFailed: return tracker ? "Tracker creation failed" : "Model creation failed";
    case PipelineResult::Restored: return tracker ? "Change failed; previous tracker restored" : "Change failed; previous model restored";
    case PipelineResult::RestoreFailed: return tracker ? "Tracker restore failed; no active tracker" : "Model restore failed; no active model";
    case PipelineResult::NotReady: return tracker ? "Tracker service not ready" : "Model service not ready";
    }
    return tracker ? "Unknown tracker result" : "Unknown model result";
}
}
