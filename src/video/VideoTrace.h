#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

// Optional PoC trace. Streaming probes and the Qt thread share this writer.
class VideoTrace
{
public:
    bool open(const QString &path)
    {
        if (path.isEmpty())
            return true;
        file.setFileName(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        file.write("stage,frameId,ptsNs,rtpTimestamp,sequence,elapsedNs\n");
        clock.start();
        return true;
    }

    void record(const char *stage, quint64 frameId, quint64 pts,
                qint64 rtpTimestamp = -1, qint64 sequence = -1)
    {
        QMutexLocker lock(&mutex);
        if (!file.isOpen())
            return;
        const QByteArray line = QByteArray(stage) + ',' + QByteArray::number(frameId) + ','
            + (pts == quint64(-1) ? QByteArray() : QByteArray::number(pts)) + ','
            + (rtpTimestamp < 0 ? QByteArray() : QByteArray::number(rtpTimestamp)) + ','
            + (sequence < 0 ? QByteArray() : QByteArray::number(sequence)) + ','
            + QByteArray::number(clock.nsecsElapsed()) + '\n';
        file.write(line);
        file.flush(); // Keep optional PoC logs readable while the process is running.
    }

    void close()
    {
        QMutexLocker lock(&mutex);
        file.close();
    }

private:
    QFile file;
    QMutex mutex;
    QElapsedTimer clock;
};
