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

`desktop_gui.py` runs the unmodified page inside Gtk/WebKit2. It receives the
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

The LAN bridge currently supports dynamic detection and tracker catalog
queries and changes, active tracker policy readback, warmth/edge/confirmation
and GMC setting changes, ByteTrack KeepID preset switching, click selection,
manual move/stop, and status/people readback.
Policy changes replace the active tracker and reset its tracks/IDs. The
original page also calls routes for GMC toggle, ReID switching, measurement,
replay, performance, camera, and system controls. Those still require Runtime
LAN commands and are shown as unavailable or return an explicit failure. The confirmation frame count readback is
the current count after frame rate adjustment. Do not treat this as a feature complete
replacement or launch the Python tracking HTTP server.

The final layout remains the source page; only its transport boundary may be
adapted. The Orange Pi keeps its C++ Runtime and Benchmark architecture.
