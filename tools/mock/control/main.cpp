#include "MockControlServer.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTextStream>
#include <QtEndian>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("LWIR mock board control server; no hardware access");
    parser.addHelpOption();
    parser.addOption({"port", "TCP listen port (0 selects an available port).", "port", "5002"});
    parser.addOption({"host", "Listen address.", "address", "127.0.0.1"});
    parser.addOption({"track-ids", "Valid synthetic Track IDs, comma separated.", "ids", "3,7,15,22"});
    parser.addOption({"home-ms", "Simulated HOME duration, not hardware timing.", "ms", "800"});
    parser.process(app);
    bool portOk = false, homeOk = false;
    const auto port = parser.value("port").toUInt(&portOk);
    const int duration = parser.value("home-ms").toInt(&homeOk);
    const QHostAddress host(parser.value("host"));
    if (!portOk || port > 65535 || !homeOk || duration < 1 || duration > 60000 || host.isNull()) parser.showHelp(1);
    QSet<quint32> ids;
    for (const auto &text : parser.value("track-ids").split(',')) {
        bool valid = false;
        const auto id = text.toULongLong(&valid);
        if (!valid || !id || id > 0xffffffffULL) parser.showHelp(1);
        ids.insert(quint32(id));
    }
    MockControlServer server;
    server.setTrackIds(ids);
    server.setHomeDuration(duration);
    QObject::connect(&server, &MockControlServer::commandReceived, &app, [](const Control::Message &message) {
        if (message.category == Control::Category::System) return;
        QTextStream out(stdout);
        out << Control::commandName(message.category, message.command);
        if (message.category == Control::Category::Gimbal && message.command == quint8(Control::GimbalCommand::Move))
            out << " x=" << int(qint8(message.payload[0])) << " y=" << int(qint8(message.payload[1]));
        else if (message.category == Control::Category::Mode && message.command == quint8(Control::ModeCommand::Auto))
            out << " trackId=" << qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(message.payload.constData()));
        out << Qt::endl;
    });
    if (!server.listen(host, quint16(port))) { qCritical().noquote() << server.errorString(); return 1; }
    QTextStream(stdout) << "Mock control listening on " << host.toString() << ' ' << server.port() << Qt::endl;
    return app.exec();
}
