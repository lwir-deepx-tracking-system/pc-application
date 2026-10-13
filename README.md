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
.\build\Debug\mock-video-sender.exe --frames 300 --trace build/sender.csv
```

Mock Sender는 움직이는 테스트 영상을 `appsrc → videoconvert → I420 → jpegenc → jpegparse → rtpjpegpay → udpsink`로 전송한다. `--frames 300`을 지정하면 300 Frame 후 종료하며 기본값은 계속 송신이다. 다른 PC로 송신하려면 `--host 192.168.10.1`을 지정한다.

수신 포트는 `--video-port`, 송신 포트는 `--port`로 지정한다. `--video-latency 50`은 수신 Jitter Buffer의 실험값(ms)이다. JPEG Quality 80, RTP MTU 1200, appsink 1 Buffer/오래된 Buffer 폐기도 현재 PoC 조건이며 최종 통합값이 아니다.

Phase 1 GUI만 확인하려면:

```powershell
.\build\Debug\pc-application.exe --gui-preview
```

## 계측 CSV 사용

Trace 옵션을 생략하면 기록하지 않는다. `--video-trace` / `--trace`는 Frame 요약을 켜며 Packet 상세는 별도 옵션으로 필요한 시험에서만 켠다. 두 출력에는 서로 다른 파일 경로를 사용한다.

```powershell
.\build\Debug\pc-application.exe --video-trace build/receiver.csv --video-packet-trace build/receiver-packets.csv
.\build\Debug\mock-video-sender.exe --frames 300 --trace build/sender.csv --packet-trace build/sender-packets.csv
```

수신기는 창을 닫아 정상 종료한 뒤 요약 CSV를 분석한다. 최대 256개 Frame을 보관하고 오래된 Frame부터 요약을 확정하며, 종료 시 남은 Frame을 기록한다. 따라서 실행 중 최신 요약은 아직 파일에 없을 수 있다. Packet 상세와 확정된 요약은 약 250ms마다 묶어서 저장한다. Streaming Probe에서는 디스크에 쓰지 않는다. 파일별 대기 큐 상한은 1MiB이며 초과한 **로그 Row**나 파일 쓰기 오류는 콘솔에 경고한다. 강제 종료 시 미확정 기록은 남지 않을 수 있다. 이 보관량과 저장 간격은 PoC 구현값이다.

| Frame 요약 열 | 의미 |
| --- | --- |
| `role`, `frameId` | Sender는 Mock 영상 생성 ID. Receiver는 로컬 Frame 전달 ID이며 Camera Frame ID가 아니다. `0`은 appsink 전달을 관측하지 못한 Frame이다. |
| `ssrc`, `rtpTimestamp` | 송수신 기록의 Frame 대응 후보. PTS와 별도로 보관한다. |
| `ptsNs`, `jitterPtsNs` | Sender appsrc PTS / Receiver appsink PTS, 그리고 Receiver의 Jitter Buffer 이후 Marker PTS. |
| `firstSequence`, `lastSequence` | 해당 Frame에서 처음/마지막으로 **관측한** Sequence. 도착 순서이며 유실 계산값이 아니다. |
| `firstRtpNs`, `lastRtpNs`, `markerRtpNs` | Sender payloader 출력 / Receiver Jitter Buffer 이전의 첫·마지막 Packet 및 마지막으로 관측한 Marker 시각. |
| `acquiredNs` | Mock 영상 생성 완료 시각. 실제 Camera Exposure 시각이 아니다. |
| `appsinkNs`, `paintNs` | Decode된 Sample을 가져온 시각, 해당 이미지의 첫 QPainter Draw 완료 시각. 모니터 발광 시각이 아니다. |
| `displayed` | Receiver에서 실제 Draw했으면 `1`, 관측 기간에 Draw하지 않았으면 `0`. Sender는 빈 칸. |
| `clockDomain`, `timeUnit` | `local_steady_clock`, `ns`. 같은 장비 내부의 Monotonic 시각끼리만 지연을 계산한다. |

Packet 상세 CSV는 `role,ssrc,rtpTimestamp,sequence,marker,observedMonotonicNs,clockDomain,timeUnit`를 기록한다. 이는 Application Pipeline의 관측 시각이며 NIC의 물리적인 송수신 시각이 아니다. Marker를 관측해도 모든 Packet이 도착했다고 단정하지 않는다.

매칭되지 않은 값은 빈 칸으로 남긴다. `displayed=0`에는 Decode/Buffer 폐기, GUI 표시 생략, 종료 시 아직 대기 중인 Frame 등이 섞일 수 있으므로 Network Loss로 계산하지 않는다. 보관 범위를 넘긴 지연이나 재사용된 SSRC/RTP Timestamp는 최종 Sync 정책이 확정되기 전의 PoC 한계다.

Decode 이후 PTS가 RTP 숫자와 같다고 가정하지 않는다. 현재 수신기는 RTP Marker Packet의 PTS와 appsink PTS를 대응시키는 PoC 관측만 수행하며, Metadata의 최종 Sync Key를 확정하지 않는다.

다음 명령은 Receiver 원본 CSV에서 세 구간의 최소·평균·최대 시간을 ms로 출력하고, 선택적으로 Frame별 계산 결과를 저장한다. 누락되거나 시간 순서가 역전된 구간은 계산에서 제외한다. 실행 정책은 이 PowerShell 프로세스에만 적용된다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/mock/video/Measure-VideoTrace.ps1 -Path build/receiver.csv -OutputCsv build/receiver-ms.csv
```

- `FirstRtpToPaintMs`: 첫 RTP 관측 → GUI Draw
- `MarkerToAppsinkMs`: Marker 관측 → Decode Sample 확보
- `AppsinkToPaintMs`: Decode Sample 확보 → GUI Draw

서로 다른 장비의 Monotonic 시각을 빼거나 PTS를 Monotonic 시각에서 빼서 End-to-End 지연으로 해석하지 않는다. Ethernet RTT 또는 RTT/2도 실제 Video 편도 전송 시간이 아니다.

## 통합 시험

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

시험은 UDP 15000으로 실제 Mock Sender를 실행하고 영상 크기, FPS, RTP/PTS 대응, 영상 메모리 수명, Dummy Track 제거, Stop/Restart 및 잘못된 Trace 경로 처리를 확인한다. 첫 Draw 중복 방지, 표시 생략, SSRC 분리, Marker 이후 Packet 도착, RTP Timestamp Wrap, 기록 보관 상한도 검사한다. `build/Debug`에 Sender/Receiver 요약·Packet CSV와 GUI 캡처를 남긴다.
