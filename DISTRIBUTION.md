# StreamPing 0.5.1

OBS Studio에서 치지직 LIVE 상태를 확인해 Discord 알림을 보내고 X 작성 화면을
준비하는 Windows x64 플러그인입니다.

## 설치 및 업데이트

1. OBS Studio를 완전히 종료합니다.
2. ZIP 파일을 원하는 폴더에 압축 해제합니다.
3. `install.cmd`를 실행합니다.
4. 관리자 권한 요청이 표시되면 승인합니다.
5. OBS를 실행하고 `도구 > StreamPing 설정`을 엽니다.

StreamPing은 기본적으로 OBS 실행 후 하루에 한 번 새 버전을 확인합니다. 새 버전이 있으면
중요도에 따라 선택, 권장 또는 필수 업데이트 알림을 표시합니다. 설정 화면의 **업데이트 >
지금 확인**에서 직접 확인하거나 자동 확인을 끌 수 있습니다. 업데이트는 자동으로 설치되지
않으며 다운로드 페이지를 연 뒤 OBS를 종료하고 설치해야 합니다.

설치기는 OBS 위치를 자동으로 찾고 기존 DLL을 `streamping.dll.bak`으로 백업합니다.
포터블 OBS는 명령 프롬프트에서 `install.cmd -ObsPath D:\Apps\obs-studio`처럼
경로를 지정할 수 있습니다.

## 설정

1. 치지직 채널 URL 또는 32자리 채널 ID를 입력합니다.
2. Discord의 `Discord 연결`을 누르고 서버와 알림 채널을 승인합니다.
3. 역할 멘션이 필요하면 `역할 선택`을 누르고 Discord 채널에서 `/streamping-role`을
   실행합니다.
4. 필요하면 Discord와 X 메시지 템플릿을 수정합니다.
5. 각 테스트 버튼으로 결과를 확인하고 저장합니다.

사용 가능한 메시지 변수는 `{role}`, `{title}`, `{category}`, `{channel}`, `{url}`입니다.
`{role}`은 선택한 역할 멘션으로 바뀌며, 역할을 선택하지 않으면 빈 문자열이 됩니다.
역할 멘션이 필요 없으면 `{role}`을 넣지 않습니다. 메시지 템플릿에서는 `@everyone`과
`@here`도 직접 사용할 수 있습니다.
Discord 임베드는 알림을 보내기 직전에 확인한 현재 방송 제목, 채널, 카테고리와 LIVE
썸네일을 사용합니다. Discord가 치지직 링크에서 자동 생성하는 미리보기는 표시하지
않으므로 이전 방송 정보가 나타나는 문제를 줄입니다.
X는 내용을 채운 작성 화면만 열며 사용자가 게시 버튼을 누르기 전에는 게시되지 않습니다.

## 제거

OBS를 종료한 뒤 다음 파일을 삭제합니다.

- `obs-plugins/64bit/streamping.dll`
- `obs-plugins/64bit/streamping.dll.bak`
- `data/obs-plugins/streamping/`

Discord 연결을 먼저 해제하면 StreamPing이 생성한 Webhook도 함께 삭제됩니다.

소스 코드와 이슈: https://github.com/TereBin/StreamPing

## 라이선스

StreamPing은 동봉된 `LICENSE`의 GNU General Public License v2.0으로 배포됩니다.
