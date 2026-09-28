# StreamPing Discord 연결 서비스

Discord OAuth2의 `webhook.incoming` 승인과 선택형 `/streamping-role` 역할 설정 명령을
처리하는 Cloudflare Worker입니다. Webhook과 선택한 역할은 플러그인이 한 번 가져갈
때까지만 KV에 저장되며 모든 연결 세션은 10분 후 만료됩니다.

## 배포

1. Discord Developer Portal에서 애플리케이션을 만듭니다.
2. OAuth2 Redirect에 `https://<worker-domain>/v1/discord/callback`을 등록합니다.
3. General Information의 Public Key를 `DISCORD_PUBLIC_KEY`에 넣습니다.
4. Interactions Endpoint URL을
   `https://<worker-domain>/v1/discord/interactions`로 설정합니다.
5. Cloudflare에서 KV namespace를 만들고 `wrangler.toml.example`을
   `wrangler.toml`로 복사한 뒤 ID, 애플리케이션 ID, Worker 주소를 채웁니다.
6. 세 Rate Limiting binding의 `namespace_id`를 같은 Cloudflare 계정 안에서 서로
   겹치지 않는 양의 정수로 바꿉니다.
7. 애플리케이션 Secret을 Worker secret으로 등록하고 배포합니다.

```powershell
cd worker
npx wrangler secret put DISCORD_CLIENT_SECRET
npx wrangler deploy
```

Secret은 소스나 `wrangler.toml`에 적지 않습니다. 배포가 끝나면 플러그인을 Worker
주소와 함께 다시 구성하고 빌드합니다.

Worker는 첫 Discord 연결 요청에서 `streamping-role` 명령을 자동 등록합니다. OAuth에서
서버와 채널을 선택하면 Webhook 연결은 즉시 완료됩니다. 사용자가 플러그인의 `역할 선택`
버튼을 누른 경우에만 해당 채널에서 `/streamping-role`을 실행해 알림 역할을 지정합니다.
명령 실행 요청은 Discord 애플리케이션 Public Key로 검증하며 Webhook 관리 권한이 있는
사용자만 역할을 확정할 수 있습니다.

Rate Limiting binding은 익명 세션 생성을 전역 분당 30회, 클라이언트당 분당 5회로
제한합니다. callback과 poll 요청은 클라이언트당 분당 60회로 제한합니다. Cloudflare
Rate Limiting API를 사용하려면 Wrangler 4.36.0 이상이 필요합니다.

```powershell
cmake -S . -B build -DSTREAMPING_CONNECT_SERVICE_URL=https://<worker-domain>
cmake --build build --config RelWithDebInfo
```
