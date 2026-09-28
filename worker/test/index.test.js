import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";
import test from "node:test";

import worker from "../src/index.js";

class MemoryKv {
  constructor(entries = {}) {
    this.entries = new Map(Object.entries(entries));
    this.reads = 0;
    this.writes = 0;
  }

  async get(key) {
    this.reads += 1;
    return this.entries.get(key) ?? null;
  }

  async put(key, value) {
    this.writes += 1;
    this.entries.set(key, value);
  }

  async delete(key) {
    this.entries.delete(key);
  }
}

function limiter(success = true) {
  return {
    calls: [],
    async limit({ key }) {
      this.calls.push(key);
      return { success };
    },
  };
}

function environment(overrides = {}) {
  const sessions = overrides.SESSIONS || new MemoryKv();
  sessions.entries.set("discord-command:streamping-role:v1", "ready");
  return {
    DISCORD_CLIENT_ID: "1234567890",
    DISCORD_CLIENT_SECRET: "test-only-secret",
    PUBLIC_BASE_URL: "https://worker.example",
    DISCORD_PUBLIC_KEY: "00".repeat(32),
    SESSIONS: sessions,
    SESSION_CREATE_GLOBAL: limiter(),
    SESSION_CREATE_CLIENT: limiter(),
    SESSION_API_CLIENT: limiter(),
    ...overrides,
  };
}

function hex(value) {
  return Buffer.from(value).toString("hex");
}

async function interactionRequest(payload, keyPair) {
  const body = JSON.stringify(payload);
  const timestamp = Math.floor(Date.now() / 1000).toString();
  const signature = await webcrypto.subtle.sign(
    "Ed25519",
    keyPair.privateKey,
    new TextEncoder().encode(timestamp + body),
  );
  return new Request("https://worker.example/v1/discord/interactions", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-signature-ed25519": hex(signature),
      "x-signature-timestamp": timestamp,
    },
    body,
  });
}

const context = { waitUntil() {} };

test("health check does not require service bindings", async () => {
  const response = await worker.fetch(new Request("https://worker.example/health"), {}, context);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { status: "ok" });
  assert.equal(response.headers.get("cache-control"), "no-store");
});

test("session creation is rejected before KV writes when rate limited", async () => {
  const sessions = new MemoryKv();
  const env = environment({ SESSIONS: sessions, SESSION_CREATE_GLOBAL: limiter(false) });
  const request = new Request("https://worker.example/v1/discord/sessions", {
    method: "POST",
    headers: { "cf-connecting-ip": "192.0.2.10" },
  });

  const response = await worker.fetch(request, env, context);
  assert.equal(response.status, 429);
  assert.equal(response.headers.get("retry-after"), "60");
  assert.equal(sessions.writes, 0);
});

test("session creation stores only hashed poll credentials", async () => {
  const sessions = new MemoryKv();
  const env = environment({ SESSIONS: sessions });
  const request = new Request("https://worker.example/v1/discord/sessions", {
    method: "POST",
    headers: { "cf-connecting-ip": "192.0.2.10" },
  });

  const response = await worker.fetch(request, env, context);
  const body = await response.json();
  assert.equal(response.status, 201);
  assert.match(body.sessionId, /^[A-Za-z0-9_-]{24}$/);
  assert.match(body.pollToken, /^[A-Za-z0-9_-]{43}$/);
  const authorizationUrl = new URL(body.authorizationUrl);
  assert.deepEqual(
    new Set(authorizationUrl.searchParams.get("scope").split(" ")),
    new Set(["webhook.incoming", "applications.commands"]),
  );
  assert.equal(authorizationUrl.searchParams.get("integration_type"), "0");

  const stored = JSON.parse(sessions.entries.get(`session:${body.sessionId}`));
  assert.equal(stored.pollToken, undefined);
  assert.match(stored.pollTokenHash, /^[0-9a-f]{64}$/);
  assert.equal(sessions.writes, 2);
});

test("session creation registers the role command when the cache is empty", { concurrency: false },
  async () => {
    const env = environment({ SESSIONS: new MemoryKv() });
    env.SESSIONS.entries.delete("discord-command:streamping-role:v1");
    const requests = [];
    const originalFetch = globalThis.fetch;
    globalThis.fetch = async (url, options) => {
      requests.push({ url: String(url), options });
      if (String(url).endsWith("/oauth2/token"))
        return Response.json({ access_token: "command-token" });
      return Response.json({ id: "423456789012345678", name: "streamping-role" });
    };

    try {
      const response = await worker.fetch(new Request(
        "https://worker.example/v1/discord/sessions",
        { method: "POST", headers: { "cf-connecting-ip": "192.0.2.10" } },
      ), env, context);
      assert.equal(response.status, 201);
    } finally {
      globalThis.fetch = originalFetch;
    }

    assert.equal(requests.length, 2);
    const command = JSON.parse(requests[1].options.body);
    assert.equal(command.name, "streamping-role");
    assert.equal(command.options[0].type, 8);
    assert.equal(command.default_member_permissions, "536870912");
    assert.equal(env.SESSIONS.entries.get("discord-command:streamping-role:v1"), "ready");
  });

test("invalid poll tokens are rejected without a KV read", async () => {
  const sessions = new MemoryKv();
  const env = environment({ SESSIONS: sessions });
  const request = new Request(
    "https://worker.example/v1/discord/sessions/abcdefghijklmnopqrstuvwx",
    { headers: { authorization: "Bearer invalid" } },
  );

  const response = await worker.fetch(request, env, context);
  assert.equal(response.status, 403);
  assert.equal(sessions.reads, 0);
});

test("completed OAuth states cannot be replayed", async () => {
  const state = "a".repeat(43);
  const sessionId = "b".repeat(24);
  const sessions = new MemoryKv({
    [`state:${state}`]: sessionId,
    [`session:${sessionId}`]: JSON.stringify({ status: "complete", state }),
  });
  const env = environment({ SESSIONS: sessions });
  const request = new Request(
    `https://worker.example/v1/discord/callback?state=${state}&code=test-code`,
    { headers: { "cf-connecting-ip": "192.0.2.10" } },
  );

  const response = await worker.fetch(request, env, context);
  assert.equal(response.status, 400);
  assert.match(await response.text(), /expired/);
});

test("OAuth completes without waiting for a role", { concurrency: false }, async () => {
  const state = "d".repeat(43);
  const sessionId = "e".repeat(24);
  const sessions = new MemoryKv({
    [`state:${state}`]: sessionId,
    [`session:${sessionId}`]: JSON.stringify({
      status: "pending",
      state,
      pollTokenHash: "hash",
    }),
  });
  const env = environment({ SESSIONS: sessions });
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => Response.json({
    webhook: {
      id: "123456789012345678",
      token: "webhook-token-long-enough-for-discord",
      guild_id: "223456789012345678",
      channel_id: "323456789012345678",
    },
  });

  try {
    const response = await worker.fetch(new Request(
      `https://worker.example/v1/discord/callback?state=${state}&code=test-code`,
      { headers: { "cf-connecting-ip": "192.0.2.10" } },
    ), env, context);
    assert.equal(response.status, 200);
  } finally {
    globalThis.fetch = originalFetch;
  }

  const stored = JSON.parse(sessions.entries.get(`session:${sessionId}`));
  assert.equal(stored.status, "complete");
  assert.equal(stored.roleId, undefined);
  assert.equal(sessions.entries.has("role-target:223456789012345678:323456789012345678"), false);
});

test("role selection starts separately for a connected webhook", { concurrency: false },
  async () => {
    const guildId = "123456789012345678";
    const channelId = "223456789012345678";
    const webhookUrl =
      "https://discord.com/api/webhooks/323456789012345678/webhook-token-long-enough";
    const sessions = new MemoryKv();
    const env = environment({ SESSIONS: sessions });
    const originalFetch = globalThis.fetch;
    globalThis.fetch = async () => Response.json({ guild_id: guildId, channel_id: channelId });

    let response;
    try {
      response = await worker.fetch(new Request(
        "https://worker.example/v1/discord/role-sessions",
        {
          method: "POST",
          headers: { "content-type": "application/json", "cf-connecting-ip": "192.0.2.10" },
          body: JSON.stringify({ webhookUrl }),
        },
      ), env, context);
    } finally {
      globalThis.fetch = originalFetch;
    }

    assert.equal(response.status, 201);
    const result = await response.json();
    const stored = JSON.parse(sessions.entries.get(`session:${result.sessionId}`));
    assert.equal(stored.status, "awaiting_role");
    assert.equal(stored.webhookUrl, webhookUrl);
    assert.equal(sessions.entries.get(`role-target:${guildId}:${channelId}`), result.sessionId);
  });

test("a signed role command completes the matching connection", async () => {
  const guildId = "123456789012345678";
  const channelId = "223456789012345678";
  const roleId = "323456789012345678";
  const sessionId = "c".repeat(24);
  const sessions = new MemoryKv({
    [`role-target:${guildId}:${channelId}`]: sessionId,
    [`session:${sessionId}`]: JSON.stringify({
      status: "awaiting_role",
      guildId,
      channelId,
      webhookUrl: "https://discord.com/api/webhooks/123/token",
      channelName: `채널 ${channelId}`,
    }),
  });
  const keyPair = await webcrypto.subtle.generateKey("Ed25519", true, ["sign", "verify"]);
  const publicKey = await webcrypto.subtle.exportKey("raw", keyPair.publicKey);
  const env = environment({ SESSIONS: sessions, DISCORD_PUBLIC_KEY: hex(publicKey) });
  const request = await interactionRequest({
    type: 2,
    guild_id: guildId,
    channel_id: channelId,
    member: { permissions: "536870912" },
    data: {
      name: "streamping-role",
      options: [{ name: "role", type: 8, value: roleId }],
      resolved: { roles: { [roleId]: { id: roleId, name: "방송 알림", mentionable: true } } },
    },
  }, keyPair);

  const response = await worker.fetch(request, env, context);
  assert.equal(response.status, 200);
  const stored = JSON.parse(sessions.entries.get(`session:${sessionId}`));
  assert.equal(stored.status, "complete");
  assert.equal(stored.roleId, roleId);
  assert.equal(stored.roleName, "방송 알림");
  assert.equal(sessions.entries.has(`role-target:${guildId}:${channelId}`), false);
});

test("role commands with invalid signatures are rejected", async () => {
  const response = await worker.fetch(new Request(
    "https://worker.example/v1/discord/interactions",
    { method: "POST", body: "{}" },
  ), environment(), context);
  assert.equal(response.status, 401);
});
