"""Test-only MPP RTP relay over SSH when inbound Windows UDP is blocked.

No codec work is done here. Not a transport for Ethernet performance measurement.
Usage: python board_relay.py jpeg|h264
"""
import socket
import struct
import subprocess
import sys


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("jpeg", "h264"):
        raise SystemExit("Usage: board_relay.py jpeg|h264")
    remote = r'''
import socket, struct, subprocess, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
s.bind(('127.0.0.1', 0))
s.settimeout(.2)
p = subprocess.Popen(['/home/orangepi/.cache/lwir-runtime/build/lwir_video_send',
    '--synthetic', '--seconds', '5', '--codec', CODEC,
    '--host', '127.0.0.1', '--port', str(s.getsockname()[1])], stdout=subprocess.PIPE)
while True:
    try:
        packet = s.recv(65535)
        sys.stdout.buffer.write(struct.pack('!H', len(packet)) + packet)
        sys.stdout.buffer.flush()
    except socket.timeout:
        if p.poll() is not None:
            break
sys.stderr.buffer.write(p.communicate()[0])
sys.exit(p.returncode)
'''.replace("CODEC", repr(sys.argv[1]))
    process = subprocess.Popen(
        ["ssh", "-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "pi", "python3", "-"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    process.stdin.write(remote.encode())
    process.stdin.close()
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def read_exact(size):
        data = bytearray()
        while len(data) < size:
            part = process.stdout.read(size - len(data))
            if not part:
                if data:
                    raise RuntimeError("Truncated RTP relay record")
                return None
            data.extend(part)
        return data

    while True:
        header = read_exact(2)
        if header is None:
            break
        packet = read_exact(struct.unpack("!H", header)[0])
        if packet is None:
            raise RuntimeError("Missing RTP relay packet")
        udp.sendto(packet, ("127.0.0.1", 5000))
    sys.stdout.buffer.write(process.stderr.read())
    udp.close()
    return process.wait()


if __name__ == "__main__":
    sys.exit(main())
