# Python GUI integration status

This directory preserves the current `model-bank/tracking_server/page/` layout and
visual design. The transport-facing JavaScript has small LAN adaptations for
Runtime catalog names, unavailable entries and connection errors. It was copied
from model-bank `main` at
`3897aaf572fae1d87f9c8cc3d7db85028cc79501` on 2026-11-12.
`python3 page/build_page.py --check` verifies the generated HTML against its
31 HTML/CSS/JS parts. Do not edit `page/live_page.html` directly.

`lan_protocol.py` implements the existing direct LAN TCP command framing,
catalog queries, accepted/completed model and tracker changes, mode/gimbal
commands, active tracker policy query, and UDP tracking metadata decoding.
The page's KeepID switch selects the available ByteTrack/Post-ReID preset;
its camera stabilization switch updates Runtime GMC. Model and tracker entries
come from Runtime catalog replies.
`desktop_routes.py` maps the original page's supported requests to those
commands. Unsupported actions return an explicit failure; they never claim to
apply a setting. The focused tests are:

```sh
PYTHONPATH=python_gui python3 -m unittest python_gui/test_lan_protocol.py python_gui/test_desktop_routes.py
```

`desktop_gui.py` runs the page inside Gtk/WebKit2. It receives the
Runtime's H.264/RTP video and tracking metadata on UDP 5000/5001 and uses
the existing Runtime TCP 5002 command service. It does not start an HTTP,
MJPEG, or WebSocket server. Start it on the PC with:

```sh
python3 python_gui/desktop_gui.py --board BOARD_LAN_IP
```

The host needs Python GI bindings for Gtk3, WebKit2 4.0, GStreamer 1.0,
GstVideo 1.0, GStreamer H.264 decoding plugins, and Pillow. Configure the
Runtime's video and metadata destinations to the PC IP. The original page's
`/stream` image is supplied by decoded RTP frames inside the desktop process;
the latest UDP metadata is overlaid. This display path has **not** been
measured for frame synchronization or sustained frame rate.

The performance graph shows measured PC video and metadata receive rates.
Runtime processing latency is not sent in this metadata contract, so its graph
fields remain unavailable rather than showing fabricated zeros.

The LAN bridge currently supports dynamic detection and tracker catalog
queries and changes, active tracker policy readback, warmth/edge/confirmation
and GMC setting changes, ByteTrack KeepID preset switching, click selection,
manual move/stop, and status/people readback.
Policy changes replace the active tracker and reset its tracks/IDs. Filter
tiles can restore a threshold only after the corresponding value was entered
in Filter settings. Prediction control and predicted box display require new
Runtime commands and are unavailable. Hot ReID switching, measurement statistics,
replay, camera, and system controls also require Runtime LAN commands and
return an explicit failure. The confirmation frame count readback is the
current count after frame rate adjustment. Do not treat this as a feature
complete replacement or launch the Python tracking HTTP server.

The final layout follows the source page; its transport and recording labels
are adapted to the direct LAN contract. The Orange Pi keeps its C++ Runtime
and Benchmark architecture.

# 원본 녹화

화면의 측정 시작·중지 버튼은 LAN TCP 5002의 Recording 카테고리로 C++ Runtime을
제어한다. Runtime은 Orange Pi의 `measurement.output_root/recordings/`에
`frames.raw.gz`(640×480 16비트 원본), `frames.csv`, `tracks.csv`,
`recording.txt`를 저장한다.
디스크가 느리면 녹화 큐에서 놓친 프레임 수를 표시한다. 측정 화면의
`최근 녹화 PC로 내보내기`는 완료된 최신 녹화의 네 파일을 기존 TCP 5002
명령 연결로 받아 PC `/workspace/recordings/MEASURE_*/`에 저장한다.
전송 중에는 `.partial` 디렉터리를 사용하고 gzip 무결성을 확인한다.
PC에 동일 이름의 녹화가 이미 있으면 덮어쓰지 않는다. 실제 장비에서는
PC와 Orange Pi를 유선 LAN으로 연결하고 `--board`에는 Orange Pi의
유선 IP를 지정한다. 인터넷, Tailscale, SSH, 별도 HTTP 서버는 필요 없다.
이 화면에서 재생하는 기능과 지난 녹화 목록은 아직 연결되지 않았다.
네트워크는 기존 UDP 5000/5001·TCP 5002만 사용한다.
