# LWIR PC Application

설계 기준은 Notion의 [Application 설계](https://app.notion.com/p/3f29de3f14a280829f90dc2dfd666d04)에서 관리한다. 이 파일은 개발환경과 실행 방법만 다룬다.

## 개발환경

- CMake 3.25 이상, C++20
- Qt 6 Widgets/Network (Windows: MSVC x64 Kit)
- GStreamer 1.22 이상 Runtime + Development (Windows: MSVC x64)
- 현재 검증 환경: Qt 6.11.2, GStreamer 1.28.7, MSVC

[GStreamer 공식 다운로드](https://gstreamer.freedesktop.org/download/)에서 Windows MSVC x86_64의 Runtime and development headers를 설치한다. CMake는 기본 사용자/시스템 설치 경로를 검색한다. 다른 경로라면 `-DGSTREAMER_ROOT=C:/path/to/gstreamer`를 지정한다.

VS Code의 CMake Tools에서 Configure와 Debug Build를 실행한다. 현재 `.vscode/settings.json`의 Qt Kit 설정을 사용한다. 명령행에서는 CMake 실행 파일이 PATH에 있어야 한다.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build --config Debug
```

Windows 빌드 후 Qt/GStreamer DLL, 필요한 GStreamer 플러그인, Plugin Scanner를 실행 파일 옆에 배치한다. 개발 PoC에서는 GStreamer의 bin DLL을 함께 복사하며, 배포용 최소 의존성 패키징은 별도 작업이다.

## 영상 수신 PoC

기본 실행은 RTP/JPEG UDP 5000 수신이다. 송신기가 없으면 No video로 시작한다. 영상과 함께 Dummy BBox를 표시하지 않는다. Orange Pi 연결 표시는 TCP Control 상태이므로, 영상만 수신해도 Offline을 유지한다.

수신기와 송신기를 별도 터미널에서 실행한다. F5로 수신기를 실행해도 된다.

```powershell
# 터미널 1: 수신기
.\build\Debug\pc-application.exe --video-trace build/receiver.csv

# 터미널 2: 640x480, 30 FPS Mock Sender
.\build\Debug\mock-video-sender.exe --trace build/sender.csv
```

Mock Sender는 움직이는 테스트 영상을 `appsrc → videoconvert → I420 → jpegenc → jpegparse → rtpjpegpay → udpsink`로 전송한다. `--frames 300`을 지정하면 300 Frame 후 종료하며 기본값은 계속 송신이다. 다른 PC로 송신하려면 `--host 192.168.10.1`을 지정한다.

수신 포트는 `--video-port`, 송신 포트는 `--port`로 지정한다. `--video-latency 50`은 수신 Jitter Buffer의 실험값(ms)이다. JPEG Quality 80, RTP MTU 1200, appsink 1 Buffer/오래된 Buffer 폐기도 현재 PoC 조건이며 최종 통합값이 아니다.

Phase 1 GUI만 확인하려면:

```powershell
.\build\Debug\pc-application.exe --gui-preview
```

CSV는 Sender의 appsrc PTS와 RTP Timestamp, Receiver의 Jitter Buffer 이후 RTP Timestamp/PTS와 appsink PTS를 기록한다. Receiver의 `frameId=0`은 Camera Frame ID가 아직 없음을 뜻한다. 매칭되지 않은 Timestamp는 빈 칸으로 기록한다. 경과시간은 각 프로세스의 로컬 시계이며 서로 직접 비교해 End-to-End Latency로 사용하면 안 된다.

Decode 이후 PTS가 RTP 숫자와 같다고 가정하지 않는다. 현재 수신기는 RTP Marker Packet의 PTS와 appsink PTS를 대응시키는 PoC 관측만 수행하며, Metadata의 최종 Sync Key를 확정하지 않는다.

## 통합 시험

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

시험은 UDP 15000으로 실제 Mock Sender를 실행하고 영상 크기, FPS, RTP/PTS 대응, 영상 메모리 수명, Dummy Track 제거, Stop/Restart 및 잘못된 Trace 경로 처리를 확인한다. `build/Debug`에 Sender/Receiver CSV와 GUI 캡처를 남긴다.
