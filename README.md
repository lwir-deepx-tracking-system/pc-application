# LWIR PC Application

설계 문서: [Notion Application](https://app.notion.com/p/3f29de3f14a280829f90dc2dfd666d04)

- [수동 영상 테스트: 실행부터 CSV 분석까지](#수동-영상-테스트)
- [GUI 및 BBox 클릭 테스트](#gui-및-bbox-클릭-테스트)
- [CSV 항목과 해석](#계측-csv-사용)
- [자동 통합 시험](#자동-통합-시험)
- [문제가 생겼을 때](#문제가-생겼을-때)

## 개발환경

- CMake 3.25 이상, C++20
- Qt 6 Widgets/Network (Windows: MSVC x64 Kit)
- GStreamer 1.22 이상 Runtime + Development (Windows: MSVC x64)
- yaml-cpp 0.8 이상. 설치된 패키지가 없으면 CMake가 0.8.0의 고정 커밋을 받아 정적으로 빌드한다. 최초 구성에는 인터넷 연결이 필요하다.
- 현재 검증 환경: Qt 6.11.2, GStreamer 1.28.7, MSVC

[GStreamer 공식 다운로드](https://gstreamer.freedesktop.org/download/)에서 Windows MSVC x86_64의 Runtime and development headers를 설치한다. CMake는 기본 사용자/시스템 설치 경로를 검색한다. 다른 경로라면 `-DGSTREAMER_ROOT=C:/path/to/gstreamer`를 지정한다.

VS Code의 CMake Tools에서 Configure와 Debug Build를 실행한다. 현재 `.vscode/settings.json`의 Qt Kit 설정을 사용한다. 명령행에서는 CMake 실행 파일이 PATH에 있어야 한다.

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build --config Debug
```

Windows 빌드 시 Qt/GStreamer DLL, 플러그인, Plugin Scanner가 실행 파일 옆에 자동으로 복사된다.

## 영상 수신

영상 수신 포트는 기본 UDP 5000이다. 송신기가 없으면 `No video`가 표시된다.

코덱은 `config/pc-application.yaml`에서 설정한다. 아래 값을 `h264` 또는 `jpeg`로 수정한 뒤 GUI를 다시 실행한다. 보드 송신 코덱도 같아야 한다.

```yaml
video:
  codec: h264
```

프로젝트 루트에서 다음과 같이 실행한다. GUI의 `Video codec`은 현재 설정을 보여주는 읽기 전용 항목이다.

```powershell
.\build\Debug\pc-application.exe --config config/pc-application.yaml
```

`--config`를 생략하면 현재 작업 폴더의 `config/pc-application.yaml`을 먼저 찾고, 없으면 실행 파일 옆 `config/pc-application.yaml`을 읽는다. 배포용 설정은 빌드 시 복사된다. 파일이 없거나 코덱 값이 잘못되면 실행을 중단한다. 기존 `--video-codec` 옵션과 코덱 선택 드롭다운은 제거했다.

### 추적 메타데이터와 BBox

GUI는 기본 UDP **5001**에서 **LWIR** 메타데이터를 수신한다. 헤더는 28바이트(Magic 4, Frame ID 8, RTP timestamp 4, guiStartedUs 8, Track count 4), Track은 25바이트(ID 4, xywh float32 16, Class ID 1, Confidence float32 4)다. Version·메시지 종류·원본 크기·Detection count는 전송하지 않는다. 다중 바이트 필드는 Big Endian이며 원본 좌표계는 640×480 고정이다. Track ID 0은 무효, Class ID 255는 Unknown이다.

보드 송신부도 이 형식을 사용해야 한다. 기존 LWIM v2는 호환되지 않는다. 보드 `gui.metadata.enabled`를 `true`, `gui.metadata.host`를 PC IP, `gui.metadata.port`를 `5001`로 맞춘다. 영상 UDP 5000과 메타데이터 UDP 5001 모두 PC에 도달해야 한다. 수신 포트는 `--metadata-port`로 변경할 수 있다.

BBox는 원본 영상의 **왼쪽 위 x, y, width, height** 좌표다. 영상과 박스에 같은 배율과 중앙 여백을 적용하므로 창 크기·화면 배율을 바꿔도 위치가 맞는다. 박스 클릭도 같은 변환을 역으로 적용한다.

박스 라벨은 첫 줄에 `ID 7`, 그 아래에 `person`처럼 클래스를 표시한다. 보드가 보내는 `class_id`를 사용하며, 기본 이름 목록 `config/class_names.txt`는 현재 runtime의 YOLOv8n 모델 설정에 있는 80개 클래스 순서다. 빌드 시 실행 파일 옆으로 복사된다.

모델의 클래스 구성이 달라지면 UTF-8 파일에 **class 0부터 한 줄에 이름 하나씩** 작성해 지정한다. 빈 줄은 해당 ID의 이름이 없는 것으로 처리하며 이후 ID를 당기지 않는다. 목록에 없는 ID는 `Class 80`, 음수 ID는 `Unknown`으로 표시한다. 보드 모델과 PC 이름 목록은 같은 순서여야 한다.

```powershell
.\build\Debug\pc-application.exe --config config/pc-application.yaml --class-names config/class_names.txt
```

영상과 메타데이터는 RTP timestamp로 대응한다. 영상은 대기 없이 표시하며, 늦게 도착한 메타데이터는 현재 표시 중인 timestamp가 같을 때만 박스를 갱신한다. 다른 프레임의 박스는 재사용하지 않으며, 캐시는 최대 128개·500ms로 제한한다. 영상 크기가 640×480이 아니면 박스를 적용하지 않는다. 수신 stream 변경 시 대응 상태를 초기화한다.

화면 밖 bbox는 영상 영역으로 자르고, 폭·높이 0 또는 잘라낸 뒤 빈 박스는 표시하지 않는다. NaN/Inf·음수 크기·Confidence 범위(0~1) 초과·Track ID 0은 해당 Track만 제외한다. 패킷 Magic·길이·count가 잘못된 경우 패킷 전체를 폐기한다. Wire의 Track count와 검증 후 표시되는 박스 수는 다를 수 있다. guiStartedUs는 송신 작업 시작 시각이며 이것만으로 네트워크 지연을 계산하지 않는다.

`Metadata FPS`는 유효 패킷 수신율이며, `Track Count`는 현재 영상에 대응된 표시 박스 수다. GUI는 들어온 메타데이터만 사용하며, 검출 결과를 자체 생성하지 않는다. 대상 제어 명령 송신은 이번 수신·표시 기능과 별개다.

PC 내부에서 이동하는 합성 영상과 박스를 함께 확인하려면 YAML의 코덱을 `h264`로 맞추고 GUI를 실행한 뒤 별도 터미널에서 다음 명령을 실행한다.

```powershell
.\build\Debug\mock-video-sender.exe --codec h264 --metadata-port 5001 --frames 300
```

이 Mock의 박스는 시험용 주석이며 NPU 검출 결과가 아니다. 자동 시험은 실제 UDP 수신, JPEG/H.264의 프레임 대응, 잘못된 패킷·지연·만료 처리, 창 크기·원본 해상도·DPI에 따른 박스 렌더링 및 클릭 위치를 확인한다.

JPEG는 RTP payload 26, H.264는 96이며 영상 포트는 둘 다 5000이다. 코덱 변경은 YAML 수정 후 GUI를 재실행하여 적용한다. 같은 경로의 `--video-trace` 기록은 재실행 시 덮어쓰므로 시험별로 파일명을 지정한다.

`Decode + delivery`는 최근 약 1초 동안 처리한 프레임의 **평균 처리시간(ms)** 이다. Jitter Buffer를 지난 마지막 RTP 조각부터 디코딩·RGB 변환·앱의 이미지 복사 완료까지 측정하며, 최대 약 5ms의 polling 대기도 포함한다. 네트워크 전송시간과 실제 화면 paint 시간은 포함하지 않는다. 프레임별 타임스탬프 CSV를 켤 필요는 없다.

두 코덱 모두 같은 Jitter Buffer(기본 50ms)와 최신 이미지 1개 정책을 사용한다. H.264는 `h264parse`와 `avdec_h264`를 사용하며, Windows 배포에 필요한 `videoparsersbad`, `libav` 플러그인도 복사한다. H.264는 연결 중간에 수신을 시작하면 다음 IDR까지 기다릴 수 있다.

PC 내부 시험에서 H.264 Mock Sender는 다음과 같이 실행한다. Mock은 소프트웨어 인코딩이므로 보드 하드웨어 성능 비교에는 사용하지 않는다.

```powershell
.\build\Debug\mock-video-sender.exe --codec h264 --frames 300
```

실제 비교는 보드의 `/home/orangepi/lwir-work/runtime-jpeg`에서 빌드한 송신 도구로 같은 RAW16 파일 또는 16비트 PNG 디렉터리를 재생한다. 다음 명령은 보드 SSH 터미널에서 각각 실행하며, PC YAML의 코덱도 맞추고 GUI를 재실행한다.

```bash
SEND=/home/orangepi/.cache/lwir-runtime/build/lwir_video_send
$SEND --input /path/to/frames --codec jpeg --host <PC_IP> --seconds 30 --output /tmp/jpeg.csv
$SEND --input /path/to/frames --codec h264 --host <PC_IP> --seconds 30 --output /tmp/h264.csv
```

보드 CSV는 전처리·인코딩/RTP의 평균 ms, CPU 사용률, 출력 FPS, RTP Mbps를 기록한다. 품질·비트레이트·표시 범위와 측정 구간의 상세 설명은 보드의 `tools/VIDEO_COMPARE.md`에 있다.

보드 연결을 포함하는 선택 시험은 다음과 같다. 기본 자동 시험에는 보드가 필요하지 않다.

```powershell
$env:LWIR_BOARD_VIDEO_HOST = "<보드에서 접근 가능한 PC IPv4>"
.\build\Debug\video-integration-test.exe receiveBoard -o build/board-test.txt,txt
```

Windows UDP 수신이 차단된 환경에서는 `$env:LWIR_BOARD_VIDEO_RELAY = "1"`로 SSH 경유 기능 검증을 할 수 있다. 이 경로는 Python과 SSH alias `pi`, 보드의 `/home/orangepi/.cache/lwir-runtime/build/lwir_video_send`를 사용한다. **SSH 경유 결과를 이더넷 성능 비교에 사용하지 않는다.** 직접 UDP 시험에는 해당 환경변수를 제거하고 PC UDP 5000의 수신 허용이 필요하다.

아래 절차는 같은 PC에서 Mock 영상을 송수신하고 수신 처리 시간을 측정한다.

Mock Sender는 움직이는 테스트 영상을 `appsrc → videoconvert → I420 → jpegenc → jpegparse → rtpjpegpay → udpsink`로 전송한다. `--frames 300`을 지정하면 300 Frame 후 종료하며 기본값은 계속 송신이다. 다른 PC로 송신하려면 `--host 192.168.10.1`을 지정한다.

수신 포트는 `--video-port`, 송신 포트는 `--port`로 지정한다. Jitter Buffer 지연은 `--video-latency`로 설정하며 기본값은 50ms다. JPEG Quality는 80, RTP MTU는 1200이며 appsink는 최신 Buffer 1개를 유지한다.

## 수동 영상 테스트

### 1. 준비 및 빌드

VS Code에서 프로젝트를 열고 PowerShell 터미널 두 개를 만든다. 두 터미널의 작업 폴더를 프로젝트 루트로 맞춘다. 아래 경로는 Clone한 위치에 맞게 바꾼다.

```powershell
Set-Location C:\Project\LWIR_Project\pc-application
```

처음 실행하거나 코드를 수정했다면 VS Code의 CMake Tools에서 Configure 후 **Debug Build**를 실행한다. 명령행 빌드는 위의 [개발환경](#개발환경) 명령을 사용한다. 빌드 후 `build/Debug/pc-application.exe`와 `build/Debug/mock-video-sender.exe`가 있어야 한다.

### 2. 터미널 1에서 수신기 실행

아래 절차는 `config/pc-application.yaml`의 `video.codec: h264`를 기준으로 한다.

```powershell
.\build\Debug\pc-application.exe --video-trace build/receiver.csv
```

송신 전에는 `No video`가 표시된다. CSV 기록에는 `--video-trace`가 필요하다. 기본 F5 실행에는 이 옵션이 없다.

### 3. 터미널 2에서 Mock Sender 실행

```powershell
.\build\Debug\mock-video-sender.exe --codec h264 --frames 300 --trace build/sender.csv
```

약 10초 동안 640×480, 30 FPS 테스트 영상을 전송한 뒤 송신기가 자동 종료한다. 더 오래 관찰하려면 `--frames 900`처럼 Frame 수를 늘린다.

### 4. 송신 중 정상 동작 확인

- 움직이는 원과 Frame 번호가 있는 Mock 영상이 보인다.
- `Video FPS`에 약 30 FPS가 표시된다.
- 창 크기를 바꿔도 영상 비율이 유지된다.
- Mock은 영상만 전송하므로 BBox가 없고 Orange Pi 연결 표시는 `Offline`이다.

창을 잠시 최소화했다가 복원하면 표시되지 않은 Frame의 `displayed=0` 기록도 확인할 수 있다.

### 5. 송신 종료 후 수신기 창 닫기

송신기가 종료되면 수신기 창도 닫는다. 창을 닫을 때 남은 Frame 요약이 CSV에 저장된다.

같은 명령으로 다시 시험하면 기존 CSV를 덮어쓴다. 비교할 기록은 먼저 다른 이름으로 보관한다.

### 6. PC 내부 지연을 ms로 분석

프로젝트 루트의 PowerShell에서 실행한다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/mock/video/Measure-VideoTrace.ps1 -Path build/receiver.csv -OutputCsv build/receiver-ms.csv
```

세 구간의 최소·평균·최대 시간이 ms로 표시되고, Frame별 결과가 `build/receiver-ms.csv`에 저장된다.

| 지표 | 측정 구간 |
| --- | --- |
| `FirstRtpToPaintMs` | 첫 RTP 관측 → GUI Draw |
| `MarkerToAppsinkMs` | Marker 관측 → Decode Sample 확보 |
| `AppsinkToPaintMs` | Decode Sample 확보 → GUI Draw |

| 결과 파일 | 내용 |
| --- | --- |
| `build/receiver.csv` | 수신 Frame 요약과 원본 시각, `displayed` 상태 |
| `build/sender.csv` | 송신 Frame ID, PTS, SSRC/RTP Timestamp 대응 및 원본 시각 |
| `build/receiver-ms.csv` | Frame별 PC 내부 지연 계산 결과 |

### 필요할 때 Packet 상세도 기록

기본 절차는 Frame 요약만 기록한다. Packet 순서나 Marker를 조사할 때는 2·3단계 명령을 각각 다음으로 바꾼다. 종료·분석 순서는 동일하다.

```powershell
# 터미널 1
.\build\Debug\pc-application.exe --video-trace build/receiver.csv --video-packet-trace build/receiver-packets.csv

# 터미널 2
.\build\Debug\mock-video-sender.exe --codec h264 --frames 300 --trace build/sender.csv --packet-trace build/sender-packets.csv
```

Packet 기록은 `build/receiver-packets.csv`, `build/sender-packets.csv`에 저장된다. ms 분석에는 `build/receiver.csv`를 사용한다.

## GUI 및 BBox 클릭 테스트

영상 수신기를 종료한 뒤 Dummy 영상과 BBox가 있는 GUI Preview를 실행한다. 송신기는 필요 없다.

```powershell
.\build\Debug\pc-application.exe --gui-preview
```

- BBox를 클릭하면 해당 Track ID의 Target Set 요청이 발생한다.
- 영상의 BBox가 없는 부분을 클릭하면 Target Clear 요청이 발생한다.
- Track ID를 입력하거나 선택한 뒤 Track 버튼으로 요청할 수 있다.

클릭 요청은 디버그 로그의 `TARGET_SET(ID) [GUI mock]`, `TARGET_CLEAR [GUI mock]`으로 확인한다. VS Code 디버거로 Preview를 실행하려면 `.vscode/launch.json`의 `Debug pc-application` 구성에 `"args": ["--gui-preview"]`를 추가한 뒤 F5를 누른다. 영상 수신 시험으로 돌아갈 때는 해당 인수를 제거한다.

## 계측 CSV 사용

Trace 옵션을 생략하면 기록하지 않는다. `--video-trace` / `--trace`는 Frame 요약을 켜며 Packet 상세는 별도 옵션으로 필요한 시험에서만 켠다. 두 출력에는 서로 다른 파일 경로를 사용한다.

최근 256개 Frame의 요약은 메모리에 보관되며 수신기를 닫을 때 저장된다. Packet 상세와 확정된 요약은 250ms 간격으로 저장한다.

| Frame 요약 열 | 의미 |
| --- | --- |
| `role`, `frameId` | Sender: Mock 영상 생성 ID. Receiver: 로컬 Frame 전달 ID. `0`은 appsink 전달 기록이 없는 Frame. |
| `ssrc`, `rtpTimestamp` | 송수신 기록에서 같은 Frame을 찾는 식별값. |
| `ptsNs`, `jitterPtsNs` | Sender appsrc PTS / Receiver appsink PTS, 그리고 Receiver의 Jitter Buffer 이후 Marker PTS. |
| `firstSequence`, `lastSequence` | 해당 Frame에서 처음/마지막으로 관측한 Sequence. |
| `firstRtpNs`, `lastRtpNs`, `markerRtpNs` | Sender payloader 출력 / Receiver Jitter Buffer 이전의 첫·마지막 Packet 및 마지막으로 관측한 Marker 시각. |
| `acquiredNs` | Mock 영상 생성 완료 시각. |
| `appsinkNs`, `paintNs` | Decode된 Sample을 가져온 시각, 해당 이미지의 첫 QPainter 그리기 완료 시각. |
| `displayed` | Receiver에서 실제 Draw했으면 `1`, 관측 기간에 Draw하지 않았으면 `0`. Sender는 빈 칸. |
| `clockDomain`, `timeUnit` | `local_steady_clock`, `ns`. 같은 장비 내부의 Monotonic 시각끼리만 지연을 계산한다. |

Packet 상세 CSV의 열은 `role,ssrc,rtpTimestamp,sequence,marker,observedMonotonicNs,clockDomain,timeUnit`이다.

매칭되지 않은 값은 빈 칸이다. `displayed=0`은 표시 기록이 없다는 뜻이며, Buffer 폐기나 GUI 표시 생략도 포함한다.

분석 명령과 세 구간의 지표는 [수동 영상 테스트](#수동-영상-테스트)의 6단계에 정리되어 있다. 누락되거나 시간 순서가 역전된 구간은 계산에서 제외한다.

ms 분석은 같은 PC의 Monotonic 시각 차이로 수신 처리 시간을 계산한다. 장비 간 전송 시간은 이 결과에 포함되지 않는다.

## 자동 통합 시험

수동 시험용 수신기와 송신기를 닫고 프로젝트 루트에서 실행한다. CMake와 CTest가 PATH에 있어야 하며, 시험은 별도의 UDP 15000 포트를 사용한다.

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

시험은 UDP 15000으로 실제 Mock Sender를 실행하고 영상 크기, FPS, RTP/PTS 대응, 영상 메모리 수명, Dummy Track 제거, Stop/Restart 및 잘못된 Trace 경로 처리를 확인한다. 첫 Draw 중복 방지, 표시 생략, SSRC 분리, Marker 이후 Packet 도착, RTP Timestamp Wrap, 기록 보관 상한도 검사한다. `build/Debug`에 Sender/Receiver 요약·Packet CSV와 GUI 캡처를 남긴다.

`100% tests passed`가 표시되면 통과다. `No tests were found`라면 `BUILD_TESTING=ON`으로 Configure한 뒤 다시 빌드했는지 확인한다.

## 문제가 생겼을 때

| 증상 | 확인 방법 |
| --- | --- |
| 실행 파일을 찾을 수 없음 | 터미널의 현재 폴더가 프로젝트 루트인지, Debug Build가 완료됐는지 확인한다. |
| `cmake` / `ctest` 명령을 찾을 수 없음 | 개발환경의 PATH를 확인한다. 빌드는 VS Code CMake Tools에서도 실행할 수 있다. |
| `No video`가 계속 표시됨 | 수신기를 먼저 실행했는지, Mock Sender가 아직 송신 중인지 확인한다. 기본 송신 주소는 `127.0.0.1`, 양쪽 기본 영상 포트는 UDP 5000이다. |
| DLL 또는 GStreamer Plugin 오류 | Configure와 Debug Build를 다시 실행해 Runtime 배치를 완료한다. 실행 파일 옆의 DLL과 `gstreamer-1.0` 폴더가 필요하다. |
| CSV가 없거나 Header만 있음 | Trace 옵션과 출력 폴더를 확인하고, 수신기를 정상 종료한 뒤 확인한다. 최신 Frame 요약은 실행 중 미확정 상태일 수 있다. |
| `No finalized frame rows` | 수신기 창을 닫은 뒤 올바른 `receiver.csv`로 분석을 다시 실행한다. |
| `Missing column` 또는 Sender/Packet CSV 오류 | `-Path`에는 현재 버전의 Receiver Frame 요약 CSV를 지정한다. |
