import assert from "node:assert/strict";
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
  return {
    DISCORD_CLIENT_ID: "1234567890",
    DISCORD_CLIENT_SECRET: "test-only-secret",
    PUBLIC_BASE_URL: "https://worker.example",
    SESSIONS: new MemoryKv(),
    SESSION_CREATE_GLOBAL: limiter(),
    SESSION_CREATE_CLIENT: limiter(),
    SESSION_API_CLIENT: limiter(),
    ...overrides,
  };
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

  const stored = JSON.parse(sessions.entries.get(`session:${body.sessionId}`));
  assert.equal(stored.pollToken, undefined);
  assert.match(stored.pollTokenHash, /^[0-9a-f]{64}$/);
  assert.equal(sessions.writes, 2);
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
