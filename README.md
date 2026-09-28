# StreamPing

StreamPing은 OBS Studio의 방송 시작 이벤트를 감지한 뒤 치지직 채널이 실제 LIVE
상태가 되었을 때 Discord 알림을 전송하고 X 작성 화면을 준비하는 Windows용 OBS
플러그인입니다.

- 배포 패키지: [StreamPing-Release](https://github.com/TereBin/StreamPing-Release/releases/latest)
- 소스 저장소: [StreamPing](https://github.com/TereBin/StreamPing)

## 기능

- 치지직 LIVE 상태 확인 및 재시도 간격 설정
- Discord OAuth2 연결과 수동 Webhook 호환
- Discord 알림 메시지 테스트
- Discord 명령으로 선택하는 역할 멘션과 `@everyone`, `@here` 지원
- API 비용 없는 X Web Intent 작성 화면
- `{title}`, `{category}`, `{channel}`, `{url}` 메시지 변수
- Discord와 X의 방송별 중복 실행 방지
- Windows DPAPI를 이용한 Discord Webhook 암호화 저장
- OBS 설치 경로 자동 탐색 및 업데이트

## 구조

- `src/`: OBS 플러그인과 Windows 네트워크/보안 구현
- `tests/`: 메시지, 입력 검증, X 작성 URL 테스트
- `worker/`: Discord OAuth2 Webhook 연결용 Cloudflare Worker
- `scripts/`: Windows 자동 설치기
- `data/`: OBS 로케일 데이터

치지직 조회는 특정 채널을 직접 조회하기 위해 웹 서비스의 공개 `live-detail`
엔드포인트를 사용합니다. 공식 Open API 계약이 아니므로 치지직 변경에 따라 업데이트가
필요할 수 있습니다.

## 빌드

요구 사항:

- Windows 10/11 x64
- Visual Studio 2022 C++ 도구
- CMake 3.26 이상
- 대상 OBS와 ABI가 맞는 libobs, obs-frontend-api, Qt 6 개발 파일

```powershell
cmake -S . -B build -A x64 `
  -DCMAKE_PREFIX_PATH="C:/path/to/obs-deps;C:/path/to/qt6;C:/path/to/obs-studio" `
  -DSTREAMPING_CONNECT_SERVICE_URL="https://your-worker.example"
cmake --build build --config RelWithDebInfo
cmake --install build --config RelWithDebInfo --prefix dist
```

Discord 자동 연결 서비스가 없는 빌드는 수동 Webhook 입력을 계속 사용할 수 있습니다.
Worker 배포 방법은 [worker/README.md](worker/README.md)를 참고하세요.

Discord 간편 연결 과정에서 `/streamping-role` 명령으로 역할을 선택하면 방송 알림 앞에
해당 역할 멘션이 자동으로 추가됩니다. 숫자 역할 ID를 직접 입력할 필요가 없습니다.

## 테스트

```powershell
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

`streamping-http-smoke`는 실제 치지직 HTTPS 통신을 확인하는 선택적 실행 파일입니다.
저장소에 채널 정보를 남기지 않도록 실행할 때 테스트 채널 ID를 전달합니다.

```powershell
.\build\RelWithDebInfo\streamping-http-smoke.exe <32자리-치지직-채널-ID>
```

## 보안

- Discord Client Secret과 실제 `worker/wrangler.toml`은 커밋하지 않습니다.
- Webhook URL은 현재 Windows 사용자 범위의 DPAPI 암호문으로 저장합니다.
- OAuth 연결 세션과 역할 선택 정보는 Worker KV에서 10분 후 만료되며 플러그인이 가져간
  뒤 삭제됩니다.
- Discord 명령 요청은 애플리케이션 Public Key로 서명을 검증합니다.
- X 기능은 API 토큰을 사용하지 않으며 사용자가 게시 전 내용을 확인합니다.
