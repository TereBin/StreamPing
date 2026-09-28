const DISCORD_API = "https://discord.com/api/v10";
const SESSION_TTL_SECONDS = 600;
const RESULT_TTL_SECONDS = 120;
const SESSION_ID_PATTERN = /^[A-Za-z0-9_-]{24}$/;
const TOKEN_PATTERN = /^[A-Za-z0-9_-]{43}$/;
const SNOWFLAKE_PATTERN = /^[0-9]{17,20}$/;
const ROLE_COMMAND_NAME = "streamping-role";
const ROLE_COMMAND_CACHE_KEY = "discord-command:streamping-role:v1";
const MANAGE_WEBHOOKS_PERMISSION = 1n << 29n;

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

function hexBytes(value) {
  if (typeof value !== "string" || value.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(value))
    return null;
  return Uint8Array.from(value.match(/.{2}/g), (byte) => Number.parseInt(byte, 16));
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

async function ensureRoleCommand(env) {
  if (await env.SESSIONS.get(ROLE_COMMAND_CACHE_KEY))
    return;

  const tokenForm = new URLSearchParams({
    client_id: env.DISCORD_CLIENT_ID,
    client_secret: env.DISCORD_CLIENT_SECRET,
    grant_type: "client_credentials",
    scope: "applications.commands.update",
  });
  const tokenResponse = await fetch(`${DISCORD_API}/oauth2/token`, {
    method: "POST",
    headers: { "content-type": "application/x-www-form-urlencoded" },
    body: tokenForm,
  });
  const token = await tokenResponse.json().catch(() => ({}));
  if (!tokenResponse.ok || !token.access_token)
    throw new Error("Discord command token request failed");

  const commandResponse = await fetch(
    `${DISCORD_API}/applications/${env.DISCORD_CLIENT_ID}/commands`,
    {
      method: "POST",
      headers: {
        authorization: `Bearer ${token.access_token}`,
        "content-type": "application/json",
      },
      body: JSON.stringify({
        name: ROLE_COMMAND_NAME,
        description: "StreamPing 알림에서 멘션할 역할을 선택합니다.",
        type: 1,
        default_member_permissions: MANAGE_WEBHOOKS_PERMISSION.toString(),
        options: [
          {
            name: "role",
            description: "방송 알림을 받을 역할",
            type: 8,
            required: true,
          },
        ],
      }),
    },
  );
  if (!commandResponse.ok)
    throw new Error("Discord command registration failed");

  await env.SESSIONS.put(ROLE_COMMAND_CACHE_KEY, "ready", { expirationTtl: 86400 });
}

async function verifyInteraction(request, env, body) {
  const signature = hexBytes(request.headers.get("x-signature-ed25519"));
  const publicKey = hexBytes(env.DISCORD_PUBLIC_KEY);
  const timestamp = request.headers.get("x-signature-timestamp") || "";
  const timestampSeconds = Number(timestamp);
  if (!signature || signature.length !== 64 || !publicKey || publicKey.length !== 32 ||
      !Number.isFinite(timestampSeconds) ||
      Math.abs(Date.now() / 1000 - timestampSeconds) > 300)
    return false;

  try {
    const key = await crypto.subtle.importKey(
      "raw",
      publicKey,
      { name: "Ed25519" },
      false,
      ["verify"],
    );
    return crypto.subtle.verify(
      "Ed25519",
      key,
      signature,
      new TextEncoder().encode(timestamp + body),
    );
  } catch {
    return false;
  }
}

function interactionMessage(content) {
  return json({ type: 4, data: { content, flags: 64, allowed_mentions: { parse: [] } } });
}

async function selectRole(request, env) {
  const body = await request.text();
  if (!(await verifyInteraction(request, env, body)))
    return text("Invalid request signature", 401);

  let interaction;
  try {
    interaction = JSON.parse(body);
  } catch {
    return text("Invalid interaction", 400);
  }

  if (interaction.type === 1)
    return json({ type: 1 });
  if (interaction.type !== 2 || interaction.data?.name !== ROLE_COMMAND_NAME)
    return interactionMessage("지원하지 않는 StreamPing 명령입니다.");

  let permissions = 0n;
  try {
    permissions = BigInt(interaction.member?.permissions || "0");
  } catch {
    permissions = 0n;
  }
  if ((permissions & MANAGE_WEBHOOKS_PERMISSION) === 0n)
    return interactionMessage("Webhook 관리 권한이 있는 사용자만 역할을 설정할 수 있습니다.");

  const guildId = interaction.guild_id || "";
  const channelId = interaction.channel_id || "";
  const roleId = interaction.data?.options?.find((option) => option.name === "role")?.value || "";
  if (!SNOWFLAKE_PATTERN.test(guildId) || !SNOWFLAKE_PATTERN.test(channelId) ||
      !SNOWFLAKE_PATTERN.test(roleId) || roleId === guildId)
    return interactionMessage("선택한 역할을 확인할 수 없습니다.");

  const roleSessionKey = `role-target:${guildId}:${channelId}`;
  const sessionId = await env.SESSIONS.get(roleSessionKey);
  const rawSession = sessionId ? await env.SESSIONS.get(`session:${sessionId}`) : null;
  if (!rawSession)
    return interactionMessage("대기 중인 StreamPing 연결이 없습니다. OBS에서 다시 연결해 주세요.");

  const session = JSON.parse(rawSession);
  if (session.status !== "awaiting_role" || session.guildId !== guildId ||
      session.channelId !== channelId)
    return interactionMessage("이 채널의 StreamPing 연결 요청이 만료되었습니다.");

  const role = interaction.data?.resolved?.roles?.[roleId];
  session.status = "complete";
  session.roleId = roleId;
  session.roleName = typeof role?.name === "string" ? role.name.slice(0, 100) : "선택한 역할";
  delete session.guildId;
  delete session.channelId;
  await Promise.all([
    env.SESSIONS.put(`session:${sessionId}`, JSON.stringify(session), {
      expirationTtl: RESULT_TTL_SECONDS,
    }),
    env.SESSIONS.delete(roleSessionKey),
  ]);

  const mentionability = role?.mentionable
    ? "OBS로 돌아가 연결 완료를 확인하세요."
    : "역할 멘션이 허용되어 있는지도 Discord 역할 설정에서 확인해 주세요.";
  return interactionMessage(`@${session.roleName} 역할을 선택했습니다. ${mentionability}`);
}

async function createSession(request, env) {
  const rateLimitResponse = await enforceRateLimits([
    [env.SESSION_CREATE_GLOBAL, "all"],
    [env.SESSION_CREATE_CLIENT, clientKey(request)],
  ]);
  if (rateLimitResponse)
    return rateLimitResponse;

  try {
    await ensureRoleCommand(env);
  } catch {
    return json({ error: "Discord 역할 명령을 준비할 수 없습니다." }, 503);
  }

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
  authorize.searchParams.set("scope", "webhook.incoming applications.commands");
  authorize.searchParams.set("integration_type", "0");
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

  if (!tokenResponse.ok || !token.webhook?.id || !token.webhook?.token ||
      !SNOWFLAKE_PATTERN.test(token.webhook?.guild_id || "") ||
      !SNOWFLAKE_PATTERN.test(token.webhook?.channel_id || "")) {
    session.status = "failed";
    session.error = tokenResponse.ok
      ? `Discord 응답에 Webhook이 없습니다. 승인 범위: ${token.scope || "알 수 없음"}`
      : discordError(token, tokenResponse.status);
  } else {
    session.status = "awaiting_role";
    session.webhookUrl = token.webhook.url ||
      `https://discord.com/api/webhooks/${token.webhook.id}/${token.webhook.token}`;
    session.channelName = token.webhook.channel_id
      ? `채널 ${token.webhook.channel_id}`
      : "선택한 채널";
    session.guildId = token.webhook.guild_id;
    session.channelId = token.webhook.channel_id;
  }

  delete session.state;
  const writes = [
    env.SESSIONS.put(`session:${sessionId}`, JSON.stringify(session), {
      expirationTtl: session.status === "awaiting_role" ? SESSION_TTL_SECONDS : RESULT_TTL_SECONDS,
    }),
    env.SESSIONS.delete(`state:${state}`),
  ];
  if (session.status === "awaiting_role") {
    writes.push(env.SESSIONS.put(
      `role-target:${session.guildId}:${session.channelId}`,
      sessionId,
      { expirationTtl: SESSION_TTL_SECONDS },
    ));
  }
  await Promise.all(writes);

  return text(
    session.status === "awaiting_role"
      ? "선택한 Discord 채널에서 /streamping-role 명령을 실행하고 알림 역할을 선택하세요. OBS는 열린 상태로 두세요."
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
  if (session.status === "pending" || session.status === "awaiting_role")
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
    roleId: session.roleId,
    roleName: session.roleName,
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
    if (request.method === "POST" && url.pathname === "/v1/discord/interactions")
      return selectRole(request, env);

    const sessionPathPrefix = "/v1/discord/sessions/";
    if (request.method === "GET" && url.pathname.startsWith(sessionPathPrefix)) {
      const sessionId = url.pathname.slice(sessionPathPrefix.length);
      if (SESSION_ID_PATTERN.test(sessionId))
        return pollSession(request, env, ctx, sessionId);
    }
    return json({ error: "Not found" }, 404);
  },
};
