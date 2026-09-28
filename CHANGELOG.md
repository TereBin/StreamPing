# 변경 이력

## 0.4.0

- Discord `/streamping-role` 명령을 이용한 역할 선택 추가
- 선택한 역할을 방송 알림 앞에 자동 멘션
- Discord 사용자 멘션 파싱을 제거하고 역할, `@everyone`, `@here`만 허용
- Discord Interaction 서명, 서버, 채널, 권한 검증 추가

## 0.3.2

- Discord 사용자, 역할, `@everyone`, `@here` 멘션 전송 지원
- 메시지 편집기에 Discord ID 기반 멘션 형식 안내 추가
- Discord 전송 payload 단위 테스트 추가

## 0.3.1

- Discord와 X 활성화 설정을 분리
- X 작성 화면 열기를 신규 설정의 기본값으로 변경
- Discord/X 메시지 편집기 높이를 통일
- LIVE 확인 설정을 기본 접힘 패널로 변경
- 설정 키와 메시지 테스트 경로 리팩터링

## 0.3.0

- 무료 X Web Intent 작성 화면과 테스트 기능 추가
- Discord/X 중복 실행 상태를 개별 관리

## 0.2.1

- 실제 치지직 데이터 또는 예시 값을 이용한 Discord 메시지 테스트 추가

## 0.2.0

- Discord OAuth2 Webhook 자동 연결 추가
- Webhook DPAPI 암호화 저장 및 안전한 연결 해제 추가

## 0.1.2

- WinHTTP/SChannel 통신으로 전환
- OBS 경로 자동 탐색 설치기 추가
