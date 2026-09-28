const DISCORD_API = "https://discord.com/api/v10";
const SESSION_TTL_SECONDS = 600;
const RESULT_TTL_SECONDS = 120;
const SESSION_ID_PATTERN = /^[A-Za-z0-9_-]{24}$/;
const TOKEN_PATTERN = /^[A-Za-z0-9_-]{43}$/;

function responseHeaders(extra = {}) {
  return {
    "cache-control": "no-store",
    "permissions-policy": "camera=(), microphone=(), geolocation=()",
    "referrer-policy": "no-referrer",
    "x-content-type-options": "nosniff",
    ...extra,
  };
}

function json(body, status = 200, headers = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: responseHeaders({
      "content-type": "application/json; charset=utf-8",
      ...headers,
    }),
  });
}

function text(body, status = 200) {
  return new Response(body, {
    status,
    headers: responseHeaders({ "content-type": "text/plain; charset=utf-8" }),
  });
}

function randomToken(bytes = 32) {
  const value = new Uint8Array(bytes);
  crypto.getRandomValues(value);
  return btoa(String.fromCharCode(...value))
    .replaceAll("+", "-")
    .replaceAll("/", "_")
    .replaceAll("=", "");
}

async function sha256(value) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value));
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function callbackUrl(env) {
  return `${env.PUBLIC_BASE_URL.replace(/\/$/, "")}/v1/discord/callback`;
}

function clientKey(request) {
  return request.headers.get("cf-connecting-ip") || "unknown";
}

async function enforceRateLimits(checks) {
  try {
    const results = await Promise.all(
      checks.map(([limiter, key]) => {
        if (!limiter?.limit)
          throw new Error("Rate limiter binding is missing");
        return limiter.limit({ key });
      }),
    );
    if (results.some(({ success }) => !success))
      return json({ error: "요청이 너무 많습니다. 잠시 후 다시 시도해 주세요." }, 429, {
        "retry-after": "60",
      });
  } catch {
    return json({ error: "연결 보호 서비스를 사용할 수 없습니다." }, 503);
  }
  return null;
}

function discordError(token, status) {
  const detail = token?.error_description || token?.message || token?.error;
  if (typeof detail !== "string" || !detail.trim())
    return `Discord OAuth HTTP ${status}`;
  return `Discord OAuth: ${detail.trim().slice(0, 240)}`;
}

async function createSession(request, env) {
  const rateLimitResponse = await enforceRateLimits([
    [env.SESSION_CREATE_GLOBAL, "all"],
    [env.SESSION_CREATE_CLIENT, clientKey(request)],
  ]);
  if (rateLimitResponse)
    return rateLimitResponse;

  const sessionId = randomToken(18);
  const state = randomToken(32);
  const pollToken = randomToken(32);
  const session = {
    status: "pending",
    state,
    pollTokenHash: await sha256(pollToken),
  };

  await Promise.all([
    env.SESSIONS.put(`session:${sessionId}`, JSON.stringify(session), {
      expirationTtl: SESSION_TTL_SECONDS,
    }),
    env.SESSIONS.put(`state:${state}`, sessionId, { expirationTtl: SESSION_TTL_SECONDS }),
  ]);

  const authorize = new URL("https://discord.com/oauth2/authorize");
  authorize.searchParams.set("client_id", env.DISCORD_CLIENT_ID);
  authorize.searchParams.set("redirect_uri", callbackUrl(env));
  authorize.searchParams.set("response_type", "code");
  authorize.searchParams.set("scope", "webhook.incoming");
  authorize.searchParams.set("state", state);

  return json({ sessionId, pollToken, authorizationUrl: authorize.toString() }, 201);
}

async function completeAuthorization(request, env) {
  const url = new URL(request.url);
  const state = url.searchParams.get("state") || "";
  const code = url.searchParams.get("code") || "";
  const oauthError = url.searchParams.get("error");
  if (!TOKEN_PATTERN.test(state) || code.length > 2048)
    return text("This StreamPing connection request is invalid.", 400);

  const rateLimitResponse = await enforceRateLimits([
    [env.SESSION_API_CLIENT, `callback:${clientKey(request)}`],
  ]);
  if (rateLimitResponse)
    return rateLimitResponse;

  const sessionId = state ? await env.SESSIONS.get(`state:${state}`) : null;
  if (!sessionId)
    return text("This StreamPing connection request has expired.", 400);

  const rawSession = await env.SESSIONS.get(`session:${sessionId}`);
  if (!rawSession)
    return text("This StreamPing connection request has expired.", 400);
  const session = JSON.parse(rawSession);
  if (session.status !== "pending" || session.state !== state)
    return text("This StreamPing connection request has expired.", 400);

  if (oauthError || !code) {
    session.status = "failed";
    session.error = "Discord 연결이 취소되었습니다.";
    await env.SESSIONS.put(`session:${sessionId}`, JSON.stringify(session), {
      expirationTtl: RESULT_TTL_SECONDS,
    });
    await env.SESSIONS.delete(`state:${state}`);
    return text("Discord connection was cancelled. You may close this tab.");
  }

  const form = new URLSearchParams({
    client_id: env.DISCORD_CLIENT_ID,
    client_secret: env.DISCORD_CLIENT_SECRET,
    grant_type: "authorization_code",
    code,
    redirect_uri: callbackUrl(env),
  });
  const tokenResponse = await fetch(`${DISCORD_API}/oauth2/token`, {
    method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body: form,
  });
  const tokenText = await tokenResponse.text();
  let token = {};
  try {
    token = JSON.parse(tokenText);
  } catch {
    token = {};
  }

  if (!tokenResponse.ok || !token.webhook?.id || !token.webhook?.token) {
    session.status = "failed";
    session.error = tokenResponse.ok
      ? `Discord 응답에 Webhook이 없습니다. 승인 범위: ${token.scope || "알 수 없음"}`
      : discordError(token, tokenResponse.status);
  } else {
    session.status = "complete";
    session.webhookUrl = token.webhook.url ||
      `https://discord.com/api/webhooks/${token.webhook.id}/${token.webhook.token}`;
    session.channelName = token.webhook.channel_id
      ? `채널 ${token.webhook.channel_id}`
      : "선택한 채널";
  }

  delete session.state;
  await Promise.all([
    env.SESSIONS.put(`session:${sessionId}`, JSON.stringify(session), {
      expirationTtl: RESULT_TTL_SECONDS,
    }),
    env.SESSIONS.delete(`state:${state}`),
  ]);

  return text(
    session.status === "complete"
      ? "StreamPing is connected to Discord. You may close this tab."
      : `${session.error}\n\nReturn to OBS and try again.`,
  );
}

async function pollSession(request, env, ctx, sessionId) {
  const authorization = request.headers.get("authorization") || "";
  const pollToken = authorization.startsWith("Bearer ") ? authorization.slice(7) : "";
  if (!TOKEN_PATTERN.test(pollToken))
    return json({ error: "연결 요청을 확인할 권한이 없습니다." }, 403);

  const rateLimitResponse = await enforceRateLimits([
    [env.SESSION_API_CLIENT, `poll:${clientKey(request)}`],
  ]);
  if (rateLimitResponse)
    return rateLimitResponse;

  const rawSession = await env.SESSIONS.get(`session:${sessionId}`);
  if (!rawSession)
    return json({ error: "연결 요청이 만료되었습니다." }, 404);

  const session = JSON.parse(rawSession);
  if (!pollToken || (await sha256(pollToken)) !== session.pollTokenHash)
    return json({ error: "연결 요청을 확인할 권한이 없습니다." }, 403);
  if (session.status === "pending")
    return json({ status: "pending" }, 202);
  if (session.status === "failed") {
    ctx.waitUntil(env.SESSIONS.delete(`session:${sessionId}`));
    return json({ error: session.error || "Discord 연결에 실패했습니다." }, 400);
  }

  ctx.waitUntil(env.SESSIONS.delete(`session:${sessionId}`));
  return json({
    status: "complete",
    webhookUrl: session.webhookUrl,
    channelName: session.channelName,
  });
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/health")
      return json({ status: "ok" });
    if (request.method === "POST" && url.pathname === "/v1/discord/sessions")
      return createSession(request, env);
    if (request.method === "GET" && url.pathname === "/v1/discord/callback")
      return completeAuthorization(request, env);

    const sessionPathPrefix = "/v1/discord/sessions/";
    if (request.method === "GET" && url.pathname.startsWith(sessionPathPrefix)) {
      const sessionId = url.pathname.slice(sessionPathPrefix.length);
      if (SESSION_ID_PATTERN.test(sessionId))
        return pollSession(request, env, ctx, sessionId);
    }
    return json({ error: "Not found" }, 404);
  },
};
