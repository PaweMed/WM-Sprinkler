const path = require("path");
const fs = require("fs");
const crypto = require("crypto");
const express = require("express");
const cookieParser = require("cookie-parser");
const bcrypt = require("bcryptjs");
const jwt = require("jsonwebtoken");
const mqtt = require("mqtt");
const { execFile } = require("child_process");
let firebaseAdmin = null;
try {
  firebaseAdmin = require("firebase-admin");
} catch {
  firebaseAdmin = null;
}
// PM2 can keep stale env vars across restarts; force .env to take precedence.
require("dotenv").config({ path: path.join(__dirname, "..", ".env"), override: true });

const PORT = Number(process.env.PORT || 8080);
const JWT_SECRET = process.env.JWT_SECRET || "change_me";
const MQTT_URL = process.env.MQTT_URL || "mqtt://127.0.0.1:1883";
const MQTT_USERNAME = process.env.MQTT_USERNAME || "";
const MQTT_PASSWORD = process.env.MQTT_PASSWORD || "";
const MQTT_TOPIC_PREFIX = process.env.MQTT_TOPIC_PREFIX || "wms";
const DEVICE_ONLINE_TTL_MS = Number(process.env.DEVICE_ONLINE_TTL_MS || 90000);
const COMPATIBLE_DEVICE_ONLINE_TTL_MS = Number(process.env.COMPATIBLE_DEVICE_ONLINE_TTL_MS || 20000);
const VIRTUAL_PLUG_ZONE_BASE = 1_000_000;
const VIRTUAL_PLUG_PROGRAM_BASE = 200_000_000;
const VIRTUAL_PLUG_PROGRAM_SLOT = 100;
const VIRTUAL_PLUG_CODE_MIN = 100_000;
const VIRTUAL_PLUG_CODE_SPAN = 900_000;
const CMD_ACK_TIMEOUT_MS = Number(process.env.CMD_ACK_TIMEOUT_MS || 4000);
const CMD_FAST_ACK_WAIT_MS = Math.max(120, Number(process.env.CMD_FAST_ACK_WAIT_MS || 220));
const REQUIRE_CMD_ACK = String(process.env.REQUIRE_CMD_ACK || "true").toLowerCase() !== "false";
const ALERT_WEBHOOK_URL = process.env.ALERT_WEBHOOK_URL || "";
const MOBILE_PUSH_ENABLED = String(process.env.MOBILE_PUSH_ENABLED || "true").toLowerCase() !== "false";
const FCM_SERVICE_ACCOUNT_PATH = String(process.env.FCM_SERVICE_ACCOUNT_PATH || "").trim();
const FCM_SERVICE_ACCOUNT_JSON = String(process.env.FCM_SERVICE_ACCOUNT_JSON || "").trim();
const FCM_SERVICE_ACCOUNT_JSON_B64 = String(process.env.FCM_SERVICE_ACCOUNT_JSON_B64 || "").trim();
const FCM_ANDROID_CHANNEL_ID = String(process.env.FCM_ANDROID_CHANNEL_ID || "wms_alerts").trim() || "wms_alerts";
const FCM_DEFAULT_TTL_SEC = Math.max(30, Number(process.env.FCM_DEFAULT_TTL_SEC || 1800));
const MOBILE_PUSH_MIN_EVENT_GAP_MS = Math.max(1000, Number(process.env.MOBILE_PUSH_MIN_EVENT_GAP_MS || 8000));
const MOBILE_PUSH_MAX_TOKENS_PER_USER = Math.max(1, Number(process.env.MOBILE_PUSH_MAX_TOKENS_PER_USER || 8));
const RATE_LIMIT_LOGIN_MAX = Number(process.env.RATE_LIMIT_LOGIN_MAX || 10);
const RATE_LIMIT_LOGIN_WINDOW_MS = Number(process.env.RATE_LIMIT_LOGIN_WINDOW_MS || 15 * 60 * 1000);
const RATE_LIMIT_CLAIM_MAX = Number(process.env.RATE_LIMIT_CLAIM_MAX || 20);
const RATE_LIMIT_CLAIM_WINDOW_MS = Number(process.env.RATE_LIMIT_CLAIM_WINDOW_MS || 10 * 60 * 1000);
const SESSION_IDLE_TIMEOUT_MS = Math.max(60_000, Number(process.env.SESSION_IDLE_TIMEOUT_MS || 10 * 60 * 1000));
const SESSION_REMEMBER_MAX_AGE_MS = Math.max(
  SESSION_IDLE_TIMEOUT_MS,
  Number(process.env.SESSION_REMEMBER_MAX_AGE_MS || 30 * 24 * 60 * 60 * 1000)
);
const SESSION_IDLE_TIMEOUT_REMEMBER_MS = Math.max(
  SESSION_IDLE_TIMEOUT_MS,
  Number(process.env.SESSION_IDLE_TIMEOUT_REMEMBER_MS || SESSION_REMEMBER_MAX_AGE_MS)
);
const SESSION_TOKEN_TTL = String(process.env.SESSION_TOKEN_TTL || "30d");
const WATERING_SESSIONS_LIMIT = Math.max(5000, Number(process.env.WATERING_SESSIONS_LIMIT || 20000));
const WATERING_RETENTION_DAYS = Math.max(30, Number(process.env.WATERING_RETENTION_DAYS || 548));
const STALE_UNASSIGNED_USER_RETENTION_DAYS = Math.max(
  1,
  Number(process.env.STALE_UNASSIGNED_USER_RETENTION_DAYS || 30)
);
const STALE_UNASSIGNED_USER_RETENTION_MS = STALE_UNASSIGNED_USER_RETENTION_DAYS * 24 * 60 * 60 * 1000;
const DB_HOUSEKEEPING_INTERVAL_MS = Math.max(
  5 * 60 * 1000,
  Number(process.env.DB_HOUSEKEEPING_INTERVAL_MS || 6 * 60 * 60 * 1000)
);
const SMART_CLIMATE_ENGINE_INTERVAL_MS = Math.max(
  20_000,
  Number(process.env.SMART_CLIMATE_ENGINE_INTERVAL_MS || 60_000)
);
const SMART_CLIMATE_MAX_HISTORY = Math.max(60, Number(process.env.SMART_CLIMATE_MAX_HISTORY || 300));
const SMART_CLIMATE_DEFAULT_LAT = Number(process.env.SMART_CLIMATE_DEFAULT_LAT || 52.2297);
const SMART_CLIMATE_DEFAULT_ALTITUDE_M = Number(process.env.SMART_CLIMATE_DEFAULT_ALTITUDE_M || 120);
const SMART_CLIMATE_WEATHER_MAX_AGE_MS = Math.max(
  15 * 60 * 1000,
  Number(process.env.SMART_CLIMATE_WEATHER_MAX_AGE_MS || 3 * 60 * 60 * 1000)
);
const SMART_CLIMATE_RAIN_HISTORY_MAX_AGE_MS = Math.max(
  30 * 60 * 1000,
  Number(process.env.SMART_CLIMATE_RAIN_HISTORY_MAX_AGE_MS || 6 * 60 * 60 * 1000)
);
const RAIN_ARCHIVE_RETENTION_DAYS = Math.max(
  30,
  Number(process.env.RAIN_ARCHIVE_RETENTION_DAYS || 548)
);
const RAIN_ARCHIVE_RETENTION_SEC = RAIN_ARCHIVE_RETENTION_DAYS * 24 * 60 * 60;
const RAIN_ARCHIVE_MAX_POINTS = Math.max(
  2_000,
  Number(process.env.RAIN_ARCHIVE_MAX_POINTS || 20_000)
);
const SMART_CLIMATE_PENDING_START_MAX_AGE_MS = Math.max(
  60 * 1000,
  Number(process.env.SMART_CLIMATE_PENDING_START_MAX_AGE_MS || 10 * 60 * 1000)
);
const SMART_CLIMATE_COMMAND_PENDING_ACCEPTED_MS = Math.max(
  60 * 1000,
  Number(process.env.SMART_CLIMATE_COMMAND_PENDING_ACCEPTED_MS || 3 * 60 * 1000)
);
const SMART_CLIMATE_COMMAND_PENDING_ACK_TIMEOUT_MS = Math.max(
  60 * 1000,
  Number(process.env.SMART_CLIMATE_COMMAND_PENDING_ACK_TIMEOUT_MS || 4 * 60 * 1000)
);
const ADMIN_LOGIN = String(process.env.ADMIN_LOGIN || "admin").trim().toLowerCase();
const ADMIN_PASSWORD = String(process.env.ADMIN_PASSWORD || "9521mycode");
const PUBLIC_BASE_URL = String(process.env.PUBLIC_BASE_URL || "https://www.wmsprinkler.pl").replace(/\/+$/, "");
const HA_MQTT_HOST = String(process.env.HA_MQTT_HOST || (() => {
  try {
    return new URL(PUBLIC_BASE_URL).hostname;
  } catch {
    return "wmsprinkler.pl";
  }
})()).trim();
const HA_MQTT_PORT = Number(process.env.HA_MQTT_PORT || 8883);
const HA_MQTT_TLS = String(process.env.HA_MQTT_TLS || "true").toLowerCase() !== "false";
const HA_DISCOVERY_PREFIX = String(process.env.HA_DISCOVERY_PREFIX || "homeassistant").trim() || "homeassistant";
const HA_MQTT_USER_PREFIX = String(process.env.HA_MQTT_USER_PREFIX || "ha_").trim() || "ha_";
const MQTT_AUTH_SYNC_HOOK = String(process.env.MQTT_AUTH_SYNC_HOOK || "").trim();
const OTA_SIGN_PRIVATE_KEY_PATH = String(process.env.OTA_SIGN_PRIVATE_KEY_PATH || "").trim();
const OTA_SIGN_PRIVATE_KEY_PEM_B64 = String(process.env.OTA_SIGN_PRIVATE_KEY_PEM_B64 || "").trim();
const OTA_SIGNATURE_ALG = "ed25519";
const ADSENSE_CLIENT_ID = String(process.env.ADSENSE_CLIENT_ID || "ca-pub-4043853774720196").trim();
const ADSENSE_HEAD_SNIPPET = ADSENSE_CLIENT_ID
  ? `  <script async src="https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js?client=${encodeURIComponent(
      ADSENSE_CLIENT_ID
    )}" crossorigin="anonymous"></script>`
  : "";
// Until this timestamp devices without reported hardware are allowed in OTA campaigns.
// After this moment hardware reporting becomes mandatory again.
const OTA_REQUIRE_HARDWARE_REPORT_FROM = String(
  process.env.OTA_REQUIRE_HARDWARE_REPORT_FROM || "2026-07-01T00:00:00+02:00"
).trim();
const LEGACY_COMPAT_MODE_UNTIL = String(
  process.env.LEGACY_COMPAT_MODE_UNTIL || "2026-03-12T23:59:59+01:00"
).trim();
const ADMIN_EMAILS = String(process.env.ADMIN_EMAILS || "")
  .split(",")
  .map((x) => x.trim().toLowerCase())
  .filter(Boolean);

const APP_ROOT = path.join(__dirname, "..", "..");
const DATA_ROOT = path.join(APP_ROOT, "data");
const DB_PATH = path.join(__dirname, "..", "storage", "db.json");
const FIRMWARE_DIR = path.join(__dirname, "..", "storage", "firmware");
const LANDING_INDEX_PATH = path.join(__dirname, "..", "public", "landing", "index.html");
const CLOUD_OVERLAY_DIR = path.join(__dirname, "..", "public", "cloud-overlay");

const app = express();
app.set("trust proxy", 1);

// Simple server-sent-events channel for admin live updates.
const adminSseClients = new Set();
const mqttAuthSyncState = {
  running: false,
  lastReason: "",
  lastOkAt: null,
  lastErrorAt: null,
  lastError: "",
  lastDurationMs: 0,
};
let mqttAuthSyncPendingReason = "";
let otaSignPrivateKeyCache = null;
let otaSignPrivateKeyError = "";
let smartClimateRunPromise = null;
const smartClimateDeviceLocks = new Set();
function sseSend(res, event, dataObj) {
  try {
    res.write(`event: ${event}\n`);
    res.write(`data: ${JSON.stringify(dataObj)}\n\n`);
  } catch {
    // ignore
  }
}
function broadcastAdmin(event, dataObj) {
  for (const res of adminSseClients) {
    sseSend(res, event, dataObj);
  }
}

function formatTs(date = new Date()) {
  const y = date.getFullYear();
  const m = String(date.getMonth() + 1).padStart(2, "0");
  const d = String(date.getDate()).padStart(2, "0");
  const hh = String(date.getHours()).padStart(2, "0");
  const mm = String(date.getMinutes()).padStart(2, "0");
  const ss = String(date.getSeconds()).padStart(2, "0");
  return `${y}-${m}-${d} ${hh}:${mm}:${ss}`;
}

function logEvent(message, level = "INFO") {
  const line = `[${formatTs()}] [${level}] ${message}`;
  if (level === "ERROR") console.error(line);
  else console.log(line);
}

let mobilePushMessaging = null;
let mobilePushInitTried = false;
let mobilePushInitError = "";
const mobilePushCooldown = new Map();

function loadFcmServiceAccount() {
  if (FCM_SERVICE_ACCOUNT_JSON_B64) {
    try {
      const decoded = Buffer.from(FCM_SERVICE_ACCOUNT_JSON_B64, "base64").toString("utf8");
      return JSON.parse(decoded);
    } catch (err) {
      mobilePushInitError = `FCM_SERVICE_ACCOUNT_JSON_B64 parse error: ${String(err?.message || err)}`;
      return null;
    }
  }
  if (FCM_SERVICE_ACCOUNT_JSON) {
    try {
      return JSON.parse(FCM_SERVICE_ACCOUNT_JSON);
    } catch (err) {
      mobilePushInitError = `FCM_SERVICE_ACCOUNT_JSON parse error: ${String(err?.message || err)}`;
      return null;
    }
  }
  if (FCM_SERVICE_ACCOUNT_PATH) {
    try {
      const raw = fs.readFileSync(FCM_SERVICE_ACCOUNT_PATH, "utf8");
      return JSON.parse(raw);
    } catch (err) {
      mobilePushInitError = `FCM_SERVICE_ACCOUNT_PATH read error: ${String(err?.message || err)}`;
      return null;
    }
  }
  mobilePushInitError = "No FCM service account configured";
  return null;
}

function getMobilePushMessaging() {
  if (!MOBILE_PUSH_ENABLED) return null;
  if (mobilePushMessaging) return mobilePushMessaging;
  if (mobilePushInitTried) return null;
  mobilePushInitTried = true;

  if (!firebaseAdmin) {
    mobilePushInitError = "firebase-admin dependency missing";
    logEvent(`[PUSH] disabled: ${mobilePushInitError}`, "WARN");
    return null;
  }

  const serviceAccount = loadFcmServiceAccount();
  if (!serviceAccount) {
    logEvent(`[PUSH] disabled: ${mobilePushInitError}`, "WARN");
    return null;
  }

  try {
    if (firebaseAdmin.apps.length === 0) {
      firebaseAdmin.initializeApp({
        credential: firebaseAdmin.credential.cert(serviceAccount),
      });
    }
    mobilePushMessaging = firebaseAdmin.messaging();
    logEvent("[PUSH] Firebase Cloud Messaging initialized");
    return mobilePushMessaging;
  } catch (err) {
    mobilePushInitError = String(err?.message || err);
    logEvent(`[PUSH] init failed: ${mobilePushInitError}`, "ERROR");
    return null;
  }
}

function mobilePushAvailable() {
  return !!getMobilePushMessaging();
}

function normalizeMobilePushToken(value) {
  const token = String(value || "").trim();
  if (token.length < 16 || token.length > 4096) return "";
  return token;
}

function ensureUserMobilePushTokens(user) {
  if (!user || typeof user !== "object") return [];
  if (!Array.isArray(user.mobilePushTokens)) user.mobilePushTokens = [];
  return user.mobilePushTokens;
}

function sanitizeMobilePushEntry(src, nowIso = new Date().toISOString()) {
  const token = normalizeMobilePushToken(src?.token);
  if (!token) return null;
  return {
    token,
    platform: String(src?.platform || "android").trim().toLowerCase() || "android",
    appVersion: String(src?.appVersion || "").trim().slice(0, 64),
    appBuild: String(src?.appBuild || "").trim().slice(0, 32),
    deviceModel: String(src?.deviceModel || "").trim().slice(0, 128),
    packageName: String(src?.packageName || "").trim().slice(0, 128),
    locale: String(src?.locale || "").trim().slice(0, 32),
    createdAt: String(src?.createdAt || nowIso),
    updatedAt: String(src?.updatedAt || nowIso),
  };
}

function detachMobilePushTokenFromOtherUsers(db, token, exceptUserId) {
  let changed = false;
  for (const user of db.users || []) {
    const userId = String(user?.id || "");
    if (!userId || userId === String(exceptUserId || "")) continue;
    const arr = ensureUserMobilePushTokens(user);
    const next = arr.filter((item) => String(item?.token || "") !== token);
    if (next.length !== arr.length) {
      user.mobilePushTokens = next;
      changed = true;
    }
  }
  return changed;
}

function upsertUserMobilePushToken(db, user, payload) {
  const nowIso = new Date().toISOString();
  const entry = sanitizeMobilePushEntry(payload, nowIso);
  if (!entry) return { ok: false, error: "invalid token" };

  const tokens = ensureUserMobilePushTokens(user);
  const idx = tokens.findIndex((item) => String(item?.token || "") === entry.token);
  let changed = detachMobilePushTokenFromOtherUsers(db, entry.token, user?.id);
  if (idx >= 0) {
    tokens[idx] = {
      ...tokens[idx],
      ...entry,
      createdAt: String(tokens[idx]?.createdAt || entry.createdAt || nowIso),
      updatedAt: nowIso,
    };
    changed = true;
  } else {
    tokens.unshift(entry);
    while (tokens.length > MOBILE_PUSH_MAX_TOKENS_PER_USER) tokens.pop();
    changed = true;
  }
  user.mobilePushTokens = tokens;
  return { ok: true, changed, token: entry.token, count: tokens.length };
}

function removeUserMobilePushToken(user, token) {
  const value = normalizeMobilePushToken(token);
  if (!value) return { removed: 0, count: ensureUserMobilePushTokens(user).length };
  const tokens = ensureUserMobilePushTokens(user);
  const next = tokens.filter((item) => String(item?.token || "") !== value);
  const removed = tokens.length - next.length;
  if (removed > 0) user.mobilePushTokens = next;
  return { removed, count: next.length };
}

function sanitizePushDataPayload(payload = {}) {
  const out = {};
  for (const [key, value] of Object.entries(payload || {})) {
    if (!/^[a-zA-Z0-9_.-]{1,64}$/.test(String(key || ""))) continue;
    const str = String(value ?? "").trim();
    if (!str) continue;
    out[key] = str.slice(0, 512);
  }
  return out;
}

function isFcmInvalidTokenError(err) {
  const code = String(err?.code || "").trim().toLowerCase();
  return code.includes("registration-token-not-registered")
    || code.includes("invalid-registration-token")
    || code.includes("invalid-argument");
}

async function sendMobilePushToUser(db, userId, payload = {}) {
  const messaging = getMobilePushMessaging();
  if (!messaging) {
    return { ok: false, sent: 0, total: 0, error: mobilePushInitError || "push disabled" };
  }
  const uid = String(userId || "").trim();
  if (!uid) return { ok: false, sent: 0, total: 0, error: "missing user id" };
  const user = (db.users || []).find((u) => String(u?.id || "") === uid);
  if (!user) return { ok: false, sent: 0, total: 0, error: "user not found" };
  const entries = ensureUserMobilePushTokens(user).filter((item) => normalizeMobilePushToken(item?.token));
  if (!entries.length) return { ok: true, sent: 0, total: 0 };

  const title = String(payload.title || "WM Sprinkler").trim().slice(0, 120) || "WM Sprinkler";
  const body = String(payload.body || "").trim().slice(0, 240);
  const data = sanitizePushDataPayload({
    ...payload.data,
    title,
    body,
    channel_id: FCM_ANDROID_CHANNEL_ID,
  });

  let sent = 0;
  const invalidTokens = [];
  for (const entry of entries) {
    const token = normalizeMobilePushToken(entry.token);
    if (!token) continue;
    try {
      await messaging.send({
        token,
        data,
        android: {
          priority: "high",
          ttl: `${FCM_DEFAULT_TTL_SEC}s`,
          data,
        },
      });
      sent += 1;
    } catch (err) {
      const code = String(err?.code || "").trim();
      if (isFcmInvalidTokenError(err)) invalidTokens.push(token);
      logEvent(
        `[PUSH] send failed user=${uid} token_tail=${token.slice(-8)} code=${code || "-"} error=${String(err?.message || err)}`,
        "WARN"
      );
    }
  }

  if (invalidTokens.length > 0) {
    logEvent(
      `[PUSH] invalid tokens detected user=${uid} count=${invalidTokens.length}. ` +
      "Token cleanup nastąpi przy kolejnym rejestrowaniu urządzenia.",
      "WARN"
    );
  }

  return {
    ok: sent > 0,
    sent,
    total: entries.length,
  };
}

function shouldSendMobilePushEvent(ownerUserId, deviceId, eventKey, minGapMs = MOBILE_PUSH_MIN_EVENT_GAP_MS) {
  const owner = String(ownerUserId || "").trim();
  const did = String(deviceId || "").trim();
  const key = String(eventKey || "").trim();
  if (!owner || !did || !key) return false;
  const now = Date.now();
  const cooldownKey = `${owner}|${did}|${key}`;
  const prev = Number(mobilePushCooldown.get(cooldownKey) || 0);
  if (now - prev < Math.max(1000, Number(minGapMs) || MOBILE_PUSH_MIN_EVENT_GAP_MS)) {
    return false;
  }
  mobilePushCooldown.set(cooldownKey, now);
  return true;
}

function injectAdSenseIntoHtml(html) {
  const source = String(html || "");
  if (!ADSENSE_HEAD_SNIPPET || !source) return source;
  if (source.includes("pagead2.googlesyndication.com/pagead/js/adsbygoogle.js")) return source;
  if (source.includes("</head>")) {
    return source.replace("</head>", `${ADSENSE_HEAD_SNIPPET}\n</head>`);
  }
  return `${ADSENSE_HEAD_SNIPPET}\n${source}`;
}

function sendHtmlFile(res, filePath) {
  try {
    const html = fs.readFileSync(filePath, "utf8");
    res.type("html");
    res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate");
    res.setHeader("Pragma", "no-cache");
    res.setHeader("Expires", "0");
    return res.send(injectAdSenseIntoHtml(html));
  } catch (err) {
    logEvent(`[HTTP] failed to read HTML ${filePath}: ${String(err?.message || err)}`, "ERROR");
    return res.status(500).send("Internal Server Error");
  }
}

function injectMainAppCloudOverlay(html) {
  const source = String(html || "");
  if (!source) return source;
  const marker = "/cloud-overlay/weather-percent-overlay.js";
  if (source.includes(marker)) return source;
  const snippet = '  <script defer src="/cloud-overlay/weather-percent-overlay.js?v=20260325-3"></script>';
  if (source.includes("</body>")) {
    return source.replace("</body>", `${snippet}\n</body>`);
  }
  return `${source}\n${snippet}`;
}

function sendMainAppHtml(res, filePath) {
  try {
    const rawHtml = fs.readFileSync(filePath, "utf8");
    const html = injectMainAppCloudOverlay(rawHtml);
    res.type("html");
    res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate");
    res.setHeader("Pragma", "no-cache");
    res.setHeader("Expires", "0");
    return res.send(injectAdSenseIntoHtml(html));
  } catch (err) {
    logEvent(`[HTTP] failed to read main app HTML ${filePath}: ${String(err?.message || err)}`, "ERROR");
    return res.status(500).send("Internal Server Error");
  }
}

function clientIp(req) {
  const xff = req.headers["x-forwarded-for"];
  if (typeof xff === "string" && xff.length > 0) return xff.split(",")[0].trim();
  return req.socket?.remoteAddress || req.ip || "unknown";
}

function createRateLimiter({ windowMs, max, keyFn }) {
  const hits = new Map();
  return (req, res, next) => {
    const now = Date.now();
    const key = keyFn(req);
    const item = hits.get(key) || { count: 0, resetAt: now + windowMs };
    if (now > item.resetAt) {
      item.count = 0;
      item.resetAt = now + windowMs;
    }
    item.count += 1;
    hits.set(key, item);
    const retryAfter = Math.max(1, Math.ceil((item.resetAt - now) / 1000));
    res.setHeader("X-RateLimit-Limit", String(max));
    res.setHeader("X-RateLimit-Remaining", String(Math.max(0, max - item.count)));
    res.setHeader("X-RateLimit-Reset", String(Math.floor(item.resetAt / 1000)));
    if (item.count > max) {
      res.setHeader("Retry-After", String(retryAfter));
      return apiError(req, res, 429, "tooManyAttempts");
    }
    next();
  };
}

const loginLimiter = createRateLimiter({
  windowMs: RATE_LIMIT_LOGIN_WINDOW_MS,
  max: RATE_LIMIT_LOGIN_MAX,
  keyFn: (req) => `login:${clientIp(req)}:${String(req.body?.email || "").trim().toLowerCase()}`,
});

const claimLimiter = createRateLimiter({
  windowMs: RATE_LIMIT_CLAIM_WINDOW_MS,
  max: RATE_LIMIT_CLAIM_MAX,
  keyFn: (req) => `claim:${clientIp(req)}:${req.user?.id || "anon"}`,
});

const registerLimiter = createRateLimiter({
  windowMs: RATE_LIMIT_LOGIN_WINDOW_MS,
  max: RATE_LIMIT_LOGIN_MAX,
  keyFn: (req) => `register:${clientIp(req)}:${String(req.body?.email || "").trim().toLowerCase()}`,
});

app.use((req, res, next) => {
  res.setHeader("X-Frame-Options", "DENY");
  res.setHeader("X-Content-Type-Options", "nosniff");
  res.setHeader("Referrer-Policy", "strict-origin-when-cross-origin");
  res.setHeader("Permissions-Policy", "geolocation=(), microphone=(), camera=()");
  next();
});

app.use(express.json({ limit: "1mb" }));
app.use(cookieParser());

function cookieOptions(req, { persistent = false } = {}) {
  const isSecure = req.secure || req.headers["x-forwarded-proto"] === "https";
  const host = String(req.hostname || req.headers?.host || "").split(":")[0].toLowerCase();
  const cookieDomain = (host === "wmsprinkler.pl" || host.endsWith(".wmsprinkler.pl")) ? ".wmsprinkler.pl" : undefined;
  const opts = {
    httpOnly: true,
    sameSite: "lax",
    secure: !!isSecure,
    path: "/",
    domain: cookieDomain,
  };
  if (persistent) opts.maxAge = SESSION_REMEMBER_MAX_AGE_MS;
  return opts;
}

const UI_SUPPORTED_LANGUAGES = ["pl", "en", "de", "es"];
const UI_TEXTS = {
  tooManyAttempts: {
    pl: "Za dużo prób. Spróbuj ponownie za chwilę.",
    en: "Too many attempts. Please try again shortly.",
    de: "Zu viele Versuche. Bitte versuche es in Kürze erneut.",
    es: "Demasiados intentos. Inténtalo de nuevo en un momento.",
  },
  noActiveSession: {
    pl: "Brak aktywnej sesji",
    en: "No active session",
    de: "Keine aktive Sitzung",
    es: "No hay una sesión activa",
  },
  invalidSessionActivity: {
    pl: "Nieprawidłowa aktywność sesji",
    en: "Invalid session activity",
    de: "Ungültige Sitzungsaktivität",
    es: "Actividad de sesión no válida",
  },
  sessionMissing: {
    pl: "Brak sesji",
    en: "No active session",
    de: "Keine aktive Sitzung",
    es: "No hay una sesión activa",
  },
  sessionIdleExpired: {
    pl: "Sesja wygasła po bezczynności",
    en: "Session expired due to inactivity",
    de: "Die Sitzung ist wegen Inaktivität abgelaufen",
    es: "La sesión expiró por inactividad",
  },
  userMissing: {
    pl: "Brak użytkownika",
    en: "User not found",
    de: "Benutzer nicht gefunden",
    es: "Usuario no encontrado",
  },
  invalidSession: {
    pl: "Nieprawidłowa sesja",
    en: "Invalid session",
    de: "Ungültige Sitzung",
    es: "Sesión no válida",
  },
  adminRequired: {
    pl: "Brak uprawnień administratora",
    en: "Administrator access required",
    de: "Administratorrechte erforderlich",
    es: "Se requieren permisos de administrador",
  },
  emailPasswordRequired: {
    pl: "email i hasło wymagane",
    en: "Email and password are required",
    de: "E-Mail und Passwort sind erforderlich",
    es: "Se requieren correo y contraseña",
  },
  userAlreadyExists: {
    pl: "Użytkownik już istnieje",
    en: "User already exists",
    de: "Benutzer existiert bereits",
    es: "El usuario ya existe",
  },
  invalidLogin: {
    pl: "Nieprawidłowy login",
    en: "Invalid login",
    de: "Ungültige Anmeldung",
    es: "Inicio de sesión no válido",
  },
  noAssignedDevice: {
    pl: "Brak przypisanego urządzenia",
    en: "No assigned device",
    de: "Kein Gerät zugewiesen",
    es: "No hay un dispositivo asignado",
  },
  deviceOfflineNoLiveData: {
    pl: "Urządzenie offline. Brak danych live.",
    en: "Device is offline. No live data available.",
    de: "Das Gerät ist offline. Keine Live-Daten verfügbar.",
    es: "El dispositivo está offline. No hay datos en vivo disponibles.",
  },
  invalidDeviceId: {
    pl: "Nieprawidłowy device_id",
    en: "Invalid device_id",
    de: "Ungültige device_id",
    es: "device_id no válido",
  },
  deviceReportsDifferentId: {
    pl: "Urządzenie zgłasza inne ID",
    en: "The device reports a different ID",
    de: "Das Gerät meldet eine andere ID",
    es: "El dispositivo informa un ID diferente",
  },
  invalidClaimCode: {
    pl: "Nieprawidłowy claim_code",
    en: "Invalid claim_code",
    de: "Ungültiger claim_code",
    es: "claim_code no válido",
  },
  deviceAssignedToAnotherAccount: {
    pl: "Urządzenie przypisane do innego konta",
    en: "The device is assigned to another account",
    de: "Das Gerät ist einem anderen Konto zugewiesen",
    es: "El dispositivo está asignado a otra cuenta",
  },
  accountAlreadyHasDevice: {
    pl: "To konto ma już przypisane urządzenie. Odłącz stare, aby przypisać nowe.",
    en: "This account already has an assigned device. Disconnect the current one before assigning a new one.",
    de: "Diesem Konto ist bereits ein Gerät zugewiesen. Trenne zuerst das aktuelle Gerät, bevor du ein neues zuweist.",
    es: "Esta cuenta ya tiene un dispositivo asignado. Desconecta el actual antes de asignar uno nuevo.",
  },
  deviceNotAssignedToAccount: {
    pl: "To urządzenie nie jest przypisane do konta",
    en: "This device is not assigned to your account",
    de: "Dieses Gerät ist deinem Konto nicht zugewiesen",
    es: "Este dispositivo no está asignado a tu cuenta",
  },
  noAssignedDeviceToUnclaim: {
    pl: "Brak przypisanego urządzenia do odłączenia",
    en: "There is no assigned device to disconnect",
    de: "Es gibt kein zugewiesenes Gerät zum Trennen",
    es: "No hay un dispositivo asignado para desconectar",
  },
  invalidData: {
    pl: "Nieprawidłowe dane",
    en: "Invalid data",
    de: "Ungültige Daten",
    es: "Datos no válidos",
  },
  namesArrayRequired: {
    pl: "Brak tablicy names",
    en: "Missing names array",
    de: "Names-Array fehlt",
    es: "Falta el array names",
  },
  programArrayExpected: {
    pl: "Oczekiwano tablicy programów",
    en: "Expected an array of programs",
    de: "Es wurde ein Programm-Array erwartet",
    es: "Se esperaba un array de programas",
  },
  haMqttDataPrepareFailed: {
    pl: "Nie udało się przygotować danych MQTT dla Home Assistant",
    en: "Failed to prepare MQTT data for Home Assistant",
    de: "MQTT-Daten für Home Assistant konnten nicht vorbereitet werden",
    es: "No se pudieron preparar los datos MQTT para Home Assistant",
  },
  haMqttRotateFailed: {
    pl: "Nie udało się obrócić hasła MQTT dla Home Assistant",
    en: "Failed to rotate the MQTT password for Home Assistant",
    de: "Das MQTT-Passwort für Home Assistant konnte nicht rotiert werden",
    es: "No se pudo rotar la contraseña MQTT para Home Assistant",
  },
  cloudMqttOffline: {
    pl: "Broker MQTT cloud jest offline",
    en: "Cloud MQTT broker is offline",
    de: "Der Cloud-MQTT-Broker ist offline",
    es: "El broker MQTT de la nube está offline",
  },
  rediscoveryPublishFailed: {
    pl: "Nie udało się opublikować sygnału rediscovery",
    en: "Failed to publish the rediscovery signal",
    de: "Das Rediscovery-Signal konnte nicht veröffentlicht werden",
    es: "No se pudo publicar la señal de rediscovery",
  },
  haMqttTestPrepareFailed: {
    pl: "Nie udało się przygotować danych MQTT dla testu Home Assistant",
    en: "Failed to prepare MQTT data for the Home Assistant test",
    de: "MQTT-Daten für den Home-Assistant-Test konnten nicht vorbereitet werden",
    es: "No se pudieron preparar los datos MQTT para la prueba de Home Assistant",
  },
  mqttBrokerUnavailable: {
    pl: "Broker MQTT niedostępny",
    en: "MQTT broker unavailable",
    de: "MQTT-Broker nicht verfügbar",
    es: "Broker MQTT no disponible",
  },
  commandRejectedByDevice: {
    pl: "Komenda odrzucona przez urządzenie",
    en: "Command rejected by the device",
    de: "Befehl wurde vom Gerät abgelehnt",
    es: "El dispositivo rechazó el comando",
  },
  noDeviceAck: {
    pl: "Brak ACK z urządzenia",
    en: "No ACK received from the device",
    de: "Kein ACK vom Gerät erhalten",
    es: "No se recibió ACK del dispositivo",
  },
  ackTimeout: {
    pl: "Przekroczono czas oczekiwania na ACK z urządzenia",
    en: "Timed out waiting for ACK from the device",
    de: "Zeitüberschreitung beim Warten auf ACK vom Gerät",
    es: "Se agotó el tiempo de espera del ACK del dispositivo",
  },
  ackCancelled: {
    pl: "Oczekiwanie na ACK zostało anulowane",
    en: "Waiting for ACK was cancelled",
    de: "Warten auf ACK wurde abgebrochen",
    es: "La espera del ACK fue cancelada",
  },
  mqttDisconnected: {
    pl: "Połączenie MQTT zostało przerwane",
    en: "MQTT connection was interrupted",
    de: "Die MQTT-Verbindung wurde unterbrochen",
    es: "La conexión MQTT se interrumpió",
  },
};

function normalizeUiLanguage(value) {
  const candidate = String(value || "").trim().toLowerCase().replace("_", "-");
  const base = candidate.split("-")[0];
  return UI_SUPPORTED_LANGUAGES.includes(base) ? base : "";
}

function uiLanguageFromReq(req) {
  const cookieLang = normalizeUiLanguage(req?.cookies?.wms_lang);
  if (cookieLang) return cookieLang;
  const raw = String(req?.headers?.["accept-language"] || "").trim().toLowerCase();
  if (raw) {
    for (const chunk of raw.split(",")) {
      const lang = normalizeUiLanguage(String(chunk || "").split(";")[0]);
      if (lang) return lang;
    }
  }
  return "en";
}

function interpolateUiText(template, vars = {}) {
  return String(template || "").replace(/\{(\w+)\}/g, (_m, key) => String(vars[key] ?? ""));
}

function uiText(req, key, vars = {}) {
  const lang = uiLanguageFromReq(req);
  const row = UI_TEXTS[key];
  if (!row || typeof row !== "object") return String(key || "");
  const template = row[lang] || row.en || row.pl || key;
  return interpolateUiText(template, vars);
}

function localizeKnownApiMessage(req, rawMessage) {
  const message = String(rawMessage || "");
  const exactMap = {
    "Brak aktywnej sesji": "noActiveSession",
    "Nieprawidłowa aktywność sesji": "invalidSessionActivity",
    "Brak sesji": "sessionMissing",
    "Sesja wygasła po bezczynności": "sessionIdleExpired",
    "Brak użytkownika": "userMissing",
    "Nieprawidłowa sesja": "invalidSession",
    "Brak uprawnień administratora": "adminRequired",
    "email i hasło wymagane": "emailPasswordRequired",
    "Użytkownik już istnieje": "userAlreadyExists",
    "Nieprawidłowy login": "invalidLogin",
    "Brak przypisanego urządzenia": "noAssignedDevice",
    "Urządzenie offline. Brak danych live.": "deviceOfflineNoLiveData",
    "Nieprawidłowy device_id": "invalidDeviceId",
    "Urządzenie zgłasza inne ID": "deviceReportsDifferentId",
    "Nieprawidłowy claim_code": "invalidClaimCode",
    "Urządzenie przypisane do innego konta": "deviceAssignedToAnotherAccount",
    "To konto ma już przypisane urządzenie. Odłącz stare, aby przypisać nowe.": "accountAlreadyHasDevice",
    "To urządzenie nie jest przypisane do konta": "deviceNotAssignedToAccount",
    "Brak przypisanego urządzenia do odłączenia": "noAssignedDeviceToUnclaim",
    "Nieprawidłowe dane": "invalidData",
    "Brak tablicy names": "namesArrayRequired",
    "Oczekiwano tablicy programów": "programArrayExpected",
    "Nie udało się przygotować danych MQTT dla Home Assistant": "haMqttDataPrepareFailed",
    "Nie udało się obrócić hasła MQTT dla Home Assistant": "haMqttRotateFailed",
    "Broker MQTT cloud jest offline": "cloudMqttOffline",
    "Nie udało się opublikować sygnału rediscovery": "rediscoveryPublishFailed",
    "Nie udało się przygotować danych MQTT dla testu Home Assistant": "haMqttTestPrepareFailed",
    "Broker MQTT niedostępny": "mqttBrokerUnavailable",
    "Komenda odrzucona przez urządzenie": "commandRejectedByDevice",
    "Brak ACK z urządzenia": "noDeviceAck",
    "MQTT disconnected": "mqttDisconnected",
  };
  if (exactMap[message]) return uiText(req, exactMap[message]);
  if (/^ACK timeout$/i.test(message)) return uiText(req, "ackTimeout");
  if (/^ACK cancelled\b/i.test(message)) return uiText(req, "ackCancelled");
  return message;
}

function apiError(req, res, status, keyOrMessage, vars = {}) {
  const key = String(keyOrMessage || "");
  const error = UI_TEXTS[key] ? uiText(req, key, vars) : localizeKnownApiMessage(req, key);
  return res.status(status).json({ ok: false, error });
}

function sendCommandError(req, res, err) {
  const raw = String(err?.message || "Brak ACK z urządzenia");
  const status = /\btimeout\b/i.test(raw) ? 504 : 503;
  return apiError(req, res, status, raw);
}

function ensureDb() {
  if (!fs.existsSync(path.dirname(DB_PATH))) {
    fs.mkdirSync(path.dirname(DB_PATH), { recursive: true });
  }
  if (!fs.existsSync(FIRMWARE_DIR)) {
    fs.mkdirSync(FIRMWARE_DIR, { recursive: true });
  }
  if (!fs.existsSync(DB_PATH)) {
    const seed = {
      users: [],
      devices: {},
      deviceStates: {},
      firmwares: [],
      otaCampaigns: [],
      wateringSessions: [],
      wateringOpenSessions: {},
      wateringPendingStarts: {},
    };
    fs.writeFileSync(DB_PATH, JSON.stringify(seed, null, 2));
    return;
  }

  // lekkie migracje schematu
  const db = JSON.parse(fs.readFileSync(DB_PATH, "utf8"));
  let changed = false;
  if (!Array.isArray(db.users)) {
    db.users = [];
    changed = true;
  }
  if (!db.devices || typeof db.devices !== "object") {
    db.devices = {};
    changed = true;
  }
  if (!db.deviceStates || typeof db.deviceStates !== "object") {
    db.deviceStates = {};
    changed = true;
  }
  if (!Array.isArray(db.firmwares)) {
    db.firmwares = [];
    changed = true;
  }
  if (!Array.isArray(db.otaCampaigns)) {
    db.otaCampaigns = [];
    changed = true;
  }
  if (!Array.isArray(db.wateringSessions)) {
    db.wateringSessions = [];
    changed = true;
  }
  if (!db.wateringOpenSessions || typeof db.wateringOpenSessions !== "object" || Array.isArray(db.wateringOpenSessions)) {
    db.wateringOpenSessions = {};
    changed = true;
  }
  if (!db.wateringPendingStarts || typeof db.wateringPendingStarts !== "object" || Array.isArray(db.wateringPendingStarts)) {
    db.wateringPendingStarts = {};
    changed = true;
  }
  for (const fw of db.firmwares) {
    if (!fw || typeof fw !== "object") continue;
    const hw = inferHardwareFromFirmwareMeta(fw);
    if (hw && String(fw.hardware || "") !== hw) {
      fw.hardware = hw;
      changed = true;
    }
    const target = sanitizeOtaTarget(fw.target);
    if (String(fw.target || "") !== target) {
      fw.target = target;
      changed = true;
    }
    const sigAlg = String(fw.signature_alg || "").trim().toLowerCase();
    if (fw.signature_alg && fw.signature_alg !== sigAlg) {
      fw.signature_alg = sigAlg;
      changed = true;
    }
    if (typeof fw.signature === "string") {
      const sig = fw.signature.trim().toLowerCase();
      if (fw.signature !== sig) {
        fw.signature = sig;
        changed = true;
      }
    }
  }
  for (const user of db.users) {
    if (!Array.isArray(user.deviceIds)) {
      user.deviceIds = [];
      changed = true;
    }
    if (!Array.isArray(user.mobilePushTokens)) {
      user.mobilePushTokens = [];
      changed = true;
    } else {
      const normalizedTokens = [];
      for (const entry of user.mobilePushTokens) {
        if (!entry || typeof entry !== "object") {
          changed = true;
          continue;
        }
        const token = String(entry.token || "").trim();
        if (token.length < 16) {
          changed = true;
          continue;
        }
        normalizedTokens.push({
          token,
          platform: String(entry.platform || "android").trim().toLowerCase() || "android",
          appVersion: String(entry.appVersion || "").trim(),
          appBuild: String(entry.appBuild || "").trim(),
          deviceModel: String(entry.deviceModel || "").trim(),
          packageName: String(entry.packageName || "").trim(),
          locale: String(entry.locale || "").trim(),
          createdAt: String(entry.createdAt || new Date().toISOString()),
          updatedAt: String(entry.updatedAt || new Date().toISOString()),
        });
      }
      const dedup = [];
      const seen = new Set();
      for (const item of normalizedTokens) {
        if (seen.has(item.token)) {
          changed = true;
          continue;
        }
        seen.add(item.token);
        dedup.push(item);
      }
      if (dedup.length !== user.mobilePushTokens.length
        || JSON.stringify(dedup) !== JSON.stringify(user.mobilePushTokens)) {
        user.mobilePushTokens = dedup.slice(0, MOBILE_PUSH_MAX_TOKENS_PER_USER);
        changed = true;
      } else if (dedup.length > MOBILE_PUSH_MAX_TOKENS_PER_USER) {
        user.mobilePushTokens = dedup.slice(0, MOBILE_PUSH_MAX_TOKENS_PER_USER);
        changed = true;
      }
    }
    const createdAtMs = Date.parse(String(user.createdAt || ""));
    if (Number.isFinite(createdAtMs)) {
      const createdIso = new Date(createdAtMs).toISOString();
      if (String(user.createdAt) !== createdIso) {
        user.createdAt = createdIso;
        changed = true;
      }
    } else {
      const inferredCreatedAt = inferUserCreatedAtIsoFromId(user.id);
      if (inferredCreatedAt) {
        user.createdAt = inferredCreatedAt;
        changed = true;
      }
    }
    if (user.lastLoginAt != null && String(user.lastLoginAt).trim() !== "") {
      const lastLoginMs = Date.parse(String(user.lastLoginAt));
      if (Number.isFinite(lastLoginMs)) {
        const loginIso = new Date(lastLoginMs).toISOString();
        if (String(user.lastLoginAt) !== loginIso) {
          user.lastLoginAt = loginIso;
          changed = true;
        }
      } else {
        user.lastLoginAt = null;
        changed = true;
      }
    } else if (user.lastLoginAt !== null) {
      user.lastLoginAt = null;
      changed = true;
    }
    if (!user.otaSeen || typeof user.otaSeen !== "object") {
      user.otaSeen = {};
      changed = true;
    }
    for (const deviceId of user.deviceIds) {
      if (!db.devices[deviceId]) {
        db.devices[deviceId] = {
          ownerUserId: user.id,
          claimedAt: new Date().toISOString(),
        };
        changed = true;
      }
    }
  }
  for (const [deviceId, meta] of Object.entries(db.devices || {})) {
    if (!meta || typeof meta !== "object") {
      db.devices[deviceId] = { ownerUserId: null, claimedAt: new Date().toISOString() };
      changed = true;
      continue;
    }
    const hw = normalizeHardwareId(meta.hardware);
    if (meta.hardware && meta.hardware !== hw) {
      meta.hardware = hw;
      changed = true;
    }
    if (meta.haMqtt && typeof meta.haMqtt !== "object") {
      delete meta.haMqtt;
      changed = true;
    }
  }
  const prunedUsers = pruneStaleUnassignedUsers(db);
  if (prunedUsers.removed > 0) {
    logEvent(
      `[GC] removed ${prunedUsers.removed} stale unassigned user(s) ` +
      `(retention=${STALE_UNASSIGNED_USER_RETENTION_DAYS}d)`,
      "INFO"
    );
    changed = true;
  }
  for (const [deviceId, state] of Object.entries(db.deviceStates || {})) {
    if (!state || typeof state !== "object") {
      db.deviceStates[deviceId] = {
        lastSeen: null,
        status: {},
        weather: {},
        weatherAt: null,
        zones: [],
        programs: [],
        logs: { logs: [] },
        settingsPublic: {},
        rainHistory: [],
        rainHistoryAt: null,
        rainArchive: [],
        rainArchiveAt: null,
        rainDailyTotals: {},
        rainRolling24LastMm: null,
        rainRolling24At: null,
        wateringPercent: {},
        wateringPercentAt: null,
        plugTelemetry: {},
        otaLastSuccess: null,
      };
      changed = true;
      continue;
    }
    if (!state.plugTelemetry || typeof state.plugTelemetry !== "object" || Array.isArray(state.plugTelemetry)) {
      state.plugTelemetry = {};
      changed = true;
    }
    if (!state.weatherAt || !Number.isFinite(Date.parse(String(state.weatherAt || "")))) {
      if (state.weatherAt !== null) {
        state.weatherAt = null;
        changed = true;
      }
    } else if (String(state.weatherAt) !== new Date(Date.parse(String(state.weatherAt))).toISOString()) {
      state.weatherAt = new Date(Date.parse(String(state.weatherAt))).toISOString();
      changed = true;
    }
    if (!state.rainHistoryAt || !Number.isFinite(Date.parse(String(state.rainHistoryAt || "")))) {
      if (state.rainHistoryAt !== null) {
        state.rainHistoryAt = null;
        changed = true;
      }
    } else if (String(state.rainHistoryAt) !== new Date(Date.parse(String(state.rainHistoryAt))).toISOString()) {
      state.rainHistoryAt = new Date(Date.parse(String(state.rainHistoryAt))).toISOString();
      changed = true;
    }
    const normalizedRainArchive = normalizeRainArchiveEntries([
      ...(Array.isArray(state.rainArchive) ? state.rainArchive : []),
      ...(Array.isArray(state.rainHistory) ? state.rainHistory : []),
    ]);
    if (!Array.isArray(state.rainArchive)
        || state.rainArchive.length !== normalizedRainArchive.length
        || state.rainArchive.some((item, idx) => {
          const normalized = normalizedRainArchive[idx];
          return !normalized
            || Number(item?.time) !== Number(normalized.time)
            || Math.abs(Number(item?.rain || 0) - Number(normalized.rain || 0)) > 0.0001;
        })) {
      state.rainArchive = normalizedRainArchive;
      changed = true;
    }
    if (!state.rainArchiveAt || !Number.isFinite(Date.parse(String(state.rainArchiveAt || "")))) {
      if (state.rainArchiveAt !== null) {
        state.rainArchiveAt = null;
        changed = true;
      }
    } else if (String(state.rainArchiveAt) !== new Date(Date.parse(String(state.rainArchiveAt))).toISOString()) {
      state.rainArchiveAt = new Date(Date.parse(String(state.rainArchiveAt))).toISOString();
      changed = true;
    }
    const normalizedDailyTotals = normalizeRainDailyTotalsMap(state.rainDailyTotals);
    if (JSON.stringify(normalizedDailyTotals) !== JSON.stringify(state.rainDailyTotals || {})) {
      state.rainDailyTotals = normalizedDailyTotals;
      changed = true;
    }
    if (state.rainRolling24LastMm != null) {
      const rolling = safeNumber(state.rainRolling24LastMm, NaN);
      if (Number.isFinite(rolling)) {
        const rounded = Math.round(Math.max(0, rolling) * 100) / 100;
        if (Number(state.rainRolling24LastMm) !== rounded) {
          state.rainRolling24LastMm = rounded;
          changed = true;
        }
      } else {
        state.rainRolling24LastMm = null;
        changed = true;
      }
    }
    if (!state.rainRolling24At || !Number.isFinite(Date.parse(String(state.rainRolling24At || "")))) {
      if (state.rainRolling24At !== null) {
        state.rainRolling24At = null;
        changed = true;
      }
    } else if (String(state.rainRolling24At) !== new Date(Date.parse(String(state.rainRolling24At))).toISOString()) {
      state.rainRolling24At = new Date(Date.parse(String(state.rainRolling24At))).toISOString();
      changed = true;
    }
    if (!state.wateringPercentAt || !Number.isFinite(Date.parse(String(state.wateringPercentAt || "")))) {
      if (state.wateringPercentAt !== null) {
        state.wateringPercentAt = null;
        changed = true;
      }
    } else if (String(state.wateringPercentAt) !== new Date(Date.parse(String(state.wateringPercentAt))).toISOString()) {
      state.wateringPercentAt = new Date(Date.parse(String(state.wateringPercentAt))).toISOString();
      changed = true;
    }
    if (state.otaLastSuccess && typeof state.otaLastSuccess === "object") {
      const stageNorm = normalizeOtaStage(state.otaLastSuccess.stage);
      if (state.otaLastSuccess.stage && state.otaLastSuccess.stage !== stageNorm) {
        state.otaLastSuccess.stage = stageNorm;
        changed = true;
      }
      if (isInterimOtaDetail(state.otaLastSuccess.detail) && !isOtaFinalSuccessStage(stageNorm)) {
        state.otaLastSuccess = null;
        changed = true;
      }
    } else if (state.otaLastSuccess != null) {
      state.otaLastSuccess = null;
      changed = true;
    }
  }
  const sessionsBefore = db.wateringSessions.length;
  db.wateringSessions = db.wateringSessions
    .filter((item) => item && typeof item === "object")
    .map((item) => {
      const startedAt = String(item.startedAt || "").trim();
      const endedAt = String(item.endedAt || "").trim();
      const deviceId = String(item.deviceId || "").trim();
      const startMs = Date.parse(startedAt);
      const endMs = Date.parse(endedAt);
      if (!deviceId || !Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs) return null;
      const durationSec = Math.max(1, Math.round((endMs - startMs) / 1000));
      return {
        id: String(item.id || `ws_${endMs}_${Math.floor(Math.random() * 100000)}`),
        deviceId,
        startedAt: new Date(startMs).toISOString(),
        endedAt: new Date(endMs).toISOString(),
        durationSec,
        maxActiveZones: Number.isFinite(Number(item.maxActiveZones)) ? Math.max(1, Number(item.maxActiveZones)) : 1,
        startedBy: String(item.startedBy || "zones"),
        activeZoneIds: Array.isArray(item.activeZoneIds)
          ? item.activeZoneIds.map((v) => Number(v)).filter((v) => Number.isInteger(v) && v >= 0).slice(0, 16)
          : [],
        smartClimateZoneId: Number.isInteger(Number(item.smartClimateZoneId)) && Number(item.smartClimateZoneId) >= 0
          ? Number(item.smartClimateZoneId)
          : null,
        requestedSec: Math.max(0, Number(item.requestedSec || 0) || 0),
        commandId: String(item.commandId || ""),
        ackStatus: String(item.ackStatus || ""),
        closedBy: String(item.closedBy || "zones"),
      };
    })
    .filter(Boolean);
  if (db.wateringSessions.length !== sessionsBefore) changed = true;
  const openSessions = {};
  for (const [deviceId, open] of Object.entries(db.wateringOpenSessions || {})) {
    if (!open || typeof open !== "object") {
      changed = true;
      continue;
    }
    const startedAt = String(open.startedAt || "").trim();
    const startMs = Date.parse(startedAt);
    if (!deviceId || !Number.isFinite(startMs)) {
      changed = true;
      continue;
    }
    openSessions[deviceId] = {
      startedAt: new Date(startMs).toISOString(),
      startedBy: String(open.startedBy || "zones"),
      maxActiveZones: Number.isFinite(Number(open.maxActiveZones)) ? Math.max(1, Number(open.maxActiveZones)) : 1,
      activeZoneIds: Array.isArray(open.activeZoneIds)
        ? open.activeZoneIds.map((v) => Number(v)).filter((v) => Number.isInteger(v) && v >= 0).slice(0, 16)
        : [],
      smartClimateZoneId: Number.isInteger(Number(open.smartClimateZoneId)) && Number(open.smartClimateZoneId) >= 0
        ? Number(open.smartClimateZoneId)
        : null,
      requestedSec: Math.max(0, Number(open.requestedSec || 0) || 0),
      commandId: String(open.commandId || ""),
      ackStatus: String(open.ackStatus || ""),
    };
  }
  if (JSON.stringify(db.wateringOpenSessions || {}) !== JSON.stringify(openSessions)) {
    db.wateringOpenSessions = openSessions;
    changed = true;
  }
  const pendingStarts = {};
  for (const [deviceId, pending] of Object.entries(db.wateringPendingStarts || {})) {
    if (!pending || typeof pending !== "object") {
      changed = true;
      continue;
    }
    const createdAt = String(pending.createdAt || "").trim();
    const createdMs = Date.parse(createdAt);
    const zoneId = Number(pending.zoneId);
    if (!deviceId || !Number.isFinite(createdMs) || !Number.isInteger(zoneId) || zoneId < 0) {
      changed = true;
      continue;
    }
    if (Date.now() - createdMs > SMART_CLIMATE_PENDING_START_MAX_AGE_MS) {
      changed = true;
      continue;
    }
    pendingStarts[deviceId] = {
      createdAt: new Date(createdMs).toISOString(),
      source: String(pending.source || "smart_climate"),
      zoneId,
      requestedSec: Math.max(0, Number(pending.requestedSec || 0) || 0),
      commandId: String(pending.commandId || ""),
      ackStatus: String(pending.ackStatus || ""),
    };
  }
  if (JSON.stringify(db.wateringPendingStarts || {}) !== JSON.stringify(pendingStarts)) {
    db.wateringPendingStarts = pendingStarts;
    changed = true;
  }
  const beforePruneLen = db.wateringSessions.length;
  pruneWateringSessions(db);
  if (db.wateringSessions.length !== beforePruneLen) changed = true;
  if (changed) fs.writeFileSync(DB_PATH, JSON.stringify(db, null, 2));
}

function loadDb() {
  ensureDb();
  return JSON.parse(fs.readFileSync(DB_PATH, "utf8"));
}

function saveDb(db) {
  fs.writeFileSync(DB_PATH, JSON.stringify(db, null, 2));
}

function baseTopic(deviceId) {
  return `${MQTT_TOPIC_PREFIX}/${deviceId}`;
}

function makeToken(userId) {
  return jwt.sign({ userId }, JWT_SECRET, { expiresIn: SESSION_TOKEN_TTL });
}

function makeAdminToken(login) {
  return jwt.sign({ userId: "__admin__", role: "admin", login }, JWT_SECRET, { expiresIn: SESSION_TOKEN_TTL });
}

function makeActivityToken(sessionUserId) {
  return jwt.sign({ userId: sessionUserId, ts: Date.now() }, JWT_SECRET, { expiresIn: SESSION_TOKEN_TTL });
}

function clearSessionCookies(req, res) {
  const opts = cookieOptions(req);
  const persistentOpts = cookieOptions(req, { persistent: true });
  for (const name of ["wms_token", "wms_activity", "wms_remember"]) {
    res.clearCookie(name, opts);
    res.clearCookie(name, persistentOpts);
  }
}

function parseRememberFlag(value) {
  if (value === true || value === 1) return true;
  const raw = String(value || "").trim().toLowerCase();
  return raw === "1" || raw === "true" || raw === "yes" || raw === "on";
}

function resolveRememberPreference(body, defaultValue = true) {
  const src = body && typeof body === "object" ? body : {};
  for (const key of ["remember", "rememberMe", "remember_me", "keepLoggedIn", "keep_logged_in"]) {
    if (Object.prototype.hasOwnProperty.call(src, key)) {
      return parseRememberFlag(src[key]);
    }
  }
  return !!defaultValue;
}

function isRememberedSession(req) {
  return parseRememberFlag(req?.cookies?.wms_remember);
}

function idleTimeoutMsForRequest(req) {
  return isRememberedSession(req) ? SESSION_IDLE_TIMEOUT_REMEMBER_MS : SESSION_IDLE_TIMEOUT_MS;
}

function setSessionCookies(req, res, token, sessionUserId, remember = false) {
  const persistent = !!remember;
  const opts = cookieOptions(req, { persistent });
  res.cookie("wms_token", token, opts);
  res.cookie("wms_activity", makeActivityToken(sessionUserId), opts);
  if (persistent) {
    res.cookie("wms_remember", "1", opts);
  } else {
    res.clearCookie("wms_remember", cookieOptions(req));
    res.clearCookie("wms_remember", cookieOptions(req, { persistent: true }));
  }
}

function sessionUserIdFromPayload(payload) {
  if (payload?.userId === "__admin__" || String(payload?.role || "").toLowerCase() === "admin") return "__admin__";
  return String(payload?.userId || "");
}

function readSessionActivity(req, sessionUserId) {
  const token = req.cookies?.wms_activity;
  if (!token) return { ok: false, error: uiText(req, "noActiveSession") };
  try {
    const payload = jwt.verify(token, JWT_SECRET);
    if (String(payload?.userId || "") !== String(sessionUserId || "")) {
      return { ok: false, error: uiText(req, "invalidSessionActivity") };
    }
    const ts = Number(payload?.ts || 0);
    if (!Number.isFinite(ts) || ts <= 0) return { ok: false, error: uiText(req, "invalidSessionActivity") };
    return { ok: true, ts };
  } catch {
    return { ok: false, error: uiText(req, "invalidSessionActivity") };
  }
}

function getDeviceState(db, deviceId) {
  return db.deviceStates[deviceId] || {
    lastSeen: null,
    status: {},
    weather: {},
    weatherAt: null,
    zones: [],
    programs: [],
    logs: { logs: [] },
    settingsPublic: {},
    rainHistory: [],
    rainHistoryAt: null,
    rainArchive: [],
    rainArchiveAt: null,
    rainDailyTotals: {},
    rainRolling24LastMm: null,
    rainRolling24At: null,
    wateringPercent: {},
    wateringPercentAt: null,
    plugTelemetry: {},
    otaLastSuccess: null,
  };
}

function resolveDeviceFirmwareVersion(state, meta = null) {
  if (!state || typeof state !== "object") return "";
  const status = (state.status && typeof state.status === "object") ? state.status : {};
  const settingsPublic = (state.settingsPublic && typeof state.settingsPublic === "object")
    ? state.settingsPublic
    : {};
  const metaObj = (meta && typeof meta === "object") ? meta : {};
  return String(
    status.fw_version
    || settingsPublic.fw_version
    || settingsPublic.firmware_version
    || state.lastKnownFw
    || metaObj.firmwareVersion
    || state.otaLastSuccess?.version
    || ""
  ).trim();
}

function isMissingTextValue(value) {
  const s = String(value ?? "").trim().toLowerCase();
  return s.length === 0 || s === "-" || s === "n/a" || s === "null" || s === "undefined";
}

function findFirmwareMeta(db, firmwareId, version) {
  const fwList = Array.isArray(db?.firmwares) ? db.firmwares : [];
  if (firmwareId) {
    const byId = fwList.find((f) => String(f.id) === String(firmwareId));
    if (byId) return byId;
  }
  if (version) {
    const byVer = fwList.find((f) => String(f.version) === String(version));
    if (byVer) return byVer;
  }
  return null;
}

function isDeviceOnlineWithin(state, ttlMs = DEVICE_ONLINE_TTL_MS) {
  const availability = String(state?.availability || "").trim().toLowerCase();
  if (availability === "offline" || availability === "0" || availability === "false") return false;
  if (!state?.lastSeen) return false;
  const ts = Date.parse(state.lastSeen);
  if (!Number.isFinite(ts)) return false;
  const ttl = Number.isFinite(Number(ttlMs)) ? Math.max(1000, Number(ttlMs)) : DEVICE_ONLINE_TTL_MS;
  return Date.now() - ts <= ttl;
}

function isDeviceOnline(state) {
  return isDeviceOnlineWithin(state, DEVICE_ONLINE_TTL_MS);
}

function getOwnedDeviceIds(db, userId) {
  return Object.keys(db.devices || {}).filter((id) => db.devices[id]?.ownerUserId === userId);
}

function inferUserCreatedAtIsoFromId(userId) {
  const raw = String(userId || "").trim();
  const match = /^u_(\d{10,})/.exec(raw);
  if (!match) return "";
  let ts = Number(match[1]);
  if (!Number.isFinite(ts) || ts <= 0) return "";
  if (ts < 1e12) ts *= 1000;
  const maxFutureMs = Date.now() + 7 * 24 * 60 * 60 * 1000;
  if (ts < Date.UTC(2020, 0, 1) || ts > maxFutureMs) return "";
  return new Date(ts).toISOString();
}

function userInactivityReferenceMs(user) {
  const lastLoginMs = Date.parse(String(user?.lastLoginAt || ""));
  if (Number.isFinite(lastLoginMs)) return lastLoginMs;
  const createdMs = Date.parse(String(user?.createdAt || ""));
  if (Number.isFinite(createdMs)) return createdMs;
  return NaN;
}

function pruneStaleUnassignedUsers(db, nowMs = Date.now()) {
  const users = Array.isArray(db?.users) ? db.users : [];
  if (!users.length) return { removed: 0 };

  const ownedCountByUserId = new Map();
  for (const meta of Object.values(db.devices || {})) {
    const ownerUserId = String(meta?.ownerUserId || "").trim();
    if (!ownerUserId) continue;
    ownedCountByUserId.set(ownerUserId, (ownedCountByUserId.get(ownerUserId) || 0) + 1);
  }

  let removed = 0;
  const keptUsers = [];
  for (const user of users) {
    const userId = String(user?.id || "").trim();
    if (!userId) {
      keptUsers.push(user);
      continue;
    }
    if ((ownedCountByUserId.get(userId) || 0) > 0) {
      keptUsers.push(user);
      continue;
    }

    const referenceMs = userInactivityReferenceMs(user);
    const isStale = Number.isFinite(referenceMs) && nowMs - referenceMs > STALE_UNASSIGNED_USER_RETENTION_MS;
    if (isStale) {
      removed += 1;
      continue;
    }
    keptUsers.push(user);
  }

  if (removed > 0) {
    db.users = keptUsers;
  }
  return { removed };
}

function isAdminUser(user) {
  if (!user) return false;
  if (String(user.role || "").toLowerCase() === "admin") return true;
  const email = String(user.email || "").toLowerCase();
  return !!email && ADMIN_EMAILS.includes(email);
}

function adminRequired(req, res, next) {
  if (!isAdminUser(req.user)) {
    return apiError(req, res, 403, "adminRequired");
  }
  next();
}

function zoneIsActive(zone) {
  if (zone === true) return true;
  if (!zone || typeof zone !== "object") return false;
  if (zone.active === true || zone.isActive === true || zone.on === true || zone.isOn === true || zone.running === true) {
    return true;
  }
  if (typeof zone.state === "string") {
    const s = zone.state.toLowerCase();
    if (s === "on" || s === "active" || s === "running" || s === "watering") return true;
  }
  return false;
}

function getActiveZoneIds(zones) {
  if (!Array.isArray(zones)) return [];
  const out = [];
  for (let i = 0; i < zones.length; i += 1) {
    const zone = zones[i];
    if (!zoneIsActive(zone)) continue;
    const rawId = Number(zone?.id);
    const id = Number.isInteger(rawId) && rawId >= 0 ? rawId : i;
    out.push(id);
  }
  return out;
}

function isSmartPlugDeviceState(state) {
  const status = state?.status && typeof state.status === "object" ? state.status : {};
  const kind = String(status.device_type || status.type || "").trim().toLowerCase();
  if (kind === "smart_plug" || kind === "smartplug" || kind === "plug") return true;
  const hw = normalizeHardwareId(state?.hardware || status?.hardware || status?.model || "");
  if (hw === "bwshp6") return true;
  const modelText = String(status.model || "").toLowerCase();
  return modelText.includes("shp6") || modelText.includes("blitzwolf");
}

function getPlugTelemetrySnapshot(state) {
  const status = (state?.status && typeof state.status === "object") ? state.status : {};
  const telem = (state?.plugTelemetry && typeof state.plugTelemetry === "object") ? state.plugTelemetry : {};
  const zones = Array.isArray(state?.zones) ? state.zones : [];
  const zone0 = zones[0] || {};
  const relayOn = telem.relay_on === true || zoneIsActive(zone0);
  return {
    relay_on: relayOn,
    remaining_sec: Number(telem.remaining_sec ?? zone0?.remaining ?? 0) || 0,
    power_w: Number(telem.power_w ?? status.power_w ?? 0) || 0,
    voltage_v: Number(telem.voltage_v ?? 0) || 0,
    current_a: Number(telem.current_a ?? 0) || 0,
    energy_total_kwh: Number(telem.energy_total_kwh ?? status.energy_total_kwh ?? 0) || 0,
  };
}

function isPlugRelayOn(state) {
  return getPlugTelemetrySnapshot(state).relay_on === true;
}

function resolveDeviceLabel(db, deviceId, state = null) {
  const did = String(deviceId || "").trim();
  const meta = db?.devices?.[did] || {};
  const currentState = state || getDeviceState(db, did);
  const status = (currentState?.status && typeof currentState.status === "object") ? currentState.status : {};
  const settingsPublic = (currentState?.settingsPublic && typeof currentState.settingsPublic === "object")
    ? currentState.settingsPublic
    : {};
  const candidates = [
    meta.name,
    meta.label,
    status.device_name,
    status.name,
    settingsPublic.deviceName,
    settingsPublic.device_name,
    did,
  ];
  for (const item of candidates) {
    const value = String(item || "").trim();
    if (value) return value;
  }
  return did || "Urządzenie";
}

function queueMobilePushForDeviceOwner(db, deviceId, eventKey, payload = {}, minGapMs = MOBILE_PUSH_MIN_EVENT_GAP_MS) {
  const did = String(deviceId || "").trim();
  const ownerUserId = String(db?.devices?.[did]?.ownerUserId || "").trim();
  if (!did || !ownerUserId) return;
  if (!shouldSendMobilePushEvent(ownerUserId, did, eventKey, minGapMs)) return;
  const data = {
    ...(payload?.data || {}),
    event: String(eventKey || ""),
    device_id: did,
    open_url: String(payload?.open_url || `${PUBLIC_BASE_URL}/app?device_id=${encodeURIComponent(did)}`),
  };
  sendMobilePushToUser(db, ownerUserId, {
    title: payload?.title || "WM Sprinkler",
    body: payload?.body || "",
    data,
  }).catch((err) => {
    logEvent(
      `[PUSH] dispatch failed user=${ownerUserId} device=${did} event=${eventKey} err=${String(err?.message || err)}`,
      "WARN"
    );
  });
}

function getOwnedEspControllersForPlug(db, plugDeviceId) {
  const did = String(plugDeviceId || "").trim();
  if (!did) return [];
  const ownerUserId = String(db?.devices?.[did]?.ownerUserId || "").trim();
  if (!ownerUserId) return [];
  const owned = getOwnedDeviceIds(db, ownerUserId);
  const out = [];
  for (const deviceId of owned) {
    if (deviceId === did) continue;
    const state = getDeviceState(db, deviceId);
    if (isSmartPlugDeviceState(state)) continue;
    const hw = normalizeHardwareId(
      state?.hardware
      || state?.status?.hardware
      || state?.status?.model
      || db?.devices?.[deviceId]?.hardware
    );
    if (hw !== "esp32" && hw !== "esp32c6") continue;
    out.push(deviceId);
  }
  return out;
}

async function dispatchPlugEventToControllers(db, plugDeviceId, event = {}) {
  const did = String(plugDeviceId || "").trim();
  if (!did || !mqttClient.connected) return;
  const plugState = getDeviceState(db, did);
  if (!isSmartPlugDeviceState(plugState)) return;

  const controllers = getOwnedEspControllersForPlug(db, did);
  if (!controllers.length) return;

  const telem = getPlugTelemetrySnapshot(plugState);
  const zones = Array.isArray(plugState.zones) ? plugState.zones : [];
  const zone0 = zones[0] || {};
  const meta = db?.devices?.[did] || {};
  const plugName = String(
    event.plug_name
    || zone0?.name
    || meta?.name
    || meta?.label
    || "Gniazdko"
  ).trim();

  const requestedOn = typeof event.on === "boolean" ? event.on : null;
  const relayOn = requestedOn === null ? telem.relay_on : requestedOn;
  let seconds = Number(event.seconds);
  if (!Number.isFinite(seconds) || seconds < 0) seconds = Number(telem.remaining_sec || 0);
  if (!Number.isFinite(seconds) || seconds < 0) seconds = 0;

  const payload = {
    action: relayOn ? "on" : "off",
    on: relayOn,
    seconds: Math.round(seconds),
    mode: String(event.mode || "GNIAZDKO").trim() || "GNIAZDKO",
    source: String(event.source || "plug_state").trim() || "plug_state",
    plug_device_id: did,
    plug_name: plugName,
    remaining_sec: Math.round(Number(telem.remaining_sec || 0) || 0),
    power_w: Number(telem.power_w || 0) || 0,
    voltage_v: Number(telem.voltage_v || 0) || 0,
    current_a: Number(telem.current_a || 0) || 0,
    energy_total_kwh: Number(telem.energy_total_kwh || 0) || 0,
  };

  const tasks = controllers.map(async (controllerId) => {
    try {
      await publishCommandWithFastAck(
        controllerId,
        "cmd/plug/event",
        payload,
        { allowAckTimeout: true, ackWaitMs: 450 }
      );
    } catch (err) {
      logEvent(
        `[PLUG-NOTIFY] failed controller=${controllerId} plug=${did} err=${String(err?.message || err)}`,
        "WARN"
      );
    }
  });
  await Promise.allSettled(tasks);
}

function isSmartClimateAllowedForDevice(db, deviceId, state = null) {
  const did = String(deviceId || "").trim();
  if (!did) return false;
  const currentState = state || getDeviceState(db, did);
  if (isSmartPlugDeviceState(currentState)) return false;
  const meta = db?.devices?.[did];
  const hw = normalizeHardwareId(meta?.hardware || currentState?.hardware || currentState?.status?.hardware || currentState?.status?.model);
  if (hw === "bwshp6") return false;
  if (!meta || typeof meta !== "object") return true;
  if (meta.smartClimateEnabled === false) return false;
  const access = meta.smartClimateAccess;
  if (access === false) return false;
  if (access && typeof access === "object" && access.enabled === false) return false;
  return true;
}

function smartClimateAccessErrorPayload(state) {
  if (isSmartPlugDeviceState(state)) {
    return {
      status: 400,
      body: { ok: false, error: "WM Smart Climate działa tylko dla sterownika stref." },
    };
  }
  return {
    status: 403,
    body: { ok: false, error: "WM Smart Climate jest wyłączone dla tego urządzenia." },
  };
}

const SMART_CLIMATE_NAME = "WM Smart Climate";
const SMART_CLIMATE_SUPPORTED_PROFILES = new Set(["eco", "balanced", "max_green"]);
const SMART_CLIMATE_SUPPORTED_ZONE_TYPES = new Set(["lawn", "flowerbed", "drip", "shrubs", "custom"]);
const SMART_CLIMATE_PROFILE_MULTIPLIERS = {
  eco: 0.9,
  balanced: 1.0,
  max_green: 1.15,
};
const SMART_CLIMATE_ZONE_PRESETS = {
  lawn: { precipitationMmPerHour: 12, rootDepthMm: 120, allowedDepletion: 0.45, minIntervalHours: 18, minRuntimeMin: 4, maxRuntimeMin: 48, kc: 1.0 },
  flowerbed: { precipitationMmPerHour: 10, rootDepthMm: 90, allowedDepletion: 0.4, minIntervalHours: 14, minRuntimeMin: 3, maxRuntimeMin: 40, kc: 0.85 },
  drip: { precipitationMmPerHour: 6, rootDepthMm: 160, allowedDepletion: 0.5, minIntervalHours: 24, minRuntimeMin: 6, maxRuntimeMin: 90, kc: 0.72 },
  shrubs: { precipitationMmPerHour: 7, rootDepthMm: 180, allowedDepletion: 0.55, minIntervalHours: 30, minRuntimeMin: 5, maxRuntimeMin: 80, kc: 0.65 },
  custom: { precipitationMmPerHour: 10, rootDepthMm: 130, allowedDepletion: 0.45, minIntervalHours: 20, minRuntimeMin: 4, maxRuntimeMin: 60, kc: 0.9 },
};
const SMART_CLIMATE_WINDOWS = [
  { start: "04:00", end: "06:30", score: 1.0 },
  { start: "03:00", end: "04:00", score: 0.88 },
  { start: "06:30", end: "07:30", score: 0.76 },
  { start: "23:30", end: "23:59", score: 0.42 },
];

function safeNumber(value, fallback = 0) {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}

function resolveSmartClimateMinTemp(weather, fallbackTemp = 50, { includeTomorrow = true } = {}) {
  const values = [];
  const tempNow = safeNumber(weather?.temp, NaN);
  const tempMin = safeNumber(weather?.temp_min, NaN);
  const tempMinTomorrow = safeNumber(weather?.temp_min_tomorrow, NaN);
  const tempMaxTomorrow = safeNumber(weather?.temp_max_tomorrow, NaN);

  if (Number.isFinite(tempNow)) values.push(tempNow);
  if (Number.isFinite(tempMin)) values.push(tempMin);

  if (includeTomorrow) {
    // Legacy firmware could publish 0/0 for tomorrow when forecast slot was missing.
    // Treat that exact pair as "unknown", otherwise a fake frost blocker appears all day.
    const tomorrowLooksMissing = Number.isFinite(tempMinTomorrow) && Number.isFinite(tempMaxTomorrow)
      ? (Math.abs(tempMinTomorrow) < 0.001 && Math.abs(tempMaxTomorrow) < 0.001)
      : false;
    const tomorrowRangeValid = Number.isFinite(tempMinTomorrow) && Number.isFinite(tempMaxTomorrow)
      ? tempMinTomorrow <= (tempMaxTomorrow + 0.1)
      : Number.isFinite(tempMinTomorrow);

    if (!tomorrowLooksMissing && tomorrowRangeValid && Number.isFinite(tempMinTomorrow)) {
      values.push(tempMinTomorrow);
    }
  }

  if (!values.length) return fallbackTemp;
  return Math.min(...values);
}

function clampNumber(value, minValue, maxValue) {
  const v = safeNumber(value, minValue);
  if (v < minValue) return minValue;
  if (v > maxValue) return maxValue;
  return v;
}

function normalizeSmartClimateProfile(profile) {
  const raw = String(profile || "").trim().toLowerCase();
  if (raw === "maxgreen") return "max_green";
  return SMART_CLIMATE_SUPPORTED_PROFILES.has(raw) ? raw : "balanced";
}

function normalizeSmartClimateZoneType(zoneType) {
  const raw = String(zoneType || "").trim().toLowerCase();
  return SMART_CLIMATE_SUPPORTED_ZONE_TYPES.has(raw) ? raw : "lawn";
}

function normalizeSmartClimateZoneConfig(zoneId, src = {}, previous = null) {
  const id = Number(zoneId);
  if (!Number.isInteger(id) || id < 0 || id > 63) return null;
  const prev = (previous && typeof previous === "object") ? previous : {};
  const requestedType = normalizeSmartClimateZoneType(src.zoneType || prev.zoneType);
  const preset = SMART_CLIMATE_ZONE_PRESETS[requestedType] || SMART_CLIMATE_ZONE_PRESETS.lawn;
  const out = {
    id,
    enabled: src.enabled !== false,
    zoneType: requestedType,
    precipitationMmPerHour: clampNumber(
      src.precipitationMmPerHour ?? prev.precipitationMmPerHour ?? preset.precipitationMmPerHour,
      1,
      80
    ),
    rootDepthMm: clampNumber(src.rootDepthMm ?? prev.rootDepthMm ?? preset.rootDepthMm, 40, 400),
    allowedDepletion: clampNumber(src.allowedDepletion ?? prev.allowedDepletion ?? preset.allowedDepletion, 0.2, 0.8),
    minIntervalHours: clampNumber(src.minIntervalHours ?? prev.minIntervalHours ?? preset.minIntervalHours, 2, 120),
    minRuntimeMin: clampNumber(src.minRuntimeMin ?? prev.minRuntimeMin ?? preset.minRuntimeMin, 1, 120),
    maxRuntimeMin: clampNumber(src.maxRuntimeMin ?? prev.maxRuntimeMin ?? preset.maxRuntimeMin, 2, 240),
    kc: clampNumber(src.kc ?? prev.kc ?? preset.kc, 0.2, 2.0),
    deficitMm: Math.max(0, safeNumber(src.deficitMm ?? prev.deficitMm ?? 0)),
    lastObservedRain24hMm: Math.max(0, safeNumber(src.lastObservedRain24hMm ?? prev.lastObservedRain24hMm ?? 0)),
    lastDecisionAt: String(src.lastDecisionAt || prev.lastDecisionAt || ""),
    lastRunAt: String(src.lastRunAt || prev.lastRunAt || ""),
    nextRunAt: String(src.nextRunAt || prev.nextRunAt || ""),
    lastReason: String(src.lastReason || prev.lastReason || ""),
    lastRuntimeMin: Math.max(0, Math.round(safeNumber(src.lastRuntimeMin ?? prev.lastRuntimeMin ?? 0))),
    lastEtoMm: Math.max(0, safeNumber(src.lastEtoMm ?? prev.lastEtoMm ?? 0)),
    lastEtcMm: Math.max(0, safeNumber(src.lastEtcMm ?? prev.lastEtcMm ?? 0)),
    lastEffectiveRainMm: Math.max(0, safeNumber(src.lastEffectiveRainMm ?? prev.lastEffectiveRainMm ?? 0)),
    commandPendingUntil: String(src.commandPendingUntil || prev.commandPendingUntil || ""),
  };
  if (out.maxRuntimeMin < out.minRuntimeMin) out.maxRuntimeMin = out.minRuntimeMin;
  return out;
}

function ensureSmartClimateConfig(db, deviceId, zoneCount = 0) {
  if (!db.devices || typeof db.devices !== "object") db.devices = {};
  if (!db.devices[deviceId] || typeof db.devices[deviceId] !== "object") {
    db.devices[deviceId] = { ownerUserId: null, claimedAt: new Date().toISOString() };
  }
  const meta = db.devices[deviceId];
  const base = (meta.smartClimate && typeof meta.smartClimate === "object") ? meta.smartClimate : {};
  const out = {
    name: String(base.name || SMART_CLIMATE_NAME),
    enabled: base.enabled === true,
    profile: normalizeSmartClimateProfile(base.profile),
    timezone: String(base.timezone || ""),
    latitude: clampNumber(base.latitude ?? SMART_CLIMATE_DEFAULT_LAT, -89.5, 89.5),
    altitudeM: clampNumber(base.altitudeM ?? SMART_CLIMATE_DEFAULT_ALTITUDE_M, -100, 5000),
    zones: {},
    history: Array.isArray(base.history) ? base.history.slice(-SMART_CLIMATE_MAX_HISTORY) : [],
    createdAt: String(base.createdAt || new Date().toISOString()),
    updatedAt: String(base.updatedAt || new Date().toISOString()),
    lastEvaluationAt: String(base.lastEvaluationAt || ""),
    pendingProgramSync: base.pendingProgramSync === true,
    lastProgramSyncError: String(base.lastProgramSyncError || ""),
    lastProgramSyncAt: String(base.lastProgramSyncAt || ""),
    lastEngineError: String(base.lastEngineError || ""),
  };
  const rawZones = (base.zones && typeof base.zones === "object" && !Array.isArray(base.zones)) ? base.zones : {};
  for (const [key, rawCfg] of Object.entries(rawZones)) {
    const normalized = normalizeSmartClimateZoneConfig(Number(key), rawCfg, rawCfg);
    if (!normalized) continue;
    if (zoneCount > 0 && normalized.id >= zoneCount) continue;
    out.zones[String(normalized.id)] = normalized;
  }
  meta.smartClimate = out;
  return out;
}

function getManagedSmartClimateZoneSet(db, deviceId) {
  const cfg = db?.devices?.[deviceId]?.smartClimate;
  const set = new Set();
  if (!isSmartClimateAllowedForDevice(db, deviceId)) return set;
  if (!cfg || typeof cfg !== "object" || cfg.enabled !== true) return set;
  const zones = (cfg.zones && typeof cfg.zones === "object") ? cfg.zones : {};
  for (const zoneCfg of Object.values(zones)) {
    if (!zoneCfg || typeof zoneCfg !== "object" || zoneCfg.enabled !== true) continue;
    const zoneId = Number(zoneCfg.id);
    if (Number.isInteger(zoneId) && zoneId >= 0) set.add(zoneId);
  }
  return set;
}

function isoToMs(value) {
  const ts = Date.parse(String(value || ""));
  return Number.isFinite(ts) ? ts : NaN;
}

function hasNonEmptyObject(value) {
  return !!value && typeof value === "object" && !Array.isArray(value) && Object.keys(value).length > 0;
}

function getSmartClimateDataFreshness(state, nowMs = Date.now()) {
  const weatherAtMs = isoToMs(state?.weatherAt);
  const rainHistoryAtMs = isoToMs(state?.rainHistoryAt);
  const weatherAgeMs = Number.isFinite(weatherAtMs) ? Math.max(0, nowMs - weatherAtMs) : Infinity;
  const rainAgeMs = Number.isFinite(rainHistoryAtMs) ? Math.max(0, nowMs - rainHistoryAtMs) : Infinity;
  const weatherFresh = hasNonEmptyObject(state?.weather) && weatherAgeMs <= SMART_CLIMATE_WEATHER_MAX_AGE_MS;
  const rainHistoryFresh = Array.isArray(state?.rainHistory) && rainAgeMs <= SMART_CLIMATE_RAIN_HISTORY_MAX_AGE_MS;
  return {
    weatherFresh,
    rainHistoryFresh,
    weatherAgeMs,
    rainHistoryAgeMs: rainAgeMs,
  };
}

function formatIsoLocal(date, timeZone) {
  const safeDate = date instanceof Date ? date : new Date();
  const formatter = new Intl.DateTimeFormat("en-CA", {
    timeZone: String(timeZone || "UTC"),
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hourCycle: "h23",
  });
  const parts = {};
  formatter.formatToParts(safeDate).forEach((part) => {
    if (part.type !== "literal") parts[part.type] = part.value;
  });
  return `${parts.year}-${parts.month}-${parts.day}T${parts.hour}:${parts.minute}:${parts.second}`;
}

function timeZoneOffsetMinutes(timeZone, date = new Date()) {
  const localIso = formatIsoLocal(date, timeZone);
  const asUtc = Date.parse(`${localIso}Z`);
  if (!Number.isFinite(asUtc)) return 0;
  return Math.round((asUtc - date.getTime()) / 60000);
}

function makeDateInTimeZone(timeZone, year, month, day, hour, minute) {
  const guessUtc = Date.UTC(year, month - 1, day, hour, minute, 0, 0);
  const offsetMin = timeZoneOffsetMinutes(timeZone, new Date(guessUtc));
  return new Date(guessUtc - offsetMin * 60_000);
}

function localDateParts(timeZone, date = new Date()) {
  const formatter = new Intl.DateTimeFormat("en-CA", {
    timeZone: String(timeZone || "UTC"),
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hourCycle: "h23",
  });
  const parts = {};
  formatter.formatToParts(date).forEach((part) => {
    if (part.type !== "literal") parts[part.type] = part.value;
  });
  return {
    year: Number(parts.year),
    month: Number(parts.month),
    day: Number(parts.day),
    hour: Number(parts.hour),
    minute: Number(parts.minute),
    second: Number(parts.second),
  };
}

function dayOfYearUtc(date) {
  const year = date.getUTCFullYear();
  const start = Date.UTC(year, 0, 1);
  const now = Date.UTC(year, date.getUTCMonth(), date.getUTCDate());
  return Math.floor((now - start) / 86400000) + 1;
}

function sumRainHistory(rainHistory, hours = 24) {
  if (!Array.isArray(rainHistory)) return 0;
  const nowSec = Date.now() / 1000;
  const horizonSec = Math.max(1, Number(hours) || 24) * 3600;
  let sum = 0;
  for (const item of rainHistory) {
    const ts = Number(item?.time);
    if (!Number.isFinite(ts) || ts <= 0) continue;
    if (nowSec - ts > horizonSec) continue;
    sum += Math.max(0, safeNumber(item?.rain, 0));
  }
  return sum;
}

function normalizeRainArchiveEntry(src) {
  if (!src || typeof src !== "object") return null;
  const rawTime = safeNumber(src.time ?? src.ts ?? src.timestamp, NaN);
  if (!Number.isFinite(rawTime)) return null;
  const time = rawTime > 1e12 ? Math.round(rawTime / 1000) : Math.round(rawTime);
  if (!Number.isFinite(time) || time <= 0) return null;
  const rain = Math.round(Math.max(0, safeNumber(src.rain ?? src.mm, NaN)) * 100) / 100;
  if (!Number.isFinite(rain)) return null;
  return { time, rain };
}

function normalizeRainArchiveEntries(entries, nowSec = Math.floor(Date.now() / 1000)) {
  const source = Array.isArray(entries) ? entries : [];
  const fromSec = Math.max(0, Number(nowSec || 0) - RAIN_ARCHIVE_RETENTION_SEC);
  const byTime = new Map();
  for (const item of source) {
    const normalized = normalizeRainArchiveEntry(item);
    if (!normalized) continue;
    if (normalized.time < fromSec) continue;
    const prev = byTime.get(normalized.time);
    if (!prev || normalized.rain > prev.rain) {
      byTime.set(normalized.time, normalized);
    }
  }
  const out = Array.from(byTime.values())
    .sort((a, b) => a.time - b.time);
  if (out.length > RAIN_ARCHIVE_MAX_POINTS) {
    return out.slice(-RAIN_ARCHIVE_MAX_POINTS);
  }
  return out;
}

function normalizeRainDailyTotalsMap(src, nowDate = new Date()) {
  const out = {};
  const input = (src && typeof src === "object" && !Array.isArray(src)) ? src : {};
  const cutoff = new Date(Date.UTC(nowDate.getUTCFullYear(), nowDate.getUTCMonth(), nowDate.getUTCDate()));
  cutoff.setUTCDate(cutoff.getUTCDate() - RAIN_ARCHIVE_RETENTION_DAYS);
  const cutoffKey = `${cutoff.getUTCFullYear()}-${String(cutoff.getUTCMonth() + 1).padStart(2, "0")}-${String(cutoff.getUTCDate()).padStart(2, "0")}`;
  for (const [keyRaw, valueRaw] of Object.entries(input)) {
    const key = String(keyRaw || "").trim();
    if (!/^\d{4}-\d{2}-\d{2}$/.test(key)) continue;
    if (key < cutoffKey) continue;
    const mm = Math.round(Math.max(0, safeNumber(valueRaw, 0)) * 100) / 100;
    if (mm <= 0) continue;
    out[key] = mm;
  }
  return out;
}

function resolveRolling24hFromState(state) {
  if (!state || typeof state !== "object") return NaN;
  const weather = (state.weather && typeof state.weather === "object") ? state.weather : {};
  const watering = (state.wateringPercent && typeof state.wateringPercent === "object") ? state.wateringPercent : {};
  const candidates = [
    safeNumber(weather?.rain_24h_observed, NaN),
    safeNumber(weather?.rain_24h, NaN),
    safeNumber(watering?.rain_24h, NaN),
    sumRainHistory(state?.rainHistory, 24),
  ].filter((v) => Number.isFinite(v));
  return candidates.length ? Math.max(...candidates) : NaN;
}

function updateRainDailyTotalsFromState(state, eventTsIso = new Date().toISOString()) {
  if (!state || typeof state !== "object") return false;
  const tz = normalizeRainStatsTimeZone(
    state?.settingsPublic?.timezoneCanonical
    || state?.settingsPublic?.timezone
    || state?.status?.timezone
    || "Europe/Warsaw"
  );
  const eventMs = isoToMs(eventTsIso);
  const nowMs = Number.isFinite(eventMs) ? eventMs : Date.now();
  const dayKey = toDateKeyInTimeZone(nowMs, tz);
  if (!dayKey) return false;

  const normalizedTotals = normalizeRainDailyTotalsMap(state.rainDailyTotals, new Date(nowMs));
  let changed = JSON.stringify(normalizedTotals) !== JSON.stringify(state.rainDailyTotals || {});
  state.rainDailyTotals = normalizedTotals;

  const rollingNow = resolveRolling24hFromState(state);
  const prevRolling = safeNumber(state.rainRolling24LastMm, NaN);
  if (Number.isFinite(rollingNow)) {
    if (Number.isFinite(prevRolling)) {
      const delta = rollingNow - prevRolling;
      if (delta > 0.005) {
        const current = Math.max(0, safeNumber(state.rainDailyTotals?.[dayKey], 0));
        const next = Math.round((current + delta) * 100) / 100;
        state.rainDailyTotals[dayKey] = next;
        changed = true;
      }
    }
    if (!Number.isFinite(prevRolling) || Math.abs(prevRolling - rollingNow) > 0.0001) {
      state.rainRolling24LastMm = Math.round(rollingNow * 100) / 100;
      changed = true;
    }
    const nextAt = String(eventTsIso || new Date(nowMs).toISOString());
    if (String(state.rainRolling24At || "") !== nextAt) {
      state.rainRolling24At = nextAt;
      changed = true;
    }
  }

  const trimmed = normalizeRainDailyTotalsMap(state.rainDailyTotals, new Date(nowMs));
  if (JSON.stringify(trimmed) !== JSON.stringify(state.rainDailyTotals)) {
    state.rainDailyTotals = trimmed;
    changed = true;
  }
  return changed;
}

function mergeRainArchiveFromHistory(state, incomingHistory, eventTsIso = new Date().toISOString()) {
  if (!state || typeof state !== "object") return false;
  const prev = Array.isArray(state.rainArchive) ? state.rainArchive : [];
  const next = normalizeRainArchiveEntries([...prev, ...(Array.isArray(incomingHistory) ? incomingHistory : [])]);
  let changed = prev.length !== next.length;
  if (!changed) {
    for (let i = 0; i < prev.length; i += 1) {
      const a = prev[i];
      const b = next[i];
      if (!a || !b || a.time !== b.time || Math.abs(Number(a.rain || 0) - Number(b.rain || 0)) > 0.0001) {
        changed = true;
        break;
      }
    }
  }
  if (!changed) return false;
  state.rainArchive = next;
  state.rainArchiveAt = String(eventTsIso || new Date().toISOString());
  return true;
}

function normalizeRainStatsGrouping(raw) {
  const value = String(raw || "daily").trim().toLowerCase();
  if (value === "weekly" || value === "monthly" || value === "yearly") return value;
  return "daily";
}

function normalizeRainStatsTimeZone(raw) {
  const candidate = String(raw || "").trim();
  if (!candidate) return "Europe/Warsaw";
  try {
    new Intl.DateTimeFormat("en-US", { timeZone: candidate }).format(new Date());
    return candidate;
  } catch {
    return "Europe/Warsaw";
  }
}

function toDateKeyInTimeZone(tsMs, timeZone) {
  const parts = localDateParts(timeZone, new Date(tsMs));
  const year = Number(parts.year);
  const month = Number(parts.month);
  const day = Number(parts.day);
  if (!Number.isInteger(year) || !Number.isInteger(month) || !Number.isInteger(day)) {
    return localDateKey(tsMs);
  }
  return `${String(year).padStart(4, "0")}-${String(month).padStart(2, "0")}-${String(day).padStart(2, "0")}`;
}

function shiftDateKey(dateKey, dayDelta) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(dateKey || "").trim());
  if (!match) return "";
  const year = Number(match[1]);
  const month = Number(match[2]);
  const day = Number(match[3]);
  const date = new Date(Date.UTC(year, month - 1, day, 0, 0, 0, 0));
  if (!Number.isFinite(date.getTime())) return "";
  date.setUTCDate(date.getUTCDate() + Number(dayDelta || 0));
  return `${date.getUTCFullYear()}-${String(date.getUTCMonth() + 1).padStart(2, "0")}-${String(date.getUTCDate()).padStart(2, "0")}`;
}

function weekStartKeyFromDateKey(dateKey) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(dateKey || "").trim());
  if (!match) return "";
  const date = new Date(Date.UTC(Number(match[1]), Number(match[2]) - 1, Number(match[3]), 0, 0, 0, 0));
  if (!Number.isFinite(date.getTime())) return "";
  const dow = date.getUTCDay();
  const offsetToMonday = (dow + 6) % 7;
  return shiftDateKey(dateKey, -offsetToMonday);
}

function buildRainStats(state, options = {}) {
  const grouping = normalizeRainStatsGrouping(options.grouping);
  const tz = normalizeRainStatsTimeZone(
    state?.settingsPublic?.timezoneCanonical
    || state?.settingsPublic?.timezone
    || state?.status?.timezone
    || "Europe/Warsaw"
  );
  const dayTotals = new Map();
  const archive = normalizeRainArchiveEntries([
    ...(Array.isArray(state?.rainArchive) ? state.rainArchive : []),
    ...(Array.isArray(state?.rainHistory) ? state.rainHistory : []),
  ]);
  for (const point of archive) {
    const tsMs = Number(point.time) * 1000;
    const dayKey = toDateKeyInTimeZone(tsMs, tz);
    if (!dayKey) continue;
    dayTotals.set(dayKey, (dayTotals.get(dayKey) || 0) + Math.max(0, safeNumber(point.rain, 0)));
  }
  const dailyTotalsLive = normalizeRainDailyTotalsMap(state?.rainDailyTotals);
  for (const [dayKey, mmRaw] of Object.entries(dailyTotalsLive)) {
    const mm = Math.max(0, safeNumber(mmRaw, 0));
    const base = Math.max(0, safeNumber(dayTotals.get(dayKey), 0));
    // Keep full history from archive and allow "live day" tracker to raise the current bar.
    dayTotals.set(dayKey, Math.max(base, mm));
  }

  const byBucket = new Map();
  for (const [dayKey, mm] of dayTotals.entries()) {
    let bucketKey = dayKey;
    if (grouping === "weekly") bucketKey = weekStartKeyFromDateKey(dayKey);
    else if (grouping === "monthly") bucketKey = dayKey.slice(0, 7);
    else if (grouping === "yearly") bucketKey = dayKey.slice(0, 4);
    if (!bucketKey) continue;
    byBucket.set(bucketKey, (byBucket.get(bucketKey) || 0) + Math.max(0, safeNumber(mm, 0)));
  }

  let series = Array.from(byBucket.entries())
    .sort((a, b) => a[0].localeCompare(b[0]))
    .map(([key, sum]) => {
      const rainMm = Math.round(Math.max(0, Number(sum || 0)) * 100) / 100;
      if (grouping === "weekly") {
        const start = key;
        const end = shiftDateKey(key, 6);
        return {
          key,
          label: start ? start.slice(5) : key,
          fullLabel: `${start} - ${end}`,
          rainMm,
        };
      }
      if (grouping === "monthly") {
        return {
          key,
          label: `${key.slice(5, 7)}/${key.slice(0, 4)}`,
          fullLabel: key,
          rainMm,
        };
      }
      if (grouping === "yearly") {
        return { key, label: key, fullLabel: key, rainMm };
      }
      return {
        key,
        label: key.slice(8, 10),
        fullLabel: key,
        rainMm,
      };
    });

  const limits = { daily: 120, weekly: 104, monthly: 60, yearly: 24 };
  const limit = limits[grouping] || 120;
  if (series.length > limit) {
    series = series.slice(-limit);
  }
  const totalMm = Math.round(series.reduce((acc, item) => acc + Math.max(0, Number(item.rainMm || 0)), 0) * 100) / 100;
  const maxBucketMm = series.reduce((acc, item) => Math.max(acc, Math.max(0, Number(item.rainMm || 0))), 0);

  return {
    grouping,
    timeZone: tz,
    totalMm,
    maxBucketMm: Math.round(maxBucketMm * 100) / 100,
    points: series.length,
    series,
  };
}

function buildRainStatsCsv(payload) {
  const grouping = normalizeRainStatsGrouping(payload?.grouping);
  const lines = [];
  lines.push(["Widok", grouping]);
  lines.push(["Strefa czasowa", String(payload?.timeZone || "Europe/Warsaw")]);
  lines.push(["Suma opadu (mm)", Number(payload?.totalMm || 0)]);
  lines.push(["Max słupek (mm)", Number(payload?.maxBucketMm || 0)]);
  lines.push([]);
  lines.push(["Okres", "Opad (mm)"]);
  for (const item of Array.isArray(payload?.series) ? payload.series : []) {
    lines.push([String(item?.fullLabel || item?.key || ""), Number(item?.rainMm || 0)]);
  }
  return lines.map((cols) => cols.map(csvEscape).join(",")).join("\n");
}

function resolveObservedRain24h(state, freshness) {
  const weather = (state?.weather && typeof state.weather === "object") ? state.weather : {};
  const watering = (state?.wateringPercent && typeof state.wateringPercent === "object") ? state.wateringPercent : {};
  const fromHistory = freshness?.rainHistoryFresh ? Math.max(0, sumRainHistory(state?.rainHistory, 24)) : NaN;
  const fromWeather = freshness?.weatherFresh ? Math.max(0, safeNumber(weather?.rain_24h_observed, NaN)) : NaN;
  const fromWatering = freshness?.weatherFresh
    ? Math.max(0, safeNumber(watering?.rain_24h, NaN))
    : NaN;
  const candidates = [fromHistory, fromWeather, fromWatering].filter((v) => Number.isFinite(v));
  if (!candidates.length) {
    return { mm: 0, fresh: false };
  }
  return {
    mm: Math.max(...candidates),
    fresh: Number.isFinite(fromHistory) || Number.isFinite(fromWeather) || Number.isFinite(fromWatering),
  };
}

function effectiveRainMm(rawMm) {
  const rain = Math.max(0, safeNumber(rawMm, 0));
  if (rain <= 1.5) return rain * 0.35;
  if (rain <= 4) return rain * 0.55;
  if (rain <= 10) return rain * 0.78;
  if (rain <= 25) return rain * 0.62;
  return rain * 0.45;
}

function parseHmToMinutes(value) {
  const raw = String(value || "").trim();
  const match = /^(\d{1,2}):(\d{2})$/.exec(raw);
  if (!match) return null;
  const h = Number(match[1]);
  const m = Number(match[2]);
  if (!Number.isInteger(h) || !Number.isInteger(m)) return null;
  if (h < 0 || h > 23 || m < 0 || m > 59) return null;
  return h * 60 + m;
}

function parseWindow(windowText, fallback) {
  const text = String(windowText || "").trim();
  const match = /^(\d{1,2}:\d{2})\s*-\s*(\d{1,2}:\d{2})$/.exec(text);
  if (!match) return fallback;
  const start = parseHmToMinutes(match[1]);
  const end = parseHmToMinutes(match[2]);
  if (start == null || end == null || end <= start) return fallback;
  return { startMin: start, endMin: end };
}

function appendSmartClimateHistory(cfg, entry) {
  if (!cfg || typeof cfg !== "object") return;
  if (!Array.isArray(cfg.history)) cfg.history = [];
  cfg.history.push(entry);
  if (cfg.history.length > SMART_CLIMATE_MAX_HISTORY) {
    cfg.history = cfg.history.slice(-SMART_CLIMATE_MAX_HISTORY);
  }
}

function resolveSmartClimateTimezone(state, cfg) {
  const raw = String(
    cfg?.timezone
    || state?.settingsPublic?.timezoneCanonical
    || state?.settingsPublic?.timezone
    || "Europe/Warsaw"
  ).trim();
  if (!raw) return "Europe/Warsaw";
  try {
    // Throws on invalid timezone names.
    // eslint-disable-next-line no-new
    new Intl.DateTimeFormat("en-US", { timeZone: raw });
    return raw;
  } catch {
    return "Europe/Warsaw";
  }
}

function normalizeTimeZoneId(rawValue, fallback = "Europe/Warsaw") {
  const raw = String(rawValue || "").trim();
  if (!raw) return fallback;
  try {
    // eslint-disable-next-line no-new
    new Intl.DateTimeFormat("en-US", { timeZone: raw });
    return raw;
  } catch {
    return fallback;
  }
}

function resolveScheduleTimezone(req, selectedState) {
  const headerTz = normalizeTimeZoneId(
    req?.headers?.["x-wms-timezone"]
    || req?.headers?.["x-timezone"]
    || req?.headers?.["x-time-zone"],
    ""
  );
  if (headerTz) return headerTz;

  if (selectedState && !isSmartPlugDeviceState(selectedState)) {
    const stateTz = normalizeTimeZoneId(
      selectedState?.settingsPublic?.timezoneCanonical || selectedState?.settingsPublic?.timezone,
      ""
    );
    if (stateTz) return stateTz;
  }

  const userId = String(req?.user?.id || "").trim();
  const db = req?.db;
  if (userId && db?.devices && typeof db.devices === "object") {
    for (const state of Object.values(db.devices)) {
      if (!state || typeof state !== "object") continue;
      const ownerUserId = String(state?.meta?.ownerUserId || "").trim();
      if (!ownerUserId || ownerUserId !== userId) continue;
      if (isSmartPlugDeviceState(state)) continue;
      const tz = normalizeTimeZoneId(
        state?.settingsPublic?.timezoneCanonical || state?.settingsPublic?.timezone,
        ""
      );
      if (tz) return tz;
    }
  }

  return "Europe/Warsaw";
}

function estimateDayLengthHours(latDeg, dayOfYear) {
  const lat = clampNumber(latDeg, -89.5, 89.5) * Math.PI / 180;
  const dr = 1 + 0.033 * Math.cos((2 * Math.PI / 365) * dayOfYear);
  const decl = 0.409 * Math.sin((2 * Math.PI / 365) * dayOfYear - 1.39);
  const cosWs = clampNumber(-Math.tan(lat) * Math.tan(decl), -1, 1);
  const ws = Math.acos(cosWs);
  return (24 / Math.PI) * ws * dr / dr;
}

function parseSunTimeToMinutes(value) {
  const m = /^(\d{1,2}):(\d{2})$/.exec(String(value || "").trim());
  if (!m) return null;
  const hh = Number(m[1]);
  const mm = Number(m[2]);
  if (!Number.isInteger(hh) || !Number.isInteger(mm)) return null;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return null;
  return hh * 60 + mm;
}

function estimateSolarRadiationMj(weather, latitude, dayOfYear, altitudeM = SMART_CLIMATE_DEFAULT_ALTITUDE_M) {
  const lat = clampNumber(latitude, -89.5, 89.5);
  const z = clampNumber(altitudeM, -100, 5000);
  const cloud = clampNumber(weather?.clouds ?? 50, 0, 100);
  const tSunriseMin = parseSunTimeToMinutes(weather?.sunrise);
  const tSunsetMin = parseSunTimeToMinutes(weather?.sunset);
  let daylightHours = estimateDayLengthHours(lat, dayOfYear);
  if (tSunriseMin != null && tSunsetMin != null && tSunsetMin > tSunriseMin) {
    daylightHours = clampNumber((tSunsetMin - tSunriseMin) / 60, 1, 16.5);
  }

  const phi = lat * Math.PI / 180;
  const dr = 1 + 0.033 * Math.cos((2 * Math.PI / 365) * dayOfYear);
  const delta = 0.409 * Math.sin((2 * Math.PI / 365) * dayOfYear - 1.39);
  const ws = Math.acos(clampNumber(-Math.tan(phi) * Math.tan(delta), -1, 1));
  const gsc = 0.0820;
  const ra = (24 * 60 / Math.PI) * gsc * dr
    * (ws * Math.sin(phi) * Math.sin(delta) + Math.cos(phi) * Math.cos(delta) * Math.sin(ws));
  const sunshineRatio = clampNumber(1 - cloud / 100, 0, 1);
  const nOverN = clampNumber(0.18 + 0.82 * sunshineRatio, 0.05, 1.0);
  const rs = Math.max(0.1, (0.25 + 0.5 * nOverN) * ra);
  const rso = (0.75 + 0.00002 * z) * ra;
  return { rs, ra, rso: Math.max(0.1, rso), daylightHours };
}

function computeEtoPenman(weather, { latitude, altitudeM, dayOfYear }) {
  const tMin = safeNumber(weather?.temp_min ?? weather?.temp ?? 12, 12);
  const tMax = safeNumber(weather?.temp_max ?? weather?.temp ?? 22, 22);
  const tMean = safeNumber(weather?.temp ?? ((tMin + tMax) / 2), (tMin + tMax) / 2);
  const rh = clampNumber(weather?.humidity ?? 60, 5, 100);
  const windMs = Math.max(0, safeNumber(weather?.wind, safeNumber(weather?.wind_kmh, 0) / 3.6));

  const { rs, rso } = estimateSolarRadiationMj(weather, latitude, dayOfYear, altitudeM);
  const esTmin = 0.6108 * Math.exp((17.27 * tMin) / (tMin + 237.3));
  const esTmax = 0.6108 * Math.exp((17.27 * tMax) / (tMax + 237.3));
  const es = (esTmin + esTmax) / 2;
  const ea = clampNumber(es * rh / 100, 0, es);
  const delta = (4098 * (0.6108 * Math.exp((17.27 * tMean) / (tMean + 237.3)))) / Math.pow(tMean + 237.3, 2);
  const p = 101.3 * Math.pow((293 - 0.0065 * altitudeM) / 293, 5.26);
  const gamma = 0.000665 * p;
  const albedo = 0.23;
  const rns = (1 - albedo) * rs;
  const sigma = 4.903e-9;
  const tMaxK = tMax + 273.16;
  const tMinK = tMin + 273.16;
  const cloudRatio = clampNumber(rs / Math.max(0.1, rso), 0.2, 1.0);
  const rnl = sigma
    * ((Math.pow(tMaxK, 4) + Math.pow(tMinK, 4)) / 2)
    * (0.34 - 0.14 * Math.sqrt(Math.max(0, ea)))
    * (1.35 * cloudRatio - 0.35);
  const rn = rns - rnl;
  const g = 0;
  const numerator = 0.408 * delta * (rn - g) + gamma * (900 / (tMean + 273)) * windMs * Math.max(0, es - ea);
  const denominator = delta + gamma * (1 + 0.34 * windMs);
  const eto = denominator > 0 ? numerator / denominator : 0;
  return clampNumber(eto, 0, 15);
}

function seasonalSmartClimateFactor(dayOfYear) {
  const phase = Math.sin(((2 * Math.PI) / 365) * (dayOfYear - 80));
  // 0.82 .. 1.18 through the year, with peak in late spring/summer.
  return clampNumber(1 + phase * 0.18, 0.82, 1.18);
}

function computeSmartClimateThresholdMm(zoneCfg) {
  const rootDepthMm = clampNumber(zoneCfg.rootDepthMm, 40, 400);
  const taw = rootDepthMm * 0.12; // Typical available water [mm]
  return clampNumber(taw * clampNumber(zoneCfg.allowedDepletion, 0.2, 0.8), 2, 120);
}

function chooseSmartClimateWindow(now, timeZone, weather) {
  const local = localDateParts(timeZone, now);
  const rainForecast = Math.max(0, safeNumber(weather?.rain_24h_forecast ?? 0, 0));
  const windKmh = Math.max(0, safeNumber(weather?.wind_kmh ?? safeNumber(weather?.wind, 0) * 3.6, 0));
  const freezeRisk = resolveSmartClimateMinTemp(weather, 50) <= 1.2;

  const candidates = [];
  for (let dayOffset = 0; dayOffset <= 1; dayOffset += 1) {
    const baseDate = makeDateInTimeZone(
      timeZone,
      local.year,
      local.month,
      local.day + dayOffset,
      0,
      0
    );
    const baseLocal = localDateParts(timeZone, baseDate);
    for (const w of SMART_CLIMATE_WINDOWS) {
      const parsed = parseWindow(`${w.start}-${w.end}`, { startMin: 240, endMin: 390 });
      const startHour = Math.floor(parsed.startMin / 60);
      const startMinute = parsed.startMin % 60;
      const startDate = makeDateInTimeZone(timeZone, baseLocal.year, baseLocal.month, baseLocal.day, startHour, startMinute);
      if (startDate.getTime() < now.getTime() - 5 * 60_000) continue;
      let score = w.score;
      score -= Math.min(0.35, rainForecast / 40);
      score -= Math.min(0.2, windKmh / 180);
      if (freezeRisk) score -= 0.25;
      if (dayOffset === 1) score -= 0.05;
      candidates.push({ startDate, score, dayOffset });
    }
  }
  if (!candidates.length) return null;
  // Prefer okna z bieżącego dnia. Dzięki temu po pominięciu 04:00
  // algorytm może przejść na 06:30 zamiast od razu odkładać na jutro 04:00.
  const pool = candidates.some((item) => item.dayOffset === 0)
    ? candidates.filter((item) => item.dayOffset === 0)
    : candidates;
  pool.sort((a, b) => {
    if (b.score !== a.score) return b.score - a.score;
    return a.startDate.getTime() - b.startDate.getTime();
  });
  return pool[0].startDate;
}

function resolveSmartClimateBlockers(weather, zoneCfg, recentRainMm, forecastRainMm, nowMs = Date.now()) {
  const blockers = [];
  const nowTemp = safeNumber(weather?.temp ?? 20, 20);
  // Frost hard-stop should reflect current/day conditions, not "tomorrow minimum".
  // Otherwise daytime watering can be blocked by a distant forecast artifact.
  const minTemp = resolveSmartClimateMinTemp(weather, nowTemp, { includeTomorrow: false });
  const windKmh = Math.max(0, safeNumber(weather?.wind_kmh ?? safeNumber(weather?.wind, 0) * 3.6, 0));
  const windLimit = zoneCfg.zoneType === "drip" ? 45 : 25;
  if (minTemp <= 1.2) blockers.push("ryzyko przymrozku");
  if (windKmh >= windLimit) blockers.push(`silny wiatr (${windKmh.toFixed(1)} km/h)`);
  if (recentRainMm >= 14) blockers.push(`mocny deszcz (${recentRainMm.toFixed(1)} mm / 24 h)`);
  if (forecastRainMm >= 8) blockers.push(`prognoza deszczu (${forecastRainMm.toFixed(1)} mm / 24 h)`);

  const lastRunMs = Date.parse(String(zoneCfg.lastRunAt || ""));
  const minIntervalMs = Math.round(clampNumber(zoneCfg.minIntervalHours, 2, 120) * 3600000);
  if (Number.isFinite(lastRunMs) && nowMs - lastRunMs < minIntervalMs) {
    const waitMin = Math.ceil((minIntervalMs - (nowMs - lastRunMs)) / 60000);
    blockers.push(`pozostało ${formatMinutesPl(waitMin)} do kolejnego uruchomienia`);
  }
  return blockers;
}

function computeSmartClimateZoneDecision({ state, cfg, zoneCfg, timeZone, now }) {
  const weather = (state?.weather && typeof state.weather === "object") ? state.weather : {};
  const freshness = getSmartClimateDataFreshness(state, now.getTime());
  const weatherUsable = freshness.weatherFresh;
  const profile = normalizeSmartClimateProfile(cfg.profile);
  const profileMul = SMART_CLIMATE_PROFILE_MULTIPLIERS[profile] || 1.0;
  const dayOfYear = dayOfYearUtc(now);
  const eto = weatherUsable
    ? computeEtoPenman(weather, {
      latitude: cfg.latitude,
      altitudeM: cfg.altitudeM,
      dayOfYear,
    })
    : 0;
  const seasonFactor = seasonalSmartClimateFactor(dayOfYear);
  const humidity = clampNumber(weather?.humidity ?? 60, 1, 100);
  const windKmh = Math.max(0, safeNumber(weather?.wind_kmh ?? safeNumber(weather?.wind, 0) * 3.6, 0));
  const cloud = clampNumber(weather?.clouds ?? 50, 0, 100);
  const nowTemp = safeNumber(weather?.temp ?? 20, 20);
  let stressFactor = 1;
  if (nowTemp > 32) stressFactor *= 1.08;
  if (nowTemp > 36) stressFactor *= 1.08;
  if (humidity < 32) stressFactor *= 1.06;
  if (humidity > 86) stressFactor *= 0.9;
  if (windKmh > 18) stressFactor *= 1.05;
  if (cloud < 22) stressFactor *= 1.04;
  if (cloud > 88) stressFactor *= 0.88;
  const etc = weatherUsable
    ? Math.max(0, eto * clampNumber(zoneCfg.kc, 0.2, 2.0) * seasonFactor * stressFactor)
    : 0;

  const observedRain24h = resolveObservedRain24h(state, freshness);
  const rainNow = weatherUsable ? Math.max(0, safeNumber(weather?.rain, 0)) : 0;
  const forecastRain24h = Math.max(0, safeNumber(weather?.rain_24h_forecast ?? 0, 0));
  const rainRecent = Math.max(observedRain24h.mm, rainNow);
  const rainDelta = Math.max(0, rainRecent - Math.max(0, safeNumber(zoneCfg.lastObservedRain24hMm, 0)));
  // Deficyt zmniejszamy wyłącznie o opad już zaobserwowany.
  // Prognoza ma wpływać na decyzję o starcie (okno/blokery), ale nie na księgowanie bilansu.
  const effectiveRain = effectiveRainMm(rainDelta);

  const lastDecisionMs = Date.parse(String(zoneCfg.lastDecisionAt || ""));
  const elapsedHours = Number.isFinite(lastDecisionMs)
    ? clampNumber((now.getTime() - lastDecisionMs) / 3600000, 0, 48)
    : 6;
  const etcElapsed = etc * (elapsedHours / 24);
  const thresholdMm = computeSmartClimateThresholdMm(zoneCfg);
  const maxDeficit = thresholdMm / clampNumber(zoneCfg.allowedDepletion, 0.2, 0.8);
  const nextDeficit = clampNumber(Math.max(0, safeNumber(zoneCfg.deficitMm, 0) + etcElapsed - effectiveRain), 0, maxDeficit);

  const blockers = resolveSmartClimateBlockers(weather, zoneCfg, rainRecent, forecastRain24h, now.getTime());
  if (!freshness.weatherFresh) {
    const ageMin = Number.isFinite(freshness.weatherAgeMs) ? Math.round(freshness.weatherAgeMs / 60000) : null;
    blockers.unshift(ageMin == null
      ? "brak świeżych danych pogodowych"
      : `brak świeżych danych pogodowych (${ageMin} min)`);
  }
  if (!freshness.rainHistoryFresh && !observedRain24h.fresh) {
    const ageMin = Number.isFinite(freshness.rainHistoryAgeMs) ? Math.round(freshness.rainHistoryAgeMs / 60000) : null;
    blockers.unshift(ageMin == null
      ? "brak świeżej historii opadów"
      : `brak świeżej historii opadów (${ageMin} min)`);
  }
  const needsWater = nextDeficit >= thresholdMm;
  const nextWindowDate = chooseSmartClimateWindow(now, timeZone, weather);
  const commandPendingUntilMs = Date.parse(String(zoneCfg.commandPendingUntil || ""));
  const pending = Number.isFinite(commandPendingUntilMs) && commandPendingUntilMs > now.getTime();
  const readyByWindow = nextWindowDate ? now.getTime() >= nextWindowDate.getTime() - 60_000 : false;
  const shouldRun = needsWater && blockers.length === 0 && readyByWindow && !pending;

  const requiredMm = Math.max(0, nextDeficit - thresholdMm * 0.15) * profileMul;
  const runtimeRawMin = (requiredMm / Math.max(0.1, zoneCfg.precipitationMmPerHour)) * 60;
  const runtimeMin = Math.round(clampNumber(runtimeRawMin, zoneCfg.minRuntimeMin, zoneCfg.maxRuntimeMin));
  const appliedMm = (runtimeMin / 60) * zoneCfg.precipitationMmPerHour * 0.85;

  return {
    eto,
    etc,
    nextDeficit,
    thresholdMm,
    rainRecent,
    rainObservedFresh: observedRain24h.fresh,
    forecastRain24h,
    effectiveRain,
    needsWater,
    blockers,
    nextWindowDate,
    shouldRun,
    runtimeMin,
    appliedMm,
    profile,
    weatherFresh: freshness.weatherFresh,
    rainHistoryFresh: freshness.rainHistoryFresh,
  };
}

async function syncSmartClimateProgramsForDevice(db, deviceId, cfg, state) {
  const managedZones = getManagedSmartClimateZoneSet(db, deviceId);
  if (!managedZones.size) {
    cfg.pendingProgramSync = false;
    cfg.lastProgramSyncError = "";
    return { changed: false, removed: 0 };
  }
  const programs = Array.isArray(state?.programs) ? state.programs : [];
  const filtered = programs.filter((item) => !managedZones.has(Number(item?.zone)));
  const removed = programs.length - filtered.length;
  if (removed <= 0) {
    cfg.pendingProgramSync = false;
    cfg.lastProgramSyncError = "";
    return { changed: false, removed: 0 };
  }
  if (!isDeviceOnline(state) || !mqttClient.connected) {
    cfg.pendingProgramSync = true;
    cfg.lastProgramSyncError = "Urządzenie offline - synchronizacja harmonogramu odłożona.";
    return { changed: true, removed };
  }
  await publishCommandWithAck(deviceId, "cmd/programs/import", { programs: filtered });
  mqttPublish(`${baseTopic(deviceId)}/global/refresh`, "");
  cfg.pendingProgramSync = false;
  cfg.lastProgramSyncError = "";
  cfg.lastProgramSyncAt = new Date().toISOString();
  appendSmartClimateHistory(cfg, {
    ts: new Date().toISOString(),
    level: "info",
    action: "sync_programs",
    detail: `Usunięto ${removed} harmonogram(y) z trybu ręcznego dla stref Smart Climate.`,
  });
  return { changed: true, removed };
}

async function runSmartClimateEngineForDevice(db, deviceId, { force = false } = {}) {
  const state = getDeviceState(db, deviceId);
  if (!isSmartClimateAllowedForDevice(db, deviceId, state)) return false;
  const zones = Array.isArray(state?.zones) ? state.zones : [];
  const cfg = ensureSmartClimateConfig(db, deviceId, zones.length);
  if (cfg.enabled !== true) return false;

  const zoneEntries = Object.values(cfg.zones || {}).filter((item) => item && item.enabled === true);
  if (!zoneEntries.length) return false;

  const now = new Date();
  const timeZone = resolveSmartClimateTimezone(state, cfg);
  cfg.timezone = timeZone;
  let changed = false;
  const pendingRemoved = prunePendingSmartClimateStarts(db, now.getTime());
  if (pendingRemoved > 0) changed = true;
  const managedZones = getManagedSmartClimateZoneSet(db, deviceId);
  const hasManagedPrograms = hasProgramsInManagedZones(state?.programs, managedZones);

  if (cfg.pendingProgramSync || hasManagedPrograms) {
    try {
      const syncResult = await syncSmartClimateProgramsForDevice(db, deviceId, cfg, state);
      if (syncResult.changed) changed = true;
    } catch (err) {
      cfg.lastProgramSyncError = String(err?.message || err);
      changed = true;
    }
  }

  if (!isDeviceOnline(state)) {
    for (const zoneCfg of zoneEntries) {
      zoneCfg.lastReason = "urządzenie offline";
      zoneCfg.lastDecisionAt = now.toISOString();
    }
    cfg.lastEvaluationAt = now.toISOString();
    cfg.lastEngineError = "";
    return true;
  }

  const hasActiveZones = zones.some(zoneIsActive);
  const runnableDecisions = [];

  for (const zoneCfg of zoneEntries) {
    const normalized = normalizeSmartClimateZoneConfig(zoneCfg.id, zoneCfg, zoneCfg);
    if (!normalized) continue;
    cfg.zones[String(zoneCfg.id)] = normalized;
    const decision = computeSmartClimateZoneDecision({ state, cfg, zoneCfg: normalized, timeZone, now });
    normalized.deficitMm = decision.nextDeficit;
    if (decision.rainObservedFresh) {
      normalized.lastObservedRain24hMm = decision.rainRecent;
    }
    normalized.lastEtoMm = decision.eto;
    normalized.lastEtcMm = decision.etc;
    normalized.lastEffectiveRainMm = decision.effectiveRain;
    normalized.lastDecisionAt = now.toISOString();
    normalized.nextRunAt = decision.nextWindowDate ? decision.nextWindowDate.toISOString() : "";
    normalized.lastReason = normalizeSmartClimateReasonText(
      decision.blockers[0] || (decision.needsWater ? "czeka na okno podlewania" : "deficyt poniżej progu")
    );
    changed = true;
    if (decision.shouldRun && decision.runtimeMin > 0) {
      runnableDecisions.push({ zoneCfg: normalized, decision });
    }
  }

  if (!runnableDecisions.length || hasActiveZones) {
    if (hasActiveZones) {
      for (const item of runnableDecisions) {
        item.zoneCfg.lastReason = "inna strefa już pracuje";
      }
    }
    cfg.lastEvaluationAt = now.toISOString();
    cfg.lastEngineError = "";
    return changed;
  }

  runnableDecisions.sort((a, b) => b.decision.nextDeficit - a.decision.nextDeficit);
  const selected = runnableDecisions[0];
  const zoneId = Number(selected.zoneCfg.id);
  const seconds = Math.max(30, Math.round(selected.decision.runtimeMin * 60));

  if (!force) {
    const lastRunMs = Date.parse(String(selected.zoneCfg.lastRunAt || ""));
    if (Number.isFinite(lastRunMs) && now.getTime() - lastRunMs < 60_000) {
      cfg.lastEvaluationAt = now.toISOString();
      return changed;
    }
  }

  try {
    const cmd = await publishCommandWithFastAck(deviceId, `cmd/zones/${zoneId}/start`, {
      action: "start",
      seconds,
      source: "smart_climate",
      profile: cfg.profile,
      zone_type: selected.zoneCfg.zoneType,
    }, { allowAckTimeout: true, ackWaitMs: 450 });
    mqttPublish(`${baseTopic(deviceId)}/global/refresh`, "");
    const ackStatus = String(cmd?.ack?.status || "accepted").toLowerCase();
    const pendingWindowMs = ackStatus === "accepted"
      ? SMART_CLIMATE_COMMAND_PENDING_ACCEPTED_MS
      : SMART_CLIMATE_COMMAND_PENDING_ACK_TIMEOUT_MS;
    selected.zoneCfg.commandPendingUntil = new Date(now.getTime() + pendingWindowMs).toISOString();
    registerPendingSmartClimateStart(db, deviceId, {
      source: "smart_climate",
      zoneId,
      requestedSec: seconds,
      commandId: cmd?.commandId || "",
      ackStatus,
    });
    selected.zoneCfg.lastReason = normalizeSmartClimateReasonText(
      ackStatus === "accepted"
        ? `wysłano start (${selected.decision.runtimeMin} min)`
        : `wysłano start (${selected.decision.runtimeMin} min), ACK timeout`
    );
    selected.zoneCfg.nextRunAt = "";
    appendSmartClimateHistory(cfg, {
      ts: now.toISOString(),
      level: "info",
      action: "run_zone",
      phase: "command_sent",
      zoneId,
      runtimeMin: selected.decision.runtimeMin,
      seconds,
      ackStatus,
      commandId: String(cmd?.commandId || ""),
      deficitMm: Number(selected.zoneCfg.deficitMm.toFixed(2)),
      etoMm: Number(selected.decision.eto.toFixed(2)),
      etcMm: Number(selected.decision.etc.toFixed(2)),
      reason: selected.zoneCfg.lastReason,
    });
    cfg.lastEngineError = "";
    changed = true;
  } catch (err) {
    const error = String(err?.message || err);
    selected.zoneCfg.lastReason = normalizeSmartClimateReasonText(`błąd uruchomienia: ${error}`);
    cfg.lastEngineError = error;
    appendSmartClimateHistory(cfg, {
      ts: now.toISOString(),
      level: "error",
      action: "run_zone_failed",
      zoneId,
      reason: error,
    });
    changed = true;
  }

  cfg.lastEvaluationAt = now.toISOString();
  return changed;
}

async function runSmartClimateEngineCore({ force = false, onlyDeviceId = "" } = {}) {
  const db = loadDb();
  const allIds = Object.keys(db.devices || {});
  const deviceIds = onlyDeviceId ? allIds.filter((id) => id === onlyDeviceId) : allIds;
  let changed = false;
  for (const deviceId of deviceIds) {
    if (smartClimateDeviceLocks.has(deviceId)) {
      logEvent(`[SMART-CLIMATE] skip overlapping device run for ${deviceId}`, "WARN");
      continue;
    }
    smartClimateDeviceLocks.add(deviceId);
    try {
      const didChange = await runSmartClimateEngineForDevice(db, deviceId, { force });
      if (didChange) changed = true;
    } catch (err) {
      logEvent(`[SMART-CLIMATE] engine error device=${deviceId}: ${String(err?.message || err)}`, "ERROR");
    } finally {
      smartClimateDeviceLocks.delete(deviceId);
    }
  }
  if (changed) saveDb(db);
  return changed;
}

async function runSmartClimateEngine({ force = false, onlyDeviceId = "" } = {}) {
  const normalizedDeviceId = String(onlyDeviceId || "").trim();
  const opts = { force: force === true, onlyDeviceId: normalizedDeviceId };

  if (smartClimateRunPromise) {
    if (!opts.force && !opts.onlyDeviceId) {
      logEvent("[SMART-CLIMATE] periodic run skipped - previous run still in progress", "WARN");
      return false;
    }
    try {
      await smartClimateRunPromise;
    } catch {
      // previous run failure should not block the next request
    }
  }

  const currentRun = runSmartClimateEngineCore(opts);
  smartClimateRunPromise = currentRun;
  try {
    return await currentRun;
  } finally {
    if (smartClimateRunPromise === currentRun) smartClimateRunPromise = null;
  }
}

function buildSmartClimateApiPayload(db, deviceId, state, cfg) {
  const zones = Array.isArray(state?.zones) ? state.zones : [];
  const freshness = getSmartClimateDataFreshness(state);
  const zoneView = zones.map((zone, index) => {
    const zoneCfg = cfg?.zones?.[String(index)] || null;
    const managed = !!(zoneCfg && zoneCfg.enabled === true && cfg?.enabled === true);
    return {
      id: index,
      name: String(zone?.name || `Strefa ${index + 1}`),
      active: zoneIsActive(zone),
      remainingSec: Math.max(0, safeNumber(zone?.remaining ?? 0, 0)),
      managed,
      zoneType: managed ? String(zoneCfg.zoneType || "lawn") : null,
      deficitMm: managed ? Number(safeNumber(zoneCfg.deficitMm, 0).toFixed(2)) : null,
      nextRunAt: managed ? String(zoneCfg.nextRunAt || "") : "",
      lastRunAt: managed ? String(zoneCfg.lastRunAt || "") : "",
      lastReason: managed ? normalizeSmartClimateReasonText(String(zoneCfg.lastReason || "")) : "",
      lastRuntimeMin: managed ? Math.max(0, Number(zoneCfg.lastRuntimeMin || 0)) : 0,
      minIntervalHours: managed ? Number(zoneCfg.minIntervalHours || 0) : 0,
      precipitationMmPerHour: managed ? Number(zoneCfg.precipitationMmPerHour || 0) : 0,
      maxRuntimeMin: managed ? Number(zoneCfg.maxRuntimeMin || 0) : 0,
      minRuntimeMin: managed ? Number(zoneCfg.minRuntimeMin || 0) : 0,
    };
  });
  const managedCount = zoneView.filter((item) => item.managed).length;
  const sortedNext = zoneView
    .filter((item) => item.managed && item.nextRunAt)
    .map((item) => ({ id: item.id, ts: Date.parse(item.nextRunAt), nextRunAt: item.nextRunAt }))
    .filter((item) => Number.isFinite(item.ts))
    .sort((a, b) => a.ts - b.ts);
  const nextRun = sortedNext.length ? sortedNext[0].nextRunAt : "";
  return {
    ok: true,
    device_id: deviceId,
    online: isDeviceOnline(state),
    config: {
      name: String(cfg?.name || SMART_CLIMATE_NAME),
      enabled: cfg?.enabled === true,
      profile: normalizeSmartClimateProfile(cfg?.profile),
      timezone: resolveSmartClimateTimezone(state, cfg),
      latitude: Number(cfg?.latitude ?? SMART_CLIMATE_DEFAULT_LAT),
      altitudeM: Number(cfg?.altitudeM ?? SMART_CLIMATE_DEFAULT_ALTITUDE_M),
      managedCount,
      nextRunAt: nextRun,
      lastEvaluationAt: String(cfg?.lastEvaluationAt || ""),
      updatedAt: String(cfg?.updatedAt || ""),
      pendingProgramSync: cfg?.pendingProgramSync === true,
      lastProgramSyncError: String(cfg?.lastProgramSyncError || ""),
      lastProgramSyncAt: String(cfg?.lastProgramSyncAt || ""),
      lastEngineError: String(cfg?.lastEngineError || ""),
      weatherFresh: freshness.weatherFresh,
      rainHistoryFresh: freshness.rainHistoryFresh,
      weatherAgeSec: Number.isFinite(freshness.weatherAgeMs) ? Math.max(0, Math.round(freshness.weatherAgeMs / 1000)) : null,
      rainHistoryAgeSec: Number.isFinite(freshness.rainHistoryAgeMs) ? Math.max(0, Math.round(freshness.rainHistoryAgeMs / 1000)) : null,
      history: Array.isArray(cfg?.history) ? cfg.history.slice(-40) : [],
    },
    zones: zoneView,
  };
}

function stablePlugCodeFromDeviceId(deviceId) {
  const text = String(deviceId || "").trim();
  let hash = 2166136261 >>> 0; // FNV-1a 32-bit
  for (let i = 0; i < text.length; i += 1) {
    hash ^= text.charCodeAt(i);
    hash = Math.imul(hash, 16777619) >>> 0;
  }
  return (hash % VIRTUAL_PLUG_CODE_SPAN) + VIRTUAL_PLUG_CODE_MIN;
}

function listOwnedSmartPlugEntries(db, userId, { excludeDeviceId = "" } = {}) {
  const entries = [];
  const owned = getOwnedDeviceIds(db, userId);
  for (const deviceId of owned) {
    if (excludeDeviceId && deviceId === excludeDeviceId) continue;
    const state = getDeviceState(db, deviceId);
    if (!isSmartPlugDeviceState(state)) continue;
    entries.push({ deviceId, state });
  }
  entries.sort((a, b) => String(a.deviceId).localeCompare(String(b.deviceId), "pl"));
  return entries;
}

function buildVirtualPlugCatalog(db, userId, { excludeDeviceId = "" } = {}) {
  const entries = listOwnedSmartPlugEntries(db, userId, { excludeDeviceId });
  const codeToDeviceId = new Map();
  const deviceToZoneId = new Map();
  const virtualZones = [];
  const usedCodes = new Set();

  for (const entry of entries) {
    let code = stablePlugCodeFromDeviceId(entry.deviceId);
    while (usedCodes.has(code)) {
      code += 1;
      if (code >= VIRTUAL_PLUG_CODE_MIN + VIRTUAL_PLUG_CODE_SPAN) code = VIRTUAL_PLUG_CODE_MIN;
    }
    usedCodes.add(code);
    codeToDeviceId.set(code, entry.deviceId);

    const zoneId = VIRTUAL_PLUG_ZONE_BASE + code;
    deviceToZoneId.set(entry.deviceId, zoneId);

    const state = entry.state || {};
    const telem = (state.plugTelemetry && typeof state.plugTelemetry === "object") ? state.plugTelemetry : {};
    const zones = Array.isArray(state.zones) ? state.zones : [];
    const zone0 = zones[0] || {};
    const relayOn = telem.relay_on === true || zoneIsActive(zone0);
    const remaining = Number(telem.remaining_sec ?? zone0?.remaining ?? 0) || 0;
    const zoneName = String(state?.settingsPublic?.zoneName || "").trim();

    virtualZones.push({
      id: zoneId,
      name: zoneName || `Gniazdko ${entry.deviceId}`,
      active: relayOn,
      remaining,
      duration: remaining,
      online: isDeviceOnline(state),
      device_id: entry.deviceId,
      is_virtual_plug: true,
    });
  }

  return { entries, codeToDeviceId, deviceToZoneId, virtualZones };
}

function decodeVirtualZoneCode(rawZoneId) {
  const zoneId = Number(rawZoneId);
  if (!Number.isInteger(zoneId)) return null;
  if (zoneId < VIRTUAL_PLUG_ZONE_BASE) return null;
  return zoneId - VIRTUAL_PLUG_ZONE_BASE;
}

function decodeVirtualProgramId(rawProgramId) {
  const id = Number(rawProgramId);
  if (!Number.isInteger(id) || id < VIRTUAL_PLUG_PROGRAM_BASE) return null;
  const offset = id - VIRTUAL_PLUG_PROGRAM_BASE;
  const code = Math.floor(offset / VIRTUAL_PLUG_PROGRAM_SLOT);
  const index = offset % VIRTUAL_PLUG_PROGRAM_SLOT;
  if (!Number.isInteger(code) || code <= 0 || index < 0) return null;
  return { code, index };
}

function encodeVirtualProgramId(code, index) {
  const safeIndex = Math.max(0, Math.min(VIRTUAL_PLUG_PROGRAM_SLOT - 1, Number(index) || 0));
  return VIRTUAL_PLUG_PROGRAM_BASE + Number(code) * VIRTUAL_PLUG_PROGRAM_SLOT + safeIndex;
}

function normalizeProgramsArray(programs) {
  const list = [];
  const src = Array.isArray(programs) ? programs : [];
  for (let i = 0; i < src.length; i += 1) {
    const item = (src[i] && typeof src[i] === "object") ? { ...src[i] } : {};
    const idNum = Number(item.id);
    item.id = Number.isInteger(idNum) ? idNum : i;
    list.push(item);
  }
  return list;
}

function filterProgramsByManagedZones(programs, managedZones) {
  const managed = managedZones instanceof Set ? managedZones : new Set();
  if (!managed.size) return programs;
  return programs.filter((item) => !managed.has(Number(item?.zone)));
}

function hasProgramsInManagedZones(programs, managedZones) {
  const managed = managedZones instanceof Set ? managedZones : new Set();
  if (!managed.size) return false;
  if (!Array.isArray(programs)) return false;
  return programs.some((item) => managed.has(Number(item?.zone)));
}

function buildProgramsResponseWithVirtualPlugs(db, userId, selectedDeviceId, selectedState) {
  const managed = getManagedSmartClimateZoneSet(db, selectedDeviceId);
  const basePrograms = filterProgramsByManagedZones(normalizeProgramsArray(selectedState?.programs), managed);
  if (isSmartPlugDeviceState(selectedState)) return basePrograms;

  const catalog = buildVirtualPlugCatalog(db, userId, { excludeDeviceId: selectedDeviceId });
  if (!catalog.entries.length) return basePrograms;

  const merged = [...basePrograms];
  for (const entry of catalog.entries) {
    const zoneId = Number(catalog.deviceToZoneId.get(entry.deviceId) || 0);
    const code = zoneId - VIRTUAL_PLUG_ZONE_BASE;
    if (!Number.isInteger(code) || code <= 0) continue;

    const plugPrograms = Array.isArray(entry.state?.programs) ? entry.state.programs : [];
    for (let idx = 0; idx < plugPrograms.length; idx += 1) {
      const src = (plugPrograms[idx] && typeof plugPrograms[idx] === "object") ? plugPrograms[idx] : {};
      const out = { ...src };
      out.id = encodeVirtualProgramId(code, idx);
      out.zone = zoneId;
      merged.push(out);
    }
  }
  return merged;
}

function makePlugProgramPayload(program) {
  const src = (program && typeof program === "object") ? program : {};
  const out = { ...src, zone: 0 };
  delete out.id;
  delete out.lastRun;
  delete out.last_run;
  return out;
}

function ensureWateringCollections(db) {
  if (!Array.isArray(db.wateringSessions)) db.wateringSessions = [];
  if (!db.wateringOpenSessions || typeof db.wateringOpenSessions !== "object" || Array.isArray(db.wateringOpenSessions)) {
    db.wateringOpenSessions = {};
  }
  if (!db.wateringPendingStarts || typeof db.wateringPendingStarts !== "object" || Array.isArray(db.wateringPendingStarts)) {
    db.wateringPendingStarts = {};
  }
}

function prunePendingSmartClimateStarts(db, nowMs = Date.now()) {
  ensureWateringCollections(db);
  let removed = 0;
  for (const [deviceId, pending] of Object.entries(db.wateringPendingStarts || {})) {
    const createdMs = isoToMs(pending?.createdAt);
    const zoneId = Number(pending?.zoneId);
    const stale = !Number.isFinite(createdMs) || nowMs - createdMs > SMART_CLIMATE_PENDING_START_MAX_AGE_MS;
    if (!deviceId || stale || !Number.isInteger(zoneId) || zoneId < 0) {
      delete db.wateringPendingStarts[deviceId];
      removed += 1;
    }
  }
  return removed;
}

function registerPendingSmartClimateStart(db, deviceId, payload = {}) {
  ensureWateringCollections(db);
  const did = String(deviceId || "").trim();
  const zoneId = Number(payload.zoneId);
  if (!did || !Number.isInteger(zoneId) || zoneId < 0) return;
  const nowIso = new Date().toISOString();
  db.wateringPendingStarts[did] = {
    createdAt: nowIso,
    source: String(payload.source || "smart_climate"),
    zoneId,
    requestedSec: Math.max(0, Number(payload.requestedSec || 0) || 0),
    commandId: String(payload.commandId || ""),
    ackStatus: String(payload.ackStatus || ""),
  };
}

function consumePendingSmartClimateStart(db, deviceId, activeZoneIds = [], nowMs = Date.now()) {
  ensureWateringCollections(db);
  prunePendingSmartClimateStarts(db, nowMs);
  const did = String(deviceId || "").trim();
  if (!did) return null;
  const pending = db.wateringPendingStarts[did];
  if (!pending || typeof pending !== "object") return null;
  const zoneId = Number(pending.zoneId);
  if (!Number.isInteger(zoneId) || zoneId < 0) {
    delete db.wateringPendingStarts[did];
    return null;
  }
  const ids = Array.isArray(activeZoneIds)
    ? activeZoneIds.map((v) => Number(v)).filter((v) => Number.isInteger(v) && v >= 0)
    : [];
  if (ids.length > 0 && !ids.includes(zoneId)) {
    return null;
  }
  delete db.wateringPendingStarts[did];
  return {
    source: String(pending.source || "smart_climate"),
    zoneId,
    requestedSec: Math.max(0, Number(pending.requestedSec || 0) || 0),
    commandId: String(pending.commandId || ""),
    ackStatus: String(pending.ackStatus || ""),
  };
}

function pruneWateringSessions(db) {
  ensureWateringCollections(db);
  const minEndMs = Date.now() - WATERING_RETENTION_DAYS * 24 * 60 * 60 * 1000;
  db.wateringSessions = db.wateringSessions
    .filter((item) => item && typeof item === "object")
    .filter((item) => {
      const startedAt = Date.parse(item.startedAt);
      const endedAt = Date.parse(item.endedAt);
      if (!Number.isFinite(startedAt) || !Number.isFinite(endedAt) || endedAt <= startedAt) return false;
      if (endedAt < minEndMs) return false;
      return true;
    })
    .slice(-WATERING_SESSIONS_LIMIT);
}

function countActiveZones(zones) {
  if (!Array.isArray(zones)) return 0;
  let count = 0;
  for (const zone of zones) {
    if (zoneIsActive(zone)) count += 1;
  }
  return count;
}

function findManagedSmartClimateZoneConfig(db, deviceId, zoneId) {
  const cfg = db?.devices?.[deviceId]?.smartClimate;
  if (!cfg || typeof cfg !== "object" || cfg.enabled !== true) return null;
  const zones = (cfg.zones && typeof cfg.zones === "object") ? cfg.zones : {};
  const zoneCfg = zones[String(zoneId)];
  if (!zoneCfg || typeof zoneCfg !== "object" || zoneCfg.enabled !== true) return null;
  return { cfg, zoneCfg };
}

function applySmartClimateSessionAccounting(db, session) {
  const deviceId = String(session?.deviceId || "").trim();
  const startedBy = String(session?.startedBy || "").toLowerCase();
  const zoneId = Number(session?.smartClimateZoneId);
  if (!deviceId || startedBy !== "smart_climate" || !Number.isInteger(zoneId) || zoneId < 0) return false;

  const hit = findManagedSmartClimateZoneConfig(db, deviceId, zoneId);
  if (!hit) return false;

  const durationSec = Math.max(1, Number(session?.durationSec || 0) || 0);
  const runtimeMin = Math.max(1, Math.round(durationSec / 60));
  const precipMmPerHour = clampNumber(hit.zoneCfg.precipitationMmPerHour, 1, 80);
  const appliedMm = (durationSec / 3600) * precipMmPerHour * 0.85;
  const prevDeficit = Math.max(0, safeNumber(hit.zoneCfg.deficitMm, 0));
  const nextDeficit = Math.max(0, prevDeficit - appliedMm);

  hit.zoneCfg.deficitMm = nextDeficit;
  hit.zoneCfg.lastRuntimeMin = runtimeMin;
  hit.zoneCfg.lastRunAt = String(session.endedAt || session.startedAt || new Date().toISOString());
  hit.zoneCfg.commandPendingUntil = "";
  hit.zoneCfg.lastReason = normalizeSmartClimateReasonText(
    `rozliczono realne podlewanie (${runtimeMin} min, ${appliedMm.toFixed(2)} mm)`
  );
  hit.cfg.lastEngineError = "";

  appendSmartClimateHistory(hit.cfg, {
    ts: new Date().toISOString(),
    level: "info",
    action: "run_zone_accounted",
    zoneId,
    runtimeMin,
    appliedMm: Number(appliedMm.toFixed(2)),
    deficitBeforeMm: Number(prevDeficit.toFixed(2)),
    deficitAfterMm: Number(nextDeficit.toFixed(2)),
  });

  return true;
}

function closeOpenWateringSession(db, deviceId, endedAtIso, closedBy = "zones") {
  ensureWateringCollections(db);
  prunePendingSmartClimateStarts(db);
  const open = db.wateringOpenSessions[deviceId];
  if (!open || typeof open !== "object") return false;

  delete db.wateringOpenSessions[deviceId];
  const startMs = Date.parse(open.startedAt);
  const endMs = Date.parse(endedAtIso);
  if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs) return false;

  const durationSec = Math.max(1, Math.round((endMs - startMs) / 1000));
  const activeZoneIds = Array.isArray(open.activeZoneIds)
    ? open.activeZoneIds.map((v) => Number(v)).filter((v) => Number.isInteger(v) && v >= 0).slice(0, 16)
    : [];
  const session = {
    id: `ws_${endMs}_${Math.floor(Math.random() * 100000)}`,
    deviceId,
    startedAt: new Date(startMs).toISOString(),
    endedAt: new Date(endMs).toISOString(),
    durationSec,
    maxActiveZones: Number.isFinite(Number(open.maxActiveZones)) ? Math.max(1, Number(open.maxActiveZones)) : 1,
    startedBy: String(open.startedBy || "zones"),
    activeZoneIds,
    smartClimateZoneId: Number.isInteger(Number(open.smartClimateZoneId)) && Number(open.smartClimateZoneId) >= 0
      ? Number(open.smartClimateZoneId)
      : null,
    requestedSec: Math.max(0, Number(open.requestedSec || 0) || 0),
    commandId: String(open.commandId || ""),
    ackStatus: String(open.ackStatus || ""),
    closedBy: String(closedBy || "zones"),
  };
  db.wateringSessions.push(session);
  applySmartClimateSessionAccounting(db, session);
  pruneWateringSessions(db);
  return true;
}

function trackWateringFromZonesUpdate(db, deviceId, prevZones, nextZones, eventTsIso) {
  ensureWateringCollections(db);
  const eventMs = isoToMs(eventTsIso);
  const nowMs = Number.isFinite(eventMs) ? eventMs : Date.now();
  prunePendingSmartClimateStarts(db, nowMs);
  const prevActiveCount = countActiveZones(prevZones);
  const nextActiveCount = countActiveZones(nextZones);
  const nextActiveZoneIds = getActiveZoneIds(nextZones);
  const wasActive = prevActiveCount > 0;
  const isActive = nextActiveCount > 0;
  const open = db.wateringOpenSessions[deviceId];

  if (!wasActive && isActive) {
    const pending = consumePendingSmartClimateStart(db, deviceId, nextActiveZoneIds, nowMs);
    db.wateringOpenSessions[deviceId] = {
      startedAt: eventTsIso,
      startedBy: pending?.source || "zones",
      maxActiveZones: Math.max(1, nextActiveCount),
      activeZoneIds: nextActiveZoneIds,
      smartClimateZoneId: Number.isInteger(Number(pending?.zoneId)) ? Number(pending.zoneId) : null,
      requestedSec: Math.max(0, Number(pending?.requestedSec || 0) || 0),
      commandId: String(pending?.commandId || ""),
      ackStatus: String(pending?.ackStatus || ""),
    };
    return;
  }

  if (wasActive && !isActive) {
    closeOpenWateringSession(db, deviceId, eventTsIso, "zones");
    return;
  }

  if (isActive) {
    if (!open || typeof open !== "object") {
      const pending = consumePendingSmartClimateStart(db, deviceId, nextActiveZoneIds, nowMs);
      db.wateringOpenSessions[deviceId] = {
        startedAt: eventTsIso,
        startedBy: pending?.source || "recovered",
        maxActiveZones: Math.max(1, nextActiveCount),
        activeZoneIds: nextActiveZoneIds,
        smartClimateZoneId: Number.isInteger(Number(pending?.zoneId)) ? Number(pending.zoneId) : null,
        requestedSec: Math.max(0, Number(pending?.requestedSec || 0) || 0),
        commandId: String(pending?.commandId || ""),
        ackStatus: String(pending?.ackStatus || ""),
      };
      return;
    }
    if (!Number.isFinite(Number(open.maxActiveZones)) || Number(open.maxActiveZones) < nextActiveCount) {
      open.maxActiveZones = Math.max(1, nextActiveCount);
    }
    if ((!open.startedBy || open.startedBy === "zones" || open.startedBy === "recovered")
      && !Number.isInteger(Number(open.smartClimateZoneId))) {
      const pending = consumePendingSmartClimateStart(db, deviceId, nextActiveZoneIds, nowMs);
      if (pending) {
        open.startedBy = pending.source || "smart_climate";
        open.smartClimateZoneId = Number.isInteger(Number(pending.zoneId)) ? Number(pending.zoneId) : null;
        open.requestedSec = Math.max(0, Number(pending.requestedSec || 0) || 0);
        open.commandId = String(pending.commandId || "");
        open.ackStatus = String(pending.ackStatus || "");
      }
    }
    open.activeZoneIds = nextActiveZoneIds;
    return;
  }

  if (!isActive && open) {
    closeOpenWateringSession(db, deviceId, eventTsIso, "cleanup");
  }
}

function parseMonthFilter(rawMonth) {
  const match = /^(\d{4})-(\d{2})$/.exec(String(rawMonth || "").trim());
  if (match) {
    const year = Number(match[1]);
    const month = Number(match[2]);
    if (year >= 2000 && year <= 2100 && month >= 1 && month <= 12) {
      return { year, month };
    }
  }
  const now = new Date();
  return {
    year: now.getFullYear(),
    month: now.getMonth() + 1,
  };
}

function localDateKey(tsMs) {
  const d = new Date(tsMs);
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, "0");
  const day = String(d.getDate()).padStart(2, "0");
  return `${y}-${m}-${day}`;
}

function nextLocalDayStartMs(tsMs) {
  const d = new Date(tsMs);
  d.setHours(24, 0, 0, 0);
  return d.getTime();
}

function monthRangeMs(year, month) {
  const start = new Date(year, month - 1, 1, 0, 0, 0, 0);
  const end = new Date(year, month, 1, 0, 0, 0, 0);
  return {
    mode: "month",
    label: `${year}-${String(month).padStart(2, "0")}`,
    month: `${year}-${String(month).padStart(2, "0")}`,
    startMs: start.getTime(),
    endMs: end.getTime(),
    startIso: start.toISOString(),
    endIso: end.toISOString(),
  };
}

function yearRangeMs(year) {
  const start = new Date(year, 0, 1, 0, 0, 0, 0);
  const end = new Date(year + 1, 0, 1, 0, 0, 0, 0);
  return {
    mode: "year",
    label: String(year),
    startMs: start.getTime(),
    endMs: end.getTime(),
    startIso: start.toISOString(),
    endIso: end.toISOString(),
  };
}

function allWateringRangeMs(db, fallbackYear) {
  const sessions = Array.isArray(db?.wateringSessions) ? db.wateringSessions : [];
  const openSessions = db?.wateringOpenSessions && typeof db.wateringOpenSessions === "object"
    ? db.wateringOpenSessions
    : {};
  const nowMs = Date.now();
  let minMs = NaN;
  let maxMs = NaN;

  const includeRange = (startMs, endMs) => {
    if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs) return;
    if (!Number.isFinite(minMs) || startMs < minMs) minMs = startMs;
    if (!Number.isFinite(maxMs) || endMs > maxMs) maxMs = endMs;
  };

  for (const session of sessions) {
    includeRange(Date.parse(session?.startedAt), Date.parse(session?.endedAt));
  }
  for (const open of Object.values(openSessions)) {
    includeRange(Date.parse(open?.startedAt), nowMs);
  }

  if (!Number.isFinite(minMs) || !Number.isFinite(maxMs)) {
    const fallback = yearRangeMs(fallbackYear);
    return { ...fallback, mode: "all", label: fallback.label };
  }

  const startYear = new Date(minMs).getFullYear();
  const endYear = new Date(maxMs).getFullYear();
  const start = new Date(startYear, 0, 1, 0, 0, 0, 0);
  const end = new Date(endYear + 1, 0, 1, 0, 0, 0, 0);
  return {
    mode: "all",
    label: startYear === endYear ? String(startYear) : `${startYear}-${endYear}`,
    startMs: start.getTime(),
    endMs: end.getTime(),
    startIso: start.toISOString(),
    endIso: end.toISOString(),
  };
}

function normalizeWateringStatsRangeMode(rawMode) {
  const mode = String(rawMode || "").trim().toLowerCase();
  if (mode === "year") return "year";
  if (mode === "all") return "all";
  return "month";
}

function resolveWateringStatsRange(db, monthParsed, rawMode) {
  const mode = normalizeWateringStatsRangeMode(rawMode);
  if (mode === "year") return yearRangeMs(monthParsed.year);
  if (mode === "all") return allWateringRangeMs(db, monthParsed.year);
  return monthRangeMs(monthParsed.year, monthParsed.month);
}

function addRangeToDailyBuckets(dayBucketsMs, startMs, endMs) {
  let cursor = startMs;
  while (cursor < endMs) {
    const nextDayMs = nextLocalDayStartMs(cursor);
    const chunkEnd = Math.min(endMs, nextDayMs);
    const key = localDateKey(cursor);
    dayBucketsMs.set(key, (dayBucketsMs.get(key) || 0) + (chunkEnd - cursor));
    cursor = chunkEnd;
  }
}

function buildWateringStats(db, options = {}) {
  ensureWateringCollections(db);
  pruneWateringSessions(db);

  const monthParsed = parseMonthFilter(options.month);
  const anchorMonth = `${monthParsed.year}-${String(monthParsed.month).padStart(2, "0")}`;
  const range = resolveWateringStatsRange(db, monthParsed, options.rangeMode);
  const selectedDeviceId = String(options.deviceId || "").trim();
  const deviceRows = buildDeviceRows(db);
  const knownDevices = new Map(deviceRows.map((d) => [d.deviceId, d]));
  const nowMs = Date.now();
  const rowsByDevice = new Map();

  const ensureRow = (deviceId) => {
    if (!rowsByDevice.has(deviceId)) {
      const meta = knownDevices.get(deviceId) || {};
      rowsByDevice.set(deviceId, {
        deviceId,
        ownerEmail: String(meta.ownerEmail || "-"),
        hardware: String(meta.hardware || "unknown"),
        sessions: 0,
        totalMs: 0,
        maxMs: 0,
        lastWateringMs: 0,
        dayBucketsMs: new Map(),
        activeNow: false,
      });
    }
    return rowsByDevice.get(deviceId);
  };

  const addSession = (deviceId, startMs, endMs, activeNow = false) => {
    if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs <= startMs) return;
    if (selectedDeviceId && selectedDeviceId !== deviceId) return;
    const clippedStart = Math.max(startMs, range.startMs);
    const clippedEnd = Math.min(endMs, range.endMs);
    if (clippedEnd <= clippedStart) return;
    const row = ensureRow(deviceId);
    row.sessions += 1;
    row.totalMs += clippedEnd - clippedStart;
    row.maxMs = Math.max(row.maxMs, clippedEnd - clippedStart);
    row.lastWateringMs = Math.max(row.lastWateringMs, clippedEnd);
    if (activeNow) row.activeNow = true;
    addRangeToDailyBuckets(row.dayBucketsMs, clippedStart, clippedEnd);
  };

  for (const session of db.wateringSessions) {
    const deviceId = String(session?.deviceId || "").trim();
    if (!deviceId) continue;
    const startMs = Date.parse(session.startedAt);
    const endMs = Date.parse(session.endedAt);
    addSession(deviceId, startMs, endMs, false);
  }

  for (const [deviceId, open] of Object.entries(db.wateringOpenSessions || {})) {
    if (!open || typeof open !== "object") continue;
    const startMs = Date.parse(open.startedAt);
    addSession(String(deviceId || "").trim(), startMs, nowMs, true);
  }

  const rows = Array.from(rowsByDevice.values()).map((row) => {
    const totalSec = Math.max(0, Math.round(row.totalMs / 1000));
    const maxSec = Math.max(0, Math.round(row.maxMs / 1000));
    return {
      deviceId: row.deviceId,
      ownerEmail: row.ownerEmail,
      hardware: row.hardware,
      sessions: row.sessions,
      totalSec,
      avgSec: row.sessions > 0 ? Math.max(0, Math.round(totalSec / row.sessions)) : 0,
      maxSec,
      lastWateringAt: row.lastWateringMs > 0 ? new Date(row.lastWateringMs).toISOString() : null,
      activeNow: row.activeNow,
      dayBucketsSec: Array.from(row.dayBucketsMs.entries())
        .map(([date, ms]) => ({ date, durationSec: Math.max(0, Math.round(ms / 1000)) }))
        .sort((a, b) => a.date.localeCompare(b.date)),
    };
  });

  rows.sort((a, b) => {
    if (b.totalSec !== a.totalSec) return b.totalSec - a.totalSec;
    return a.deviceId.localeCompare(b.deviceId, "pl");
  });

  const summary = {
    devices: rows.length,
    sessions: rows.reduce((acc, row) => acc + row.sessions, 0),
    totalSec: rows.reduce((acc, row) => acc + row.totalSec, 0),
    maxSec: rows.reduce((acc, row) => Math.max(acc, row.maxSec), 0),
  };
  summary.avgSec = summary.sessions > 0 ? Math.max(0, Math.round(summary.totalSec / summary.sessions)) : 0;

  let selectedRow = null;
  if (selectedDeviceId) {
    selectedRow = rows.find((row) => row.deviceId === selectedDeviceId) || null;
  } else if (rows.length > 0) {
    selectedRow = rows[0];
  }

  const dailySeries = [];
  if (selectedRow) {
    const byDay = new Map((selectedRow.dayBucketsSec || []).map((x) => [x.date, x.durationSec]));
    let cursor = range.startMs;
    while (cursor < range.endMs) {
      const key = localDateKey(cursor);
      dailySeries.push({ date: key, durationSec: Number(byDay.get(key) || 0) });
      cursor = nextLocalDayStartMs(cursor);
    }
  }

  return {
    month: anchorMonth,
    range: {
      mode: range.mode,
      label: range.label,
      start: range.startIso,
      end: range.endIso,
    },
    selectedDeviceId: selectedRow?.deviceId || null,
    summary,
    rows,
    dailySeries,
    availableDevices: deviceRows.map((d) => ({
      deviceId: d.deviceId,
      ownerEmail: d.ownerEmail,
      hardware: d.hardware,
    })),
  };
}

function csvEscape(value) {
  const raw = String(value ?? "");
  if (/[",\r\n]/.test(raw)) return `"${raw.replace(/"/g, "\"\"")}"`;
  return raw;
}

function durationToHhMm(totalSec) {
  const sec = Math.max(0, Number(totalSec || 0));
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}`;
}

function buildWateringSummaryCsv(stats) {
  const lines = [];
  lines.push(["Miesiąc", stats.month]);
  lines.push(["Liczba urządzeń", stats.summary.devices]);
  lines.push(["Liczba sesji", stats.summary.sessions]);
  lines.push(["Łączny czas (sek)", stats.summary.totalSec]);
  lines.push(["Średnia sesja (sek)", stats.summary.avgSec]);
  lines.push([]);
  lines.push([
    "Urządzenie",
    "Właściciel",
    "Sesje",
    "Łączny czas (sek)",
    "Łączny czas (HH:MM)",
    "Średnia sesja (sek)",
    "Maksymalna sesja (sek)",
    "Ostatnie podlewanie",
    "Aktywne teraz",
  ]);
  for (const row of stats.rows) {
    lines.push([
      row.deviceId,
      row.ownerEmail,
      row.sessions,
      row.totalSec,
      durationToHhMm(row.totalSec),
      row.avgSec,
      row.maxSec,
      row.lastWateringAt || "",
      row.activeNow ? "tak" : "nie",
    ]);
  }
  return lines.map((cols) => cols.map(csvEscape).join(",")).join("\n");
}

function buildWateringDailyCsv(stats, deviceId) {
  const selected = stats.rows.find((row) => row.deviceId === deviceId);
  const lines = [];
  lines.push(["Miesiąc", stats.month]);
  lines.push(["Urządzenie", deviceId || ""]);
  lines.push(["Właściciel", selected?.ownerEmail || ""]);
  lines.push([]);
  lines.push(["Data", "Czas podlewania (sek)", "Czas podlewania (HH:MM)"]);
  for (const day of stats.dailySeries || []) {
    lines.push([day.date, day.durationSec, durationToHhMm(day.durationSec)]);
  }
  return lines.map((cols) => cols.map(csvEscape).join(",")).join("\n");
}

function sanitizeVersion(v) {
  // Make version safe for URLs/filenames, but be forgiving: users often paste
  // full names with spaces or extra chars. We normalize rather than reject.
  const raw = String(v || "").trim();
  if (!raw) return null;
  const s = raw
    .replace(/\s+/g, "_")
    .replace(/[^0-9A-Za-z._-]/g, "_")
    .replace(/_+/g, "_")
    .replace(/^_+|_+$/g, "")
    .slice(0, 80);
  if (!s) return null;
  if (!/^[0-9A-Za-z._-]{1,80}$/.test(s)) return null;
  return s;
}

function sanitizeChannel(ch) {
  const s = String(ch || "stable").trim().toLowerCase();
  return s === "beta" ? "beta" : "stable";
}

function normalizeHardwareId(raw) {
  const s = String(raw || "").trim().toLowerCase();
  if (!s) return "";
  if (s === "esp32c6" || s === "esp32-c6" || s === "c6") return "esp32c6";
  if (s === "esp32" || s === "esp-32" || s === "esp32dev" || s === "classic") return "esp32";
  if (s === "sonoff4ch" || s === "sonoff-4ch" || s === "sonoff_4ch" || s === "sonoff 4ch" || s === "sonof4ch" || s === "sonof-4ch") return "sonoff4ch";
  if (s === "bwshp6" || s === "bw-shp6" || s === "bw_shp6" || s === "shp6" || s === "blitzwolf-shp6") return "bwshp6";
  return "";
}

function hardwareLabel(id) {
  const hw = normalizeHardwareId(id);
  if (hw === "esp32c6") return "ESP32-C6";
  if (hw === "esp32") return "ESP32";
  if (hw === "sonoff4ch") return "SONOFF 4CH";
  if (hw === "bwshp6") return "BW-SHP6 (ESP8266)";
  return "unknown";
}

function inferHardwareFromText(value) {
  const s = String(value || "").toLowerCase();
  if (!s) return "";
  if (/(^|[^a-z0-9])(bw|blitzwolf)[\s._-]*shp[\s._-]*6([^a-z0-9]|$)/.test(s)) return "bwshp6";
  if (/(^|[^a-z0-9])sono?ff?[\s._-]*4ch([^a-z0-9]|$)/.test(s)) return "sonoff4ch";
  if (/(^|[^a-z0-9])esp32[\s._-]*c6([^a-z0-9]|$)/.test(s)) return "esp32c6";
  if (/(^|[^a-z0-9])esp32([^a-z0-9]|$)/.test(s)) return "esp32";
  return "";
}

function inferHardwareFromStatus(status) {
  if (!status || typeof status !== "object") return "";
  const direct = normalizeHardwareId(
    status.hardware ||
    status.hw ||
    status.chip ||
    status.chip_id ||
    status.platform ||
    status.board ||
    status.mcu ||
    status.soc ||
    status.device_type
  );
  if (direct) return direct;
  return inferHardwareFromText(status.model);
}

function normalizeBssid(raw) {
  const compact = String(raw || "").trim().toLowerCase().replace(/[^0-9a-f]/g, "");
  return compact.length === 12 ? compact : "";
}

function normalizeIpv4(raw) {
  const text = String(raw || "").trim();
  const m = text.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);
  if (!m) return "";
  const parts = m.slice(1).map((p) => Number(p));
  if (parts.some((p) => !Number.isInteger(p) || p < 0 || p > 255)) return "";
  return `${parts[0]}.${parts[1]}.${parts[2]}.${parts[3]}`;
}

function ipv4Subnet24(raw) {
  const ip = normalizeIpv4(raw);
  if (!ip) return "";
  const parts = ip.split(".");
  return `${parts[0]}.${parts[1]}.${parts[2]}`;
}

function isLegacyCompatModeActive(nowMs = Date.now()) {
  const untilMs = Date.parse(LEGACY_COMPAT_MODE_UNTIL);
  return Number.isFinite(untilMs) && nowMs <= untilMs;
}

function networkIdentityFromState(state) {
  const status = (state?.status && typeof state.status === "object") ? state.status : {};
  const settingsPublic = (state?.settingsPublic && typeof state.settingsPublic === "object")
    ? state.settingsPublic
    : {};
  const bssid = normalizeBssid(
    status.bssid ||
    status.wifi_bssid ||
    status.ap_bssid ||
    settingsPublic.bssid ||
    settingsPublic.wifiBssid ||
    settingsPublic.apBssid
  );
  const ssid = String(
    status.ssid ||
    status.wifi_ssid ||
    settingsPublic.ssid ||
    settingsPublic.wifiSsid ||
    ""
  ).trim();
  const gateway = String(
    status.gateway ||
    status.wifi_gateway ||
    settingsPublic.gateway ||
    settingsPublic.wifiGateway ||
    ""
  ).trim();
  return { bssid, ssid, gateway };
}

function inferHardwareFromFirmwareMeta(meta) {
  if (!meta || typeof meta !== "object") return "";
  const direct = normalizeHardwareId(meta.hardware);
  if (direct) return direct;
  const merged = [
    meta.fileName,
    meta.storedAs,
    meta.notes,
    meta.version,
  ].map((x) => String(x || "")).join(" ");
  return inferHardwareFromText(merged);
}

function sanitizeOtaTarget(target) {
  const s = String(target || "firmware").trim().toLowerCase();
  if (s === "fs" || s === "littlefs" || s === "spiffs" || s === "filesystem") return "fs";
  return "firmware";
}

function shouldHideCloudLogLine(line) {
  const text = String(line || "")
    .replace(/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}:\s*/, "")
    .trim();
  if (!text) return false;
  if (/^MQTT CMD\b/i.test(text)) return false;
  // Historyczny, techniczny format wyliczeń auto-podlewania.
  // W UI zostawiamy czytelne wpisy typu "Automat: Start strefy ...".
  if (/^Automat:\s*Strefa\s+\d+:\s*bazowo\b.*\|\s*AUTO:/i.test(text)) return true;
  return /^MQTT\b/i.test(text);
}

function normalizeLegacyLogObjectLabel(value) {
  return String(value || "").replace(/ \(#/g, " (").replace(/#/g, "").trim();
}

function legacyLogField(text, key) {
  const match = String(text || "").match(new RegExp(`${key}=([^|]+)`));
  return match ? String(match[1] || "").trim() : "";
}

function formatMinutesPl(value) {
  const total = Math.max(0, Math.round(Number(value || 0)));
  if (!Number.isFinite(total)) return "";
  const hours = Math.floor(total / 60);
  const minutes = total % 60;
  if (hours > 0 && minutes > 0) return `${hours} h ${minutes} min`;
  if (hours > 0) return `${hours} h`;
  return `${minutes} min`;
}

function normalizeSmartClimateReasonText(value) {
  const raw = String(value || "").trim();
  if (!raw) return "";

  const lower = raw.toLowerCase();
  const metric = (regex) => {
    const match = raw.match(regex);
    return match ? String(match[1] || "").replace(",", ".").trim() : "";
  };
  const mm = metric(/(\d+(?:[.,]\d+)?)\s*mm/i);
  const kmh = metric(/(\d+(?:[.,]\d+)?)\s*km\/h/i);
  const mins = metric(/(\d+)\s*min/i);

  if (lower.includes("przymro") || lower.includes("mroz")) return "ryzyko przymrozku";
  if (lower.includes("niska temperatura")) return "niska temperatura";
  if (lower.includes("wysoka temperatura") || lower.includes("upal")) return "upał";
  if (lower.includes("deficyt ponizej progu") || lower.includes("deficyt poniżej progu")) return "deficyt poniżej progu";
  if (lower.includes("okno czasowe") || lower.includes("okno podlewania")) return "czeka na okno podlewania";
  if (lower.includes("prognoza") && (lower.includes("deszcz") || lower.includes("opad"))) {
    return mm ? `prognoza deszczu (${mm} mm / 24 h)` : "prognoza deszczu";
  }
  if (lower.includes("ulew") || lower.includes("mocne opady") || lower.includes("mocny deszcz")) {
    return mm ? `mocny deszcz (${mm} mm / 24 h)` : "mocny deszcz";
  }
  if (lower.includes("deszcz") || lower.includes("opad")) {
    return mm ? `opady (${mm} mm)` : "opady";
  }
  if (lower.includes("wiatr")) {
    return kmh ? `silny wiatr (${kmh} km/h)` : "silny wiatr";
  }
  if (
    lower.includes("za krótki odstęp") ||
    lower.includes("za krotki odstep") ||
    lower.includes("ostatniego cyklu") ||
    lower.includes("kolejnego uruchomienia")
  ) {
    if (lower.includes("kolejnego uruchomienia")) {
      return raw.charAt(0).toLowerCase() + raw.slice(1);
    }
    const inParens = raw.match(/\(([^)]+)\)/);
    const duration = inParens ? String(inParens[1] || "").trim() : "";
    return duration
      ? `pozostało ${duration} do kolejnego uruchomienia`
      : "pozostało do kolejnego uruchomienia";
  }
  if (lower.includes("offline")) return "urządzenie offline";
  if (lower.includes("inna strefa")) return "inna strefa już pracuje";
  if (lower.includes("brak świeżych danych pogodowych") || lower.includes("brak swiezych danych pogodowych")) {
    return mins ? `brak świeżych danych pogodowych (${mins} min)` : "brak świeżych danych pogodowych";
  }
  if (lower.includes("brak świeżej historii opadów") || lower.includes("brak swiezej historii opadow")) {
    return mins ? `brak świeżej historii opadów (${mins} min)` : "brak świeżej historii opadów";
  }
  if (lower.includes("wysłano start") || lower.includes("wyslano start")) {
    const suffix = mins ? ` (${mins} min)` : "";
    if (lower.includes("ack timeout") || lower.includes("brak ack")) {
      return `polecenie startu wysłane - brak ACK, czekam na potwierdzenie${suffix}`;
    }
    return `polecenie startu wysłane - oczekiwanie na potwierdzenie${suffix}`;
  }
  if (lower.includes("rozliczono realne podlewanie") || lower.includes("realne podlewanie")) {
    if (mins && mm) return `podlano ${mins} min (${mm} mm)`;
    if (mins) return `podlano ${mins} min`;
    return "podlewanie zakończone";
  }
  if (lower.includes("błąd uruchomienia") || lower.includes("blad uruchomienia")) {
    return raw.charAt(0).toLowerCase() + raw.slice(1);
  }

  return raw.charAt(0).toLowerCase() + raw.slice(1);
}

function simplifyHumanSchedulerLine(text) {
  const line = String(text || "").trim();
  if (!line) return line;

  // Przykład wejścia:
  // Harmonogram: strefa 3 (06:00) NIE uruchomiona. Powód=temp_below_threshold (Temperatura poniżej progu). 24h=...
  const skippedMatch = line.match(
    /^Harmonogram:\s*strefa\s+(\d+)\s+\((\d{2}:\d{2})\)\s+NIE uruchomiona\.\s*Pow[oó]d=([^.\s(]+)(?:\s*\(([^)]+)\))?/i
  );
  if (!skippedMatch) return line;

  const zoneNo = skippedMatch[1];
  const startAt = skippedMatch[2];
  const reasonFromParen = String(skippedMatch[4] || "").trim();
  const reasonCode = String(skippedMatch[3] || "").trim().replace(/_/g, " ");
  const reasonRaw = reasonFromParen || reasonCode;
  let reason = normalizeSmartClimateReasonText(reasonRaw);
  if (!reason) reason = reasonRaw;
  reason = String(reason || "").trim();
  if (!reason) reason = "brak powodu";
  reason = reason.charAt(0).toUpperCase() + reason.slice(1);

  return `Harmonogram: strefa ${zoneNo} (${startAt}) NIE uruchomiona. ${reason}.`;
}

function transformCloudLogLine(line) {
  const raw = String(line || "").trim();
  if (!raw) return raw;

  let prefix = "";
  let text = raw;
  const isoMatch = raw.match(/^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}):\s*(.*)$/);
  if (isoMatch) {
    prefix = `${isoMatch[1]}: `;
    text = String(isoMatch[2] || "").trim();
  } else {
    text = raw.replace(/^uptime\+\d+s:\s*/i, "").trim();
  }

  const simplifiedSchedulerLine = simplifyHumanSchedulerLine(text);
  if (simplifiedSchedulerLine !== text) {
    return `${prefix}${simplifiedSchedulerLine}`;
  }

  if (text.includes(" | ")) {
    const action = text.replace(/^\[[^\]]+\]\s*/, "").split("|")[0].trim();
    const object = normalizeLegacyLogObjectLabel(legacyLogField(text, "obiekt"));
    const minutes = legacyLogField(text, "czas") || legacyLogField(text, "wykonano");
    const detail = normalizeSmartClimateReasonText(legacyLogField(text, "szczegoly") || legacyLogField(text, "powod"));
    const categoryMatch = text.match(/^\[([^\]]+)\]/);
    const category = categoryMatch ? String(categoryMatch[1] || "").trim().toUpperCase() : "";

    if ((category === "STREFA" || category === "WMSC") && action === "start" && object) {
      return `${prefix}${object} uruchomiono${minutes ? ` na ${minutes}` : ""}.`;
    }
    if ((category === "STREFA" || category === "WMSC") && action === "stop" && object) {
      return `${prefix}${object} wyłączono.`;
    }
    if ((category === "STREFA" || category === "WMSC") && action === "aktualizacja" && object) {
      return `${prefix}${object} zaktualizowano${minutes ? ` do ${minutes}` : ""}.`;
    }
    if (category === "HARMONOGRAM" && action === "pominieto" && object) {
      return `${prefix}${object} anulowano${detail ? ` - ${detail}` : ""}.`;
    }
    if (category === "HARMONOGRAM" && (action === "przeliczono" || action === "start") && object) {
      return `${prefix}${object} uruchomiono${minutes ? ` na ${minutes}` : ""}.`;
    }
    if (category === "GNIAZDKO") {
      const plug = legacyLogField(text, "obiekt");
      if (action === "start" || /włącz/i.test(action)) return `${prefix}${plug} włączono${minutes ? ` na ${minutes}` : ""}.`;
      if (action === "stop" || /wyłącz/i.test(action)) return `${prefix}${plug} wyłączono.`;
    }
    if (category === "OTA") {
      const targetRaw = legacyLogField(text, "target");
      const version = legacyLogField(text, "wersja");
      const result = legacyLogField(text, "wynik");
      const reason = legacyLogField(text, "powod");
      const extra = legacyLogField(text, "szczegoly");
      let target = targetRaw;
      if (target === "firmware") target = "firmware";
      else if (target === "fs") target = "plików WWW";
      else if (!target) target = "pakietu";

      if (action === "start") {
        return `${prefix}Rozpoczęto aktualizację OTA ${target}${version ? ` do wersji ${version}` : ""}.`;
      }
      if (action === "sukces") {
        return `${prefix}Aktualizacja OTA ${target} zakończona${version ? ` (${version})` : ""}${result ? ` - ${result}` : ""}.`;
      }
      if (action === "blad" || action === "błąd") {
        return `${prefix}Błąd OTA${reason ? ` - ${reason}` : ""}.`;
      }
      if (action === "blad backupu" || action === "błąd backupu") {
        return `${prefix}Błąd backupu OTA${reason ? ` - ${reason}` : ""}.`;
      }
      if (action === "ostrzezenie" || action === "ostrzeżenie") {
        return `${prefix}Ostrzeżenie OTA${reason ? ` - ${reason}` : ""}${extra ? `: ${extra}` : ""}.`;
      }
    }
  }

  return `${prefix}${text.replace(/^\[[^\]]+\]\s*/, "").trim()}`;
}

function buildLogDedupKey(line) {
  const text = String(line || "").trim();
  if (!text) return "";

  const tsMatch = text.match(/^(\d{4}-\d{2}-\d{2} \d{2}:\d{2})(?::\d{2})?:\s*(.*)$/);
  const minuteTag = tsMatch ? String(tsMatch[1] || "") : "";
  const body = tsMatch ? String(tsMatch[2] || "") : text;
  const normalizedBody = body
    .replace(/\s+/g, " ")
    .replace(/[.]+$/g, "")
    .trim()
    .toLowerCase();

  return `${minuteTag}|${normalizedBody}`;
}

function normalizeLogsPayload(logsPayload) {
  const base = logsPayload && typeof logsPayload === "object" ? logsPayload : {};
  const rawLogs = Array.isArray(base.logs) ? base.logs : [];
  const visibleLogs = rawLogs
    .filter((line) => !shouldHideCloudLogLine(line))
    .map((line) => transformCloudLogLine(line));
  if (visibleLogs.length <= 1) return { ...base, logs: visibleLogs };

  const sortedLogs = visibleLogs
    .map((line, index) => {
      const text = String(line || "");
      const match = text.match(/^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})/);
      return {
        line,
        index,
        ts: match ? match[1] : "",
      };
    })
    .sort((a, b) => {
      if (a.ts && b.ts && a.ts !== b.ts) return a.ts < b.ts ? 1 : -1;
      if (a.ts && !b.ts) return -1;
      if (!a.ts && b.ts) return 1;
      return a.index - b.index;
    })
    .map((entry) => entry.line);

  const dedupedLogs = [];
  const seen = new Set();
  for (const line of sortedLogs) {
    const key = buildLogDedupKey(line);
    if (!key) continue;
    if (seen.has(key)) continue;
    seen.add(key);
    dedupedLogs.push(line);
  }

  return { ...base, logs: dedupedLogs };
}

function normalizeHex(value, expectedLen) {
  const s = String(value || "").trim().toLowerCase();
  return new RegExp(`^[0-9a-f]{${expectedLen}}$`).test(s) ? s : "";
}

function isHardwareReportRequiredNow() {
  const ts = Date.parse(OTA_REQUIRE_HARDWARE_REPORT_FROM);
  if (!Number.isFinite(ts)) return true;
  return Date.now() >= ts;
}

function resolveOtaSignPrivateKeyPem() {
  if (OTA_SIGN_PRIVATE_KEY_PEM_B64) {
    try {
      return Buffer.from(OTA_SIGN_PRIVATE_KEY_PEM_B64, "base64").toString("utf8");
    } catch {
      throw new Error("Nieprawidłowe OTA_SIGN_PRIVATE_KEY_PEM_B64 (base64)");
    }
  }
  if (!OTA_SIGN_PRIVATE_KEY_PATH) {
    throw new Error("Brak OTA_SIGN_PRIVATE_KEY_PATH lub OTA_SIGN_PRIVATE_KEY_PEM_B64");
  }
  const keyPath = path.isAbsolute(OTA_SIGN_PRIVATE_KEY_PATH)
    ? OTA_SIGN_PRIVATE_KEY_PATH
    : path.join(__dirname, "..", OTA_SIGN_PRIVATE_KEY_PATH);
  if (!fs.existsSync(keyPath)) {
    throw new Error(`Nie znaleziono klucza OTA: ${keyPath}`);
  }
  return fs.readFileSync(keyPath, "utf8");
}

function getOtaSignPrivateKey() {
  if (otaSignPrivateKeyCache) return otaSignPrivateKeyCache;
  if (otaSignPrivateKeyError) throw new Error(otaSignPrivateKeyError);
  try {
    const pem = resolveOtaSignPrivateKeyPem();
    otaSignPrivateKeyCache = crypto.createPrivateKey(pem);
    return otaSignPrivateKeyCache;
  } catch (err) {
    otaSignPrivateKeyError = err?.message || "Nie udało się wczytać klucza OTA";
    throw new Error(otaSignPrivateKeyError);
  }
}

function signSha256Ed25519(sha256Hex) {
  const digestHex = normalizeHex(sha256Hex, 64);
  if (!digestHex) throw new Error("Nieprawidłowy SHA256 do podpisu OTA");
  const digest = Buffer.from(digestHex, "hex");
  const privateKey = getOtaSignPrivateKey();
  const signature = crypto.sign(null, digest, privateKey);
  const publicKey = crypto.createPublicKey(privateKey);
  if (!crypto.verify(null, digest, publicKey, signature)) {
    throw new Error("Weryfikacja własna podpisu OTA nie powiodła się");
  }
  return signature.toString("hex");
}

function signedOtaMetaFromBuffer(body, target) {
  const sha256 = crypto.createHash("sha256").update(body).digest("hex");
  return {
    target: sanitizeOtaTarget(target),
    size: body.length,
    sha256,
    signature: signSha256Ed25519(sha256),
    signature_alg: OTA_SIGNATURE_ALG,
    signedAt: new Date().toISOString(),
  };
}

function firmwareStoredPath(firmware) {
  const storedAs = path.basename(String(firmware?.storedAs || ""));
  if (!storedAs) throw new Error("Firmware nie ma pola storedAs");
  return path.join(FIRMWARE_DIR, storedAs);
}

function ensureFirmwareSigned(firmware) {
  if (!firmware || typeof firmware !== "object") {
    throw new Error("Nieprawidłowy rekord firmware");
  }
  const filePath = firmwareStoredPath(firmware);
  if (!fs.existsSync(filePath)) {
    throw new Error(`Brak pliku firmware: ${path.basename(filePath)}`);
  }

  let changed = false;
  const hardware = inferHardwareFromFirmwareMeta(firmware);
  if (hardware && String(firmware.hardware || "") !== hardware) {
    firmware.hardware = hardware;
    changed = true;
  }
  const target = sanitizeOtaTarget(firmware.target);
  if (String(firmware.target || "") !== target) {
    firmware.target = target;
    changed = true;
  }

  let sha256 = normalizeHex(firmware.sha256, 64);
  let size = Number(firmware.size);
  if (!sha256 || !Number.isFinite(size) || size <= 0) {
    const payload = fs.readFileSync(filePath);
    sha256 = crypto.createHash("sha256").update(payload).digest("hex");
    size = payload.length;
    changed = true;
  }

  if (String(firmware.sha256 || "").toLowerCase() !== sha256) {
    firmware.sha256 = sha256;
    changed = true;
  }
  if (Number(firmware.size) !== size) {
    firmware.size = size;
    changed = true;
  }

  const signature = normalizeHex(firmware.signature, 128);
  const signatureAlg = String(firmware.signature_alg || "").trim().toLowerCase();
  if (!signature || signatureAlg !== OTA_SIGNATURE_ALG) {
    firmware.signature = signSha256Ed25519(sha256);
    firmware.signature_alg = OTA_SIGNATURE_ALG;
    firmware.signedAt = new Date().toISOString();
    changed = true;
  } else if (!firmware.signedAt) {
    firmware.signedAt = new Date().toISOString();
    changed = true;
  }

  return { changed, firmware };
}

function buildDeviceRows(db) {
  const usersById = new Map((db.users || []).map((u) => [u.id, u]));
  const rows = Object.entries(db.devices || {}).map(([deviceId, meta]) => {
    const owner = usersById.get(meta?.ownerUserId) || null;
    const state = getDeviceState(db, deviceId);
    const hw = normalizeHardwareId(meta?.hardware || state?.hardware || inferHardwareFromStatus(state?.status));
    const online = isDeviceOnline(state);
    const zones = Array.isArray(state.zones) ? state.zones : [];
    const watering = online && zones.some(zoneIsActive);
    const smartPlug = isSmartPlugDeviceState(state) || hw === "bwshp6";
    const relayOn = watering || state?.plugTelemetry?.relay_on === true || state?.status?.relay_on === true;
    const smartClimateSupported = !smartPlug;
    const smartClimateAllowed = smartClimateSupported && isSmartClimateAllowedForDevice(db, deviceId, state);
    const smartClimateCfg = (meta?.smartClimate && typeof meta.smartClimate === "object") ? meta.smartClimate : null;
    const smartClimateManagedZones = smartClimateCfg && smartClimateCfg.zones && typeof smartClimateCfg.zones === "object"
      ? Object.values(smartClimateCfg.zones).filter((z) => z && z.enabled === true).length
      : 0;
    const smartClimateEngineEnabled = smartClimateAllowed && smartClimateCfg?.enabled === true && smartClimateManagedZones > 0;
    return {
      deviceId,
      ownerEmail: owner?.email || "nieznany@użytkownik",
      ownerUserId: owner?.id || null,
      hardware: hw || "unknown",
      firmwareVersion: resolveDeviceFirmwareVersion(state),
      online,
      state: online ? (smartPlug ? (relayOn ? "włączone" : "wyłączone") : (watering ? "podlewa" : "bezczynny")) : "offline",
      lastSeen: state.lastSeen || null,
      claimedAt: meta?.claimedAt || null,
      smartClimateSupported,
      smartClimateAllowed,
      smartClimateManagedZones,
      smartClimateEngineEnabled,
    };
  });
  rows.sort((a, b) => {
    if (a.online !== b.online) return a.online ? -1 : 1;
    return a.ownerEmail.localeCompare(b.ownerEmail, "pl");
  });
  return rows;
}

function mapOtaStageToDeviceStatus(stageRaw, progress, errorText) {
  const stage = String(stageRaw || "").toLowerCase();
  if (String(errorText || "").trim().length > 0) return "failed";
  if (stage === "queued") return "queued";
  if (stage === "sent" || stage === "accepted") return "sent";
  if (stage === "verified" || stage === "verifying") return "downloading";
  if (stage === "downloading" || stage === "download") return "downloading";
  if (stage === "flashing" || stage === "writing") return "flashing";
  if (stage === "rebooting" || stage === "restarting") return "rebooting";
  if (stage === "done" || stage === "success" || stage === "ok") return "done";
  if (stage === "failed" || stage === "error") return "failed";
  if (Number.isFinite(progress) && progress > 0) return "downloading";
  return "sent";
}

function normalizeOtaStage(stageRaw) {
  return String(stageRaw || "").trim().toLowerCase();
}

function isOtaFinalSuccessStage(stageRaw) {
  const stage = normalizeOtaStage(stageRaw);
  return stage === "done" || stage === "success" || stage === "ok" || stage === "completed";
}

function isInterimOtaDetail(detailRaw) {
  const detail = String(detailRaw || "").trim().toLowerCase();
  if (!detail) return false;
  return (
    detail.includes("podpis ota poprawny") ||
    detail.includes("weryfikacja ") ||
    detail.includes("pobieranie ")
  );
}

function recomputeCampaignStatus(campaign) {
  const items = Array.isArray(campaign.devices) ? campaign.devices : [];
  if (!items.length) return "empty";
  const statuses = items.map((d) => String(d.status || ""));
  const hasRunning = statuses.some((s) => ["queued", "sent", "downloading", "flashing", "rebooting"].includes(s));
  const hasFailed = statuses.some((s) => s === "failed" || s === "publish_failed");
  const hasDone = statuses.some((s) => s === "done");
  if (hasRunning) return "running";
  if (hasDone && hasFailed) return "partial_failed";
  if (hasFailed && !hasDone) return "failed";
  if (hasDone) return "done";
  if (statuses.every((s) => s === "cancelled")) return "cancelled";
  return "queued";
}

function makeTempPassword() {
  // Avoid ambiguous chars (O/0, I/l/1).
  const alphabet = "ABCDEFGHJKMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
  let out = "";
  for (let i = 0; i < 12; i++) out += alphabet[Math.floor(Math.random() * alphabet.length)];
  return out;
}

function normalizeEmailOrLogin(s) {
  return String(s || "").trim().toLowerCase();
}

function sanitizeMqttPart(s) {
  const raw = String(s || "").trim().toLowerCase();
  if (!raw) return "wm";
  const normalized = raw
    .replace(/[^a-z0-9_-]+/g, "_")
    .replace(/_+/g, "_")
    .replace(/^_+|_+$/g, "");
  return normalized || "wm";
}

function randomPassword(len = 24) {
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
  const bytes = crypto.randomBytes(Math.max(8, len));
  let out = "";
  for (let i = 0; i < len; i++) out += alphabet[bytes[i] % alphabet.length];
  return out;
}

function ensureHaMqttCredentials(db, deviceId, options = {}) {
  const rotate = !!options.rotate;
  if (!db?.devices || !deviceId || !db.devices[deviceId]) {
    return { changed: false, credentials: null };
  }
  const meta = db.devices[deviceId];
  const username = `${HA_MQTT_USER_PREFIX}${sanitizeMqttPart(deviceId)}`;
  const nowIso = new Date().toISOString();
  const current = (meta.haMqtt && typeof meta.haMqtt === "object") ? meta.haMqtt : {};
  const next = {
    username,
    password: String(current.password || ""),
    discoveryNode: String(current.discoveryNode || deviceId),
    createdAt: String(current.createdAt || nowIso),
    rotatedAt: String(current.rotatedAt || current.createdAt || nowIso),
    revision: Number(current.revision || 1),
  };
  let changed = false;

  if (current.username !== username) changed = true;
  if (!next.password || rotate) {
    next.password = randomPassword(24);
    next.rotatedAt = nowIso;
    next.revision = Number.isFinite(Number(current.revision)) ? Number(current.revision) + 1 : 1;
    changed = true;
  }
  if (!next.discoveryNode || next.discoveryNode !== deviceId) {
    next.discoveryNode = deviceId;
    changed = true;
  }
  if (!current.createdAt) changed = true;
  if (!Number.isFinite(Number(current.revision))) changed = true;

  meta.haMqtt = next;
  return { changed, credentials: next };
}

function haMqttPayload(deviceId, creds) {
  const discoveryPrefix = HA_DISCOVERY_PREFIX;
  return {
    ok: true,
    device_id: deviceId,
    broker: {
      host: HA_MQTT_HOST,
      port: HA_MQTT_PORT,
      tls: HA_MQTT_TLS,
    },
    username: String(creds?.username || ""),
    password: String(creds?.password || ""),
    topic_base: baseTopic(deviceId),
    command_base: `${baseTopic(deviceId)}/cmd`,
    refresh_topic: `${baseTopic(deviceId)}/global/refresh`,
    ha_status_topic: `${discoveryPrefix}/status`,
    discovery: {
      prefix: discoveryPrefix,
      node: String(creds?.discoveryNode || deviceId),
      object_prefix: `wm_${deviceId}_`,
    },
    generated_at: String(creds?.createdAt || ""),
    rotated_at: String(creds?.rotatedAt || ""),
    revision: Number(creds?.revision || 1),
  };
}

function mqttSubscribeAsync(client, topics) {
  return new Promise((resolve) => {
    client.subscribe(topics, (err, granted) => {
      if (err) {
        resolve({ ok: false, error: String(err?.message || err), granted: [] });
        return;
      }
      const grants = Array.isArray(granted) ? granted : [];
      const denied = grants.filter((g) => Number(g?.qos) === 128).map((g) => String(g?.topic || ""));
      resolve({
        ok: denied.length === 0,
        error: denied.length ? `SUB denied: ${denied.join(", ")}` : "",
        granted: grants.map((g) => ({ topic: String(g?.topic || ""), qos: Number(g?.qos ?? -1) })),
      });
    });
  });
}

function mqttPublishAsync(client, topic, payload) {
  return new Promise((resolve) => {
    client.publish(topic, payload, { qos: 0, retain: false }, (err) => {
      resolve({
        topic,
        ok: !err,
        error: err ? String(err?.message || err) : "",
      });
    });
  });
}

function runHaMqttSelfTest(deviceId, creds) {
  const brokerUrl = `${HA_MQTT_TLS ? "mqtts" : "mqtt"}://${HA_MQTT_HOST}:${HA_MQTT_PORT}`;
  const discoveryNode = String(creds?.discoveryNode || deviceId);
  const refreshTopic = `${baseTopic(deviceId)}/global/refresh`;
  const haStatusTopic = `${HA_DISCOVERY_PREFIX}/status`;

  return new Promise((resolve) => {
    const startedAt = Date.now();
    const result = {
      broker: brokerUrl,
      connected: false,
      subscribe: { ok: false, error: "", granted: [] },
      publish: [],
      duration_ms: 0,
      error: "",
    };

    let settled = false;
    let timer = null;
    const client = mqtt.connect(brokerUrl, {
      username: String(creds?.username || ""),
      password: String(creds?.password || ""),
      clientId: `ha_test_${sanitizeMqttPart(deviceId)}_${Math.floor(Math.random() * 1_000_000)}`,
      clean: true,
      reconnectPeriod: 0,
      connectTimeout: 6000,
    });

    const finalize = (extra = {}) => {
      if (settled) return;
      settled = true;
      if (timer) clearTimeout(timer);
      Object.assign(result, extra);
      result.duration_ms = Date.now() - startedAt;
      try { client.end(true); } catch {}
      resolve(result);
    };

    timer = setTimeout(() => finalize({ error: "timeout" }), 10_000);

    client.once("error", (err) => {
      finalize({ error: String(err?.message || err) });
    });

    client.once("connect", async () => {
      result.connected = true;
      try {
        result.subscribe = await mqttSubscribeAsync(client, [
          `${baseTopic(deviceId)}/#`,
          `${HA_DISCOVERY_PREFIX}/+/${discoveryNode}/#`,
        ]);

        const pubRefresh = await mqttPublishAsync(client, refreshTopic, "");
        const pubStatus = await mqttPublishAsync(client, haStatusTopic, "online");
        result.publish = [pubRefresh, pubStatus];

        const publishOk = result.publish.every((p) => p.ok);
        const ok = result.connected && result.subscribe.ok && publishOk;
        finalize({ ok, error: ok ? "" : (result.subscribe.error || result.publish.find((p) => !p.ok)?.error || "ACL test failed") });
      } catch (err) {
        finalize({ ok: false, error: String(err?.message || err) });
      }
    });
  });
}

function runMqttAuthSyncHook(reason) {
  if (!MQTT_AUTH_SYNC_HOOK) return;
  if (mqttAuthSyncState.running) {
    mqttAuthSyncPendingReason = reason || "queued";
    return;
  }
  mqttAuthSyncState.running = true;
  mqttAuthSyncState.lastReason = reason || "manual";
  const startedAt = Date.now();

  execFile("/bin/sh", ["-lc", MQTT_AUTH_SYNC_HOOK], { timeout: 20_000 }, (err, stdout, stderr) => {
    mqttAuthSyncState.running = false;
    mqttAuthSyncState.lastDurationMs = Date.now() - startedAt;
    if (err) {
      mqttAuthSyncState.lastErrorAt = new Date().toISOString();
      mqttAuthSyncState.lastError = String(stderr || err.message || err);
      logEvent(`[MQTT-AUTH] sync hook failed (${reason}): ${mqttAuthSyncState.lastError}`, "ERROR");
      const pendingOnError = mqttAuthSyncPendingReason;
      mqttAuthSyncPendingReason = "";
      if (pendingOnError) {
        setTimeout(() => runMqttAuthSyncHook(`followup:${pendingOnError}`), 500);
      }
      return;
    }
    mqttAuthSyncState.lastOkAt = new Date().toISOString();
    mqttAuthSyncState.lastError = "";
    const out = String(stdout || "").trim();
    if (out) logEvent(`[MQTT-AUTH] sync hook ok (${reason}): ${out}`);
    else logEvent(`[MQTT-AUTH] sync hook ok (${reason})`);
    const pending = mqttAuthSyncPendingReason;
    mqttAuthSyncPendingReason = "";
    if (pending) {
      setTimeout(() => runMqttAuthSyncHook(`followup:${pending}`), 200);
    }
  });
}

function mqttPublish(topic, payload, opts = {}) {
  if (!mqttClient.connected) return false;
  const msg = typeof payload === "string" ? payload : JSON.stringify(payload);
  mqttClient.publish(topic, msg, {
    qos: Number.isFinite(Number(opts.qos)) ? Math.max(0, Math.min(1, Number(opts.qos))) : 0,
    retain: opts.retain === true,
  });
  return true;
}

const pendingAcks = new Map();
let lastMqttEventTs = null;
let lastMqttAlertTs = 0;

function parseJsonSafe(text, fallback = null) {
  try {
    return JSON.parse(text);
  } catch {
    return fallback;
  }
}

async function sendAlert(text) {
  const now = Date.now();
  if (!ALERT_WEBHOOK_URL) return;
  if (now - lastMqttAlertTs < 60_000) return;
  lastMqttAlertTs = now;
  try {
    await fetch(ALERT_WEBHOOK_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        source: "wms-cloud",
        timestamp: formatTs(),
        message: text,
      }),
    });
  } catch (err) {
    console.error("[ALERT] webhook error:", err?.message || err);
  }
}

function makeCommandId() {
  return `cmd_${Date.now()}_${Math.floor(Math.random() * 100000)}`;
}

function waitForAck(commandId, timeoutMs = CMD_ACK_TIMEOUT_MS) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pendingAcks.delete(commandId);
      reject(new Error("ACK timeout"));
    }, timeoutMs);
    pendingAcks.set(commandId, { resolve, reject, timer });
  });
}

function clearPendingAck(commandId) {
  const waiter = pendingAcks.get(commandId);
  if (!waiter) return;
  clearTimeout(waiter.timer);
  pendingAcks.delete(commandId);
}

function rejectAllPendingAcks(reason) {
  for (const [commandId, waiter] of pendingAcks.entries()) {
    clearTimeout(waiter.timer);
    waiter.reject(new Error(reason || `ACK cancelled (${commandId})`));
    pendingAcks.delete(commandId);
  }
}

async function publishCommandWithAck(deviceId, commandLeaf, body = {}) {
  if (!mqttClient.connected) throw new Error("Broker MQTT niedostępny");
  const commandId = makeCommandId();
  const envelope = {
    command_id: commandId,
    timestamp: formatTs(),
    ...body,
  };
  const topic = `${baseTopic(deviceId)}/${commandLeaf}`;
  const ackPromise = REQUIRE_CMD_ACK ? waitForAck(commandId, CMD_ACK_TIMEOUT_MS) : null;
  const ok = mqttPublish(topic, envelope, { qos: 1 });
  if (!ok) {
    if (ackPromise) clearPendingAck(commandId);
    throw new Error("Broker MQTT niedostępny");
  }
  if (!REQUIRE_CMD_ACK) {
    return { commandId, ack: { status: "accepted", detail: "ACK bypass (migration mode)" } };
  }
  const ack = await ackPromise;
  if (ack?.status !== "accepted") {
    const detail = ack?.detail || "Komenda odrzucona przez urządzenie";
    throw new Error(detail);
  }
  return { commandId, ack };
}

async function publishCommandWithFastAck(deviceId, commandLeaf, body = {}, opts = {}) {
  if (!mqttClient.connected) throw new Error("Broker MQTT niedostępny");
  const commandId = makeCommandId();
  const envelope = {
    command_id: commandId,
    timestamp: formatTs(),
    ...body,
  };
  const topic = `${baseTopic(deviceId)}/${commandLeaf}`;
  const waitMs = Math.max(150, Number(opts.ackWaitMs || CMD_FAST_ACK_WAIT_MS));
  const allowAckTimeout = opts.allowAckTimeout !== false;
  const ackPromise = REQUIRE_CMD_ACK ? waitForAck(commandId, waitMs) : null;
  const ok = mqttPublish(topic, envelope, { qos: 1 });
  if (!ok) {
    if (ackPromise) clearPendingAck(commandId);
    throw new Error("Broker MQTT niedostępny");
  }
  if (!REQUIRE_CMD_ACK) {
    return { commandId, ack: { status: "accepted", detail: "ACK bypass (migration mode)" } };
  }
  try {
    const ack = await ackPromise;
    if (ack?.status !== "accepted") {
      const detail = ack?.detail || "Komenda odrzucona przez urządzenie";
      throw new Error(detail);
    }
    return { commandId, ack };
  } catch (err) {
    const message = String(err?.message || err);
    if (allowAckTimeout && /^ACK timeout$/i.test(message)) {
      logEvent(
        `[CMD] fast ACK timeout tolerated device=${deviceId} leaf=${commandLeaf} command=${commandId} waitMs=${waitMs}`,
        "WARN"
      );
      return { commandId, ack: { status: "pending", detail: "ACK timeout" } };
    }
    throw err;
  }
}

function authRequired(req, res, next) {
  const token = req.cookies?.wms_token;
  if (!token) return apiError(req, res, 401, "sessionMissing");
  try {
    const payload = jwt.verify(token, JWT_SECRET);
    const sessionUserId = sessionUserIdFromPayload(payload);
    const activity = readSessionActivity(req, sessionUserId);
    if (!activity.ok) {
      clearSessionCookies(req, res);
      return apiError(req, res, 401, activity.error);
    }
    const idleTimeoutMs = idleTimeoutMsForRequest(req);
    if (Date.now() - activity.ts > idleTimeoutMs) {
      clearSessionCookies(req, res);
      return apiError(req, res, 401, "sessionIdleExpired");
    }
    req.sessionActivityTs = activity.ts;
    req.sessionIdleTimeoutMs = idleTimeoutMs;
    req.sessionRemembered = isRememberedSession(req);
    const db = loadDb();
    if (payload?.userId === "__admin__" || String(payload?.role || "").toLowerCase() === "admin") {
      req.db = db;
      req.user = {
        id: "__admin__",
        email: ADMIN_LOGIN,
        role: "admin",
        deviceIds: [],
        selectedDeviceId: null,
      };
      return next();
    }
    const user = db.users.find((u) => u.id === payload.userId);
    if (!user) return apiError(req, res, 401, "userMissing");
    req.db = db;
    req.user = user;
    next();
  } catch {
    return apiError(req, res, 401, "invalidSession");
  }
}

function withDevice(req, res) {
  const owned = getOwnedDeviceIds(req.db, req.user.id);
  const selected = owned.includes(req.user.selectedDeviceId) ? req.user.selectedDeviceId : (owned[0] || null);
  if (!selected) {
    apiError(req, res, 400, "noAssignedDevice");
    return null;
  }
  return selected;
}

function resolveOwnedDeviceId(req, res, candidateDeviceId, { allowAutoSelect = true } = {}) {
  const owned = getOwnedDeviceIds(req.db, req.user.id);
  if (!owned.length) {
    apiError(req, res, 400, "noAssignedDevice");
    return null;
  }
  const requested = String(candidateDeviceId || "").trim();
  if (requested) {
    if (!owned.includes(requested)) {
      apiError(req, res, 403, "deviceNotAssignedToAccount");
      return null;
    }
    return requested;
  }
  if (!allowAutoSelect) {
    apiError(req, res, 400, "noAssignedDevice");
    return null;
  }
  const selected = owned.includes(req.user.selectedDeviceId) ? req.user.selectedDeviceId : (owned[0] || null);
  if (!selected) {
    apiError(req, res, 400, "noAssignedDevice");
    return null;
  }
  return selected;
}

function requireDeviceOnlineOrFail(res, state) {
  if (!isDeviceOnline(state)) {
    apiError(res.req, res, 503, "deviceOfflineNoLiveData");
    return false;
  }
  return true;
}

const mqttClient = mqtt.connect(MQTT_URL, {
  username: MQTT_USERNAME || undefined,
  password: MQTT_PASSWORD || undefined,
  reconnectPeriod: 1000,
});

mqttClient.on("connect", () => {
  logEvent("[MQTT] connected");
  lastMqttEventTs = new Date().toISOString();
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/global/status`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/weather`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/zones`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/programs`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/logs`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/settings/public`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/rain-history`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/watering-percent`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/plug/telemetry`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/global/availability`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/ack`);
  mqttClient.subscribe(`${MQTT_TOPIC_PREFIX}/+/ota/status`);
  sendAlert("[MQTT] reconnect - broker online");
});

mqttClient.on("close", () => {
  lastMqttEventTs = new Date().toISOString();
  logEvent("[MQTT] disconnected", "ERROR");
  rejectAllPendingAcks("MQTT disconnected");
  sendAlert("[MQTT] disconnected - broker offline");
});

mqttClient.on("error", (err) => {
  lastMqttEventTs = new Date().toISOString();
  logEvent(`[MQTT] error: ${err?.message || err}`, "ERROR");
  sendAlert(`[MQTT] error: ${err?.message || err}`);
});

mqttClient.on("message", (topic, payloadBuf, packet) => {
  const payloadText = payloadBuf.toString("utf8");
  const parts = topic.split("/");
  if (parts.length < 3) return;
  const prefix = parts[0];
  const deviceId = parts[1];
  const leaf = parts.slice(2).join("/");
  if (prefix !== MQTT_TOPIC_PREFIX) return;

  const db = loadDb();
  const state = getDeviceState(db, deviceId);
  const eventTsIso = new Date().toISOString();
  const isRetained = packet?.retain === true;
  const wasSmartPlug = isSmartPlugDeviceState(state);
  const prevPlugRelayOn = wasSmartPlug ? isPlugRelayOn(state) : null;
  let zonePushEvent = null;
  // Retained snapshots on broker reconnect should not mark device as "freshly online".
  if (!isRetained) state.lastSeen = eventTsIso;

  const parseMaybeJson = (fallback) => parseJsonSafe(payloadText, fallback);

  if (leaf === "global/status") {
    const incoming = parseMaybeJson({});
    const prevStatus = (state.status && typeof state.status === "object") ? state.status : {};
    let status = (incoming && typeof incoming === "object" && !Array.isArray(incoming))
      ? { ...prevStatus, ...incoming }
      : prevStatus;
    if (isMissingTextValue(status.ip) && !isMissingTextValue(prevStatus.ip)) {
      status = { ...status, ip: prevStatus.ip };
    }
    if (isMissingTextValue(status.fw_version) && !isMissingTextValue(prevStatus.fw_version)) {
      status = { ...status, fw_version: prevStatus.fw_version };
    }
    state.status = status;
    const hw = inferHardwareFromStatus(status);
    if (hw) {
      state.hardware = hw;
      if (db.devices?.[deviceId] && String(db.devices[deviceId].hardware || "") !== hw) {
        db.devices[deviceId].hardware = hw;
      }
    }
    const ip = String(status.ip || "").trim();
    if (!isMissingTextValue(ip)) state.lastKnownIp = ip;
    const fw = String(status.fw_version || "").trim();
    if (!isMissingTextValue(fw)) state.lastKnownFw = fw;
    if (fw && db.devices?.[deviceId] && String(db.devices[deviceId].firmwareVersion || "").trim() !== fw) {
      db.devices[deviceId].firmwareVersion = fw;
    }
  }
  if (leaf === "weather") {
    state.weather = parseMaybeJson({});
    if (!isRetained || !Number.isFinite(isoToMs(state.weatherAt))) state.weatherAt = eventTsIso;
    updateRainDailyTotalsFromState(state, eventTsIso);
  }
  if (leaf === "zones") {
    const nextZones = parseMaybeJson([]);
    const prevZones = Array.isArray(state.zones) ? state.zones : [];
    const prevActiveCount = countActiveZones(prevZones);
    const nextActiveCount = countActiveZones(nextZones);
    trackWateringFromZonesUpdate(db, deviceId, prevZones, nextZones, eventTsIso);
    state.zones = nextZones;
    if (!isRetained && !wasSmartPlug) {
      if (prevActiveCount === 0 && nextActiveCount > 0) {
        zonePushEvent = { eventKey: "watering_start", nextActiveCount };
      } else if (prevActiveCount > 0 && nextActiveCount === 0) {
        zonePushEvent = { eventKey: "watering_stop", prevActiveCount };
      }
    }
  }
  if (leaf === "programs") state.programs = parseMaybeJson([]);
  if (leaf === "logs") state.logs = parseMaybeJson({ logs: [] });
  if (leaf === "settings/public") {
    const incoming = parseMaybeJson({});
    const prevSettings = (state.settingsPublic && typeof state.settingsPublic === "object")
      ? state.settingsPublic
      : {};
    state.settingsPublic = (incoming && typeof incoming === "object" && !Array.isArray(incoming))
      ? { ...prevSettings, ...incoming }
      : prevSettings;
    updateRainDailyTotalsFromState(state, eventTsIso);
  }
  if (leaf === "rain-history") {
    state.rainHistory = parseMaybeJson([]);
    if (!isRetained || !Number.isFinite(isoToMs(state.rainHistoryAt))) state.rainHistoryAt = eventTsIso;
    mergeRainArchiveFromHistory(state, state.rainHistory, eventTsIso);
    updateRainDailyTotalsFromState(state, eventTsIso);
  }
  if (leaf === "watering-percent") {
    state.wateringPercent = parseMaybeJson({});
    if (!isRetained || !Number.isFinite(isoToMs(state.wateringPercentAt))) state.wateringPercentAt = eventTsIso;
    updateRainDailyTotalsFromState(state, eventTsIso);
  }
  if (leaf === "plug/telemetry") state.plugTelemetry = parseMaybeJson({});
  if (leaf === "global/availability") {
    const availability = String(payloadText || "").trim().toLowerCase();
    state.availability = availability;
    state.availabilityAt = eventTsIso;
    if (availability === "offline" || availability === "0" || availability === "false") {
      state.lastSeen = null;
    } else if ((availability === "online" || availability === "1" || availability === "true") && !state.lastSeen) {
      state.lastSeen = eventTsIso;
    }
  }
  if (leaf === "ota/status") {
    const ota = parseMaybeJson({});
    state.ota = ota;
    const campaignId = String(ota?.campaign_id || ota?.campaignId || "");
    const stage = String(ota?.stage || ota?.status || "");
    const progress = Number(ota?.progress);
    const detailText = String(ota?.detail || "");
    const errorText = String(ota?.error || "");
    const nowIso = new Date().toISOString();
    const updateDeviceEntry = (entry) => {
      entry.status = mapOtaStageToDeviceStatus(stage, progress, errorText);
      entry.updatedAt = nowIso;
      if (Number.isFinite(progress)) entry.progress = Math.max(0, Math.min(100, Math.round(progress)));
      if (errorText) entry.note = errorText;
      else if (detailText.length > 0) entry.note = detailText;
    };

    if (Array.isArray(db.otaCampaigns) && db.otaCampaigns.length > 0) {
      let touched = false;
      for (const campaign of db.otaCampaigns) {
        if (!Array.isArray(campaign.devices)) continue;
        if (campaignId && String(campaign.id) !== campaignId) continue;
        const entry = campaign.devices.find((d) => String(d.deviceId) === deviceId);
        if (!entry) continue;
        updateDeviceEntry(entry);
        campaign.status = recomputeCampaignStatus(campaign);
        touched = true;
        if (campaignId) break;
      }
      if (!touched && !campaignId) {
        const candidate = db.otaCampaigns.find((c) =>
          Array.isArray(c.devices) && c.devices.some((d) =>
            String(d.deviceId) === deviceId && ["queued", "sent", "downloading", "flashing", "rebooting"].includes(String(d.status || ""))));
        if (candidate) {
          const entry = candidate.devices.find((d) => String(d.deviceId) === deviceId);
          if (entry) {
            updateDeviceEntry(entry);
            candidate.status = recomputeCampaignStatus(candidate);
          }
        }
      }
    }

    const mappedStatus = mapOtaStageToDeviceStatus(stage, progress, errorText);
    const stageNorm = normalizeOtaStage(stage);
    if (isOtaFinalSuccessStage(stageNorm) && !errorText) {
      const firmwareId = String(ota?.firmware_id || ota?.firmwareId || "");
      const version = String(ota?.version || "");
      const fw = findFirmwareMeta(db, firmwareId, version);
      state.otaLastSuccess = {
        firmwareId: firmwareId || String(fw?.id || ""),
        version: version || String(fw?.version || ""),
        channel: String(fw?.channel || ota?.channel || "stable"),
        notes: String(fw?.notes || ""),
        detail: detailText || "Aktualizacja OTA zakończona pomyślnie.",
        stage: stageNorm || "done",
        completedAt: nowIso,
      };
    } else if (mappedStatus === "failed" && isInterimOtaDetail(state?.otaLastSuccess?.detail)) {
      // Clean up stale, false-positive notices created by non-final OTA stages.
      state.otaLastSuccess = null;
    }

    // Push event to admin UI (live progress).
    broadcastAdmin("ota", {
      deviceId,
      campaign_id: String(ota?.campaign_id || ota?.campaignId || ""),
      stage: String(ota?.stage || ota?.status || ""),
      progress: Number.isFinite(Number(ota?.progress)) ? Number(ota.progress) : null,
      timestamp: formatTs(),
    });
  }
  if (leaf === "ack") {
    const ack = parseMaybeJson({});
    state.lastAck = ack;
    const commandId = String(ack?.command_id || "");
    if (commandId && pendingAcks.has(commandId)) {
      const waiter = pendingAcks.get(commandId);
      clearTimeout(waiter.timer);
      pendingAcks.delete(commandId);
      waiter.resolve(ack);
    }
  }

  const isSmartPlugNow = isSmartPlugDeviceState(state);
  if (isSmartPlugNow && !isRetained && (leaf === "zones" || leaf === "plug/telemetry")) {
    const nextPlugRelayOn = isPlugRelayOn(state);
    if (typeof prevPlugRelayOn === "boolean" && nextPlugRelayOn !== prevPlugRelayOn) {
      const telem = getPlugTelemetrySnapshot(state);
      let mode = "GNIAZDKO";
      let source = `mqtt:${leaf}`;
      if (state.lastPlugEventHint && typeof state.lastPlugEventHint === "object") {
        const hint = state.lastPlugEventHint;
        const expMs = Date.parse(String(hint.expiresAt || ""));
        if (!Number.isFinite(expMs) || Date.now() <= expMs) {
          const hintedMode = String(hint.mode || "").trim();
          const hintedSource = String(hint.source || "").trim();
          if (hintedMode.length > 0) mode = hintedMode;
          if (hintedSource.length > 0) source = hintedSource;
        }
        state.lastPlugEventHint = null;
      }
      dispatchPlugEventToControllers(db, deviceId, {
        on: nextPlugRelayOn,
        seconds: telem.remaining_sec,
        mode,
        source,
      }).catch((err) => {
        logEvent(`[PLUG-NOTIFY] dispatch failed plug=${deviceId} err=${String(err?.message || err)}`, "WARN");
      });

      const deviceLabel = resolveDeviceLabel(db, deviceId, state);
      queueMobilePushForDeviceOwner(
        db,
        deviceId,
        nextPlugRelayOn ? "plug_on" : "plug_off",
        {
          title: "WM Sprinkler",
          body: nextPlugRelayOn
            ? `${deviceLabel}: gniazdko zostało włączone.`
            : `${deviceLabel}: gniazdko zostało wyłączone.`,
          data: {
            type: nextPlugRelayOn ? "plug_on" : "plug_off",
            mode,
            source,
          },
        },
        2500
      );
    }
  }

  if (zonePushEvent) {
    const deviceLabel = resolveDeviceLabel(db, deviceId, state);
    if (zonePushEvent.eventKey === "watering_start") {
      const count = Math.max(1, Number(zonePushEvent.nextActiveCount || 1));
      queueMobilePushForDeviceOwner(
        db,
        deviceId,
        "watering_start",
        {
          title: "WM Sprinkler",
          body: count === 1
            ? `${deviceLabel}: rozpoczęto podlewanie (1 strefa).`
            : `${deviceLabel}: rozpoczęto podlewanie (${count} stref).`,
          data: { type: "watering_start", active_zones: String(count) },
        },
        3500
      );
    } else if (zonePushEvent.eventKey === "watering_stop") {
      queueMobilePushForDeviceOwner(
        db,
        deviceId,
        "watering_stop",
        {
          title: "WM Sprinkler",
          body: `${deviceLabel}: zakończono podlewanie.`,
          data: { type: "watering_stop" },
        },
        3500
      );
    }
  }

  db.deviceStates[deviceId] = state;
  saveDb(db);
});

app.get("/api/admin/updates/stream", authRequired, adminRequired, (req, res) => {
  res.status(200);
  res.setHeader("Content-Type", "text/event-stream; charset=utf-8");
  res.setHeader("Cache-Control", "no-cache, no-transform");
  res.setHeader("Connection", "keep-alive");
  res.flushHeaders?.();

  adminSseClients.add(res);
  sseSend(res, "hello", { ok: true, timestamp: formatTs(), user: req.user?.email || "admin" });

  const pingTimer = setInterval(() => {
    sseSend(res, "ping", { t: Date.now() });
  }, 25_000);

  req.on("close", () => {
    clearInterval(pingTimer);
    adminSseClients.delete(res);
  });
});

// ---- Auth/session
app.post("/api/register", registerLimiter, async (req, res) => {
  const email = String(req.body?.email || "").trim().toLowerCase();
  const password = String(req.body?.password || "");
  const remember = resolveRememberPreference(req.body, true);
  if (!email || !password) return apiError(req, res, 400, "emailPasswordRequired");
  const db = loadDb();
  if (db.users.some((u) => u.email === email)) {
    return apiError(req, res, 409, "userAlreadyExists");
  }
  const passwordHash = await bcrypt.hash(password, 10);
  const loginAt = new Date().toISOString();
  const user = {
    id: `u_${Date.now()}_${Math.floor(Math.random() * 100000)}`,
    email,
    passwordHash,
    deviceIds: [],
    selectedDeviceId: null,
    createdAt: loginAt,
    lastLoginAt: loginAt,
  };
  db.users.push(user);
  saveDb(db);
  setSessionCookies(req, res, makeToken(user.id), user.id, remember);
  res.json({ ok: true, redirect: isAdminUser(user) ? "/admin" : "/app" });
});

app.post("/api/login", loginLimiter, async (req, res) => {
  const email = String(req.body?.email || "").trim().toLowerCase();
  const password = String(req.body?.password || "");
  const remember = resolveRememberPreference(req.body, true);
  if (email === ADMIN_LOGIN && password === ADMIN_PASSWORD) {
    setSessionCookies(req, res, makeAdminToken(ADMIN_LOGIN), "__admin__", remember);
    return res.json({ ok: true, redirect: "/admin" });
  }
  const db = loadDb();
  const user = db.users.find((u) => u.email === email);
  if (!user) return apiError(req, res, 401, "invalidLogin");
  const ok = await bcrypt.compare(password, user.passwordHash);
  if (!ok) return apiError(req, res, 401, "invalidLogin");
  if (!user.createdAt) user.createdAt = inferUserCreatedAtIsoFromId(user.id) || new Date().toISOString();
  user.lastLoginAt = new Date().toISOString();
  saveDb(db);
  setSessionCookies(req, res, makeToken(user.id), user.id, remember);
  res.json({ ok: true, redirect: isAdminUser(user) ? "/admin" : "/app" });
});

app.post("/api/logout", (req, res) => {
  clearSessionCookies(req, res);
  res.json({ ok: true });
});

app.get("/logout", (req, res) => {
  clearSessionCookies(req, res);
  res.redirect("/");
});

app.get("/api/session", authRequired, (req, res) => {
  const devices = getOwnedDeviceIds(req.db, req.user.id);
  const selected = devices.includes(req.user.selectedDeviceId) ? req.user.selectedDeviceId : (devices[0] || null);
  const selectedState = selected ? getDeviceState(req.db, selected) : null;
  const smartClimateAllowed = selected ? isSmartClimateAllowedForDevice(req.db, selected, selectedState) : false;
  const idleTimeoutMs = Number(req.sessionIdleTimeoutMs || idleTimeoutMsForRequest(req));
  const idleRemainingMs = Math.max(0, idleTimeoutMs - Math.max(0, Date.now() - Number(req.sessionActivityTs || 0)));
  res.json({
    ok: true,
    email: req.user.email,
    deviceId: selected,
    devices,
    role: isAdminUser(req.user) ? "admin" : "user",
    isAdmin: isAdminUser(req.user),
    idleTimeoutMs,
    idleRemainingMs,
    remember: !!req.sessionRemembered,
    features: {
      smartClimate: smartClimateAllowed,
    },
  });
});

app.post("/api/session/touch", authRequired, (req, res) => {
  const sessionUserId = isAdminUser(req.user) ? "__admin__" : String(req.user.id || "");
  const remembered = isRememberedSession(req);
  const idleTimeoutMs = remembered ? SESSION_IDLE_TIMEOUT_REMEMBER_MS : SESSION_IDLE_TIMEOUT_MS;
  const opts = cookieOptions(req, { persistent: remembered });
  res.cookie("wms_activity", makeActivityToken(sessionUserId), opts);
  res.json({ ok: true, idleTimeoutMs, remember: remembered });
});

app.get("/api/mobile/push/status", authRequired, (req, res) => {
  const tokens = ensureUserMobilePushTokens(req.user);
  res.json({
    ok: true,
    enabled: MOBILE_PUSH_ENABLED,
    backend_ready: mobilePushAvailable(),
    token_count: tokens.length,
    last_error: mobilePushInitError || "",
  });
});

app.post("/api/mobile/push/register", authRequired, (req, res) => {
  if (!MOBILE_PUSH_ENABLED) {
    return res.status(503).json({ ok: false, error: "Powiadomienia mobilne są wyłączone na serwerze." });
  }
  if (!mobilePushAvailable()) {
    return res.status(503).json({
      ok: false,
      error: "Backend push nie jest gotowy (sprawdź konfigurację FCM).",
      detail: mobilePushInitError || "FCM init failed",
    });
  }
  const token = normalizeMobilePushToken(req.body?.token);
  if (!token) return apiError(req, res, 400, "invalidData");

  const result = upsertUserMobilePushToken(req.db, req.user, {
    token,
    platform: req.body?.platform || "android",
    appVersion: req.body?.app_version || req.body?.appVersion || "",
    appBuild: req.body?.app_build || req.body?.appBuild || "",
    deviceModel: req.body?.device_model || req.body?.deviceModel || "",
    packageName: req.body?.package_name || req.body?.packageName || "",
    locale: req.body?.locale || "",
  });
  if (!result.ok) return apiError(req, res, 400, "invalidData");

  saveDb(req.db);
  logEvent(`[PUSH] token registered user=${req.user?.id || "?"} platform=android count=${result.count}`);
  return res.json({
    ok: true,
    registered: true,
    token_count: result.count,
  });
});

app.post("/api/mobile/push/unregister", authRequired, (req, res) => {
  const token = normalizeMobilePushToken(req.body?.token);
  if (!token) return apiError(req, res, 400, "invalidData");
  const result = removeUserMobilePushToken(req.user, token);
  if (result.removed > 0) {
    saveDb(req.db);
    logEvent(`[PUSH] token unregistered user=${req.user?.id || "?"} removed=${result.removed}`);
  }
  return res.json({
    ok: true,
    removed: result.removed,
    token_count: result.count,
  });
});

app.post("/api/mobile/push/test", authRequired, async (req, res) => {
  const title = String(req.body?.title || "WM Sprinkler").trim().slice(0, 120) || "WM Sprinkler";
  const body = String(req.body?.body || "To jest test natywnego powiadomienia push.").trim().slice(0, 240);
  const userId = String(req.user?.id || "").trim();
  if (!userId) return apiError(req, res, 400, "invalidData");
  try {
    const result = await sendMobilePushToUser(req.db, userId, {
      title,
      body,
      data: {
        type: "push_test",
        open_url: `${PUBLIC_BASE_URL}/app`,
      },
    });
    return res.json({ ok: true, ...result });
  } catch (err) {
    return res.status(500).json({ ok: false, error: String(err?.message || err) });
  }
});

app.get("/api/selected-device-status", authRequired, (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  res.json({
    ok: true,
    device_id: deviceId,
    online: isDeviceOnline(state),
    lastSeen: state.lastSeen || null,
  });
});

app.get("/api/ota/latest", authRequired, (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  let latest = state?.otaLastSuccess || null;
  if (latest && isInterimOtaDetail(latest.detail) && !isOtaFinalSuccessStage(latest.stage)) {
    latest = null;
  }
  const seenKey = String(req.user?.otaSeen?.[deviceId] || "");
  const latestKey = String(latest?.completedAt || "");
  const pending = !!latest && !!latestKey && seenKey !== latestKey;
  res.json({
    ok: true,
    device_id: deviceId,
    pending,
    notice: pending ? latest : null,
  });
});

app.post("/api/ota/latest/seen", authRequired, (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  const latest = getDeviceState(req.db, deviceId)?.otaLastSuccess || null;
  if (!latest?.completedAt) return res.json({ ok: true, pending: false });
  if (!req.user.otaSeen || typeof req.user.otaSeen !== "object") req.user.otaSeen = {};
  req.user.otaSeen[deviceId] = String(latest.completedAt);
  saveDb(req.db);
  res.json({ ok: true, pending: false });
});

// ---- Device claim/select
app.post("/api/devices/claim", authRequired, claimLimiter, async (req, res) => {
  const deviceId = String(req.body?.device_id || "").trim();
  const claimCode = String(req.body?.claim_code || "").trim() || deviceId;
  logEvent(`[CLAIM] request user=${req.user?.id || "?"} ip=${clientIp(req)} device=${deviceId}`);
  if (!/^WMS_\d{5}$/.test(deviceId)) {
    logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=invalidDeviceId`);
    return apiError(req, res, 400, "invalidDeviceId");
  }

  let state = getDeviceState(req.db, deviceId);
  const legacyCompatActive = isLegacyCompatModeActive();
  const claimOnlineTtlMs = legacyCompatActive
    ? Math.max(DEVICE_ONLINE_TTL_MS, 5 * 60 * 1000)
    : DEVICE_ONLINE_TTL_MS;
  if (!isDeviceOnlineWithin(state, claimOnlineTtlMs)) {
    apiError(req, res, 503, "deviceOfflineNoLiveData");
    logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=deviceOfflineNoLiveData`);
    return;
  }

  const remoteDeviceId = String(state.status?.device_id || "").trim();
  const remoteClaimCode = String(state.status?.claim_code || remoteDeviceId || (legacyCompatActive ? deviceId : "")).trim();
  if (remoteDeviceId && remoteDeviceId !== deviceId) {
    logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=deviceReportsDifferentId remote=${remoteDeviceId || "-"}`);
    return apiError(req, res, 400, "deviceReportsDifferentId");
  }
  if (!remoteDeviceId && !legacyCompatActive) {
    logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=deviceReportsDifferentId remote=-`);
    return apiError(req, res, 400, "deviceReportsDifferentId");
  }
  const claimCodeMatches = !!remoteClaimCode && claimCode === remoteClaimCode;
  const legacyClaimCodeBypass = legacyCompatActive && claimCode === deviceId;
  if (!claimCodeMatches && !legacyClaimCodeBypass) {
    logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=invalidClaimCode provided=${claimCode || "-"} expected=${remoteClaimCode || "-"}`);
    return apiError(req, res, 400, "invalidClaimCode");
  }
  if (!claimCodeMatches && legacyClaimCodeBypass) {
    logEvent(
      `[CLAIM] legacy claim_code bypass user=${req.user?.id || "?"} device=${deviceId} provided=${claimCode || "-"} expected=${remoteClaimCode || "-"}`
    );
  }

  const owner = req.db.devices?.[deviceId]?.ownerUserId || null;
  if (owner && owner !== req.user.id) {
    logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=deviceAssignedToAnotherAccount owner=${owner}`);
    return apiError(req, res, 403, "deviceAssignedToAnotherAccount");
  }

  const statusNow = (state?.status && typeof state.status === "object") ? state.status : {};
  const settingsNow = (state?.settingsPublic && typeof state.settingsPublic === "object")
    ? state.settingsPublic
    : {};
  const pairNow = String(statusNow.pair_state || settingsNow.pair_state || "").trim().toLowerCase();
  const accountNow = String(statusNow.assigned_account || settingsNow.assigned_account || "").trim();
  const assignedToWmsNow = statusNow.assigned_to_wms === true || settingsNow.assigned_to_wms === true;
  const alreadyAssignedToUser =
    pairNow === "assigned"
    && (accountNow === req.user.id || (accountNow.length === 0 && assignedToWmsNow));

  if (!alreadyAssignedToUser && !mqttClient.connected) {
    if (!legacyCompatActive) {
      logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=cloudMqttOffline`);
      return apiError(req, res, 503, "cloudMqttOffline");
    }
    logEvent(`[CLAIM] legacy mqtt offline bypass user=${req.user?.id || "?"} device=${deviceId}`, "WARN");
  }

  let ackError = null;
  if (!alreadyAssignedToUser && mqttClient.connected) {
    try {
      await publishCommandWithAck(deviceId, "cmd/pair/claim", {
        action: "pair_claim",
        account_id: req.user.id,
        owner_user_id: req.user.id,
      });
      logEvent(`[CLAIM] ack accepted user=${req.user?.id || "?"} device=${deviceId}`);
    } catch (err) {
      ackError = err;
      logEvent(`[CLAIM] ack failed user=${req.user?.id || "?"} device=${deviceId} error=${String(err?.message || err)}`, "ERROR");
    }
  } else if (!alreadyAssignedToUser && !mqttClient.connected) {
    ackError = null;
    logEvent(`[CLAIM] legacy claim continues without mqtt ack user=${req.user?.id || "?"} device=${deviceId}`, "WARN");
  } else {
    logEvent(
      `[CLAIM] state already assigned user=${req.user?.id || "?"} device=${deviceId}`
      + ` pair=${pairNow} account=${accountNow || "-"} assigned=${assignedToWmsNow ? 1 : 0}`
    );
  }

  if (ackError) {
    // Fallback: ACK can be missed due QoS0/race, so check state updates for a short window.
    const fallbackDeadline = Date.now() + 4500;
    let assignedByState = false;
    let stateAfter = null;
    let pairAfter = "";
    let accountAfter = "";
    let assignedToWmsAfter = false;

    while (Date.now() <= fallbackDeadline) {
      const dbAfter = loadDb();
      stateAfter = getDeviceState(dbAfter, deviceId);
      const statusAfter = (stateAfter?.status && typeof stateAfter.status === "object") ? stateAfter.status : {};
      const settingsAfter = (stateAfter?.settingsPublic && typeof stateAfter.settingsPublic === "object")
        ? stateAfter.settingsPublic
        : {};
      pairAfter = String(statusAfter.pair_state || settingsAfter.pair_state || "").trim().toLowerCase();
      accountAfter = String(statusAfter.assigned_account || settingsAfter.assigned_account || "").trim();
      assignedToWmsAfter = statusAfter.assigned_to_wms === true || settingsAfter.assigned_to_wms === true;
      assignedByState = pairAfter === "assigned" && (accountAfter === req.user.id || (accountAfter.length === 0 && assignedToWmsAfter));
      if (assignedByState) break;
      await new Promise((resolve) => setTimeout(resolve, 300));
    }

    if (!assignedByState) {
      const ackMessage = String(ackError?.message || "");
      const canBypassAckInLegacy =
        legacyCompatActive &&
        (/^ACK timeout$/i.test(ackMessage)
          || /Broker MQTT niedostępny/i.test(ackMessage)
          || /MQTT disconnected/i.test(ackMessage)
          || /^ACK cancelled\b/i.test(ackMessage));
      if (canBypassAckInLegacy) {
        logEvent(
          `[CLAIM] legacy bypass ack timeout user=${req.user?.id || "?"} device=${deviceId}`
          + ` pair=${pairAfter || "-"} account=${accountAfter || "-"} assigned=${assignedToWmsAfter ? 1 : 0}`,
          "WARN"
        );
      } else {
        logEvent(
          `[CLAIM] fallback check failed user=${req.user?.id || "?"} device=${deviceId}`
          + ` pair=${pairAfter || "-"} account=${accountAfter || "-"} assigned=${assignedToWmsAfter ? 1 : 0}`,
          "ERROR"
        );
        return apiError(req, res, 503, ackError?.message || "ACK timeout");
      }
    } else {
      state = stateAfter || state;
      logEvent(`[CLAIM] ack missing but state confirms assignment user=${req.user?.id || "?"} device=${deviceId}`);
    }
  }

  const dbNow = loadDb();
  const userNow = Array.isArray(dbNow.users) ? dbNow.users.find((u) => u.id === req.user.id) : null;
  if (!userNow) {
    return apiError(req, res, 401, "userMissing");
  }
  if (!dbNow.devices || typeof dbNow.devices !== "object") dbNow.devices = {};
  const ownerNow = dbNow.devices?.[deviceId]?.ownerUserId || null;
  if (ownerNow && ownerNow !== req.user.id) {
    logEvent(`[CLAIM] reject user=${req.user?.id || "?"} device=${deviceId} reason=deviceAssignedToAnotherAccount ownerNow=${ownerNow}`);
    return apiError(req, res, 403, "deviceAssignedToAnotherAccount");
  }

  const prevMeta = dbNow.devices?.[deviceId] || {};
  const claimHardware = normalizeHardwareId(prevMeta.hardware || state.hardware || inferHardwareFromStatus(state.status));
  const nowIso = new Date().toISOString();
  const freshClaim = ownerNow !== req.user.id;
  const prevAccess = (prevMeta.smartClimateAccess && typeof prevMeta.smartClimateAccess === "object")
    ? prevMeta.smartClimateAccess
    : null;
  const nextSmartClimateEnabled = freshClaim ? true : (prevMeta.smartClimateEnabled !== false);
  const nextSmartClimateAccess = freshClaim
    ? { enabled: true, updatedAt: nowIso, updatedBy: "claim" }
    : (prevAccess || { enabled: true, updatedAt: nowIso, updatedBy: "claim" });
  dbNow.devices[deviceId] = {
    ...prevMeta,
    ownerUserId: req.user.id,
    claimedAt: prevMeta.claimedAt || nowIso,
    hardware: claimHardware || prevMeta.hardware || "",
    smartClimateEnabled: nextSmartClimateEnabled,
    smartClimateAccess: {
      ...nextSmartClimateAccess,
      enabled: nextSmartClimateAccess.enabled !== false,
      updatedAt: String(nextSmartClimateAccess.updatedAt || nowIso),
      updatedBy: String(nextSmartClimateAccess.updatedBy || "claim"),
    },
  };
  const ha = ensureHaMqttCredentials(dbNow, deviceId, { rotate: false });
  if (!Array.isArray(userNow.deviceIds)) userNow.deviceIds = [];
  if (!userNow.deviceIds.includes(deviceId)) userNow.deviceIds.push(deviceId);
  if (!userNow.selectedDeviceId) userNow.selectedDeviceId = deviceId;
  saveDb(dbNow);
  if (ha.changed) runMqttAuthSyncHook(`claim:${deviceId}`);
  mqttPublish(`${baseTopic(deviceId)}/global/refresh`, "");
  logEvent(`[CLAIM] success user=${req.user?.id || "?"} device=${deviceId}`);
  res.json({ ok: true, device_id: deviceId });
});

app.post("/api/devices/select", authRequired, (req, res) => {
  const deviceId = String(req.body?.device_id || "").trim();
  if ((req.db.devices?.[deviceId]?.ownerUserId || null) !== req.user.id) {
    return apiError(req, res, 403, "deviceNotAssignedToAccount");
  }
  req.user.selectedDeviceId = deviceId;
  saveDb(req.db);
  mqttPublish(`${baseTopic(deviceId)}/global/refresh`, "");
  res.json({ ok: true });
});

app.post("/api/devices/unclaim", authRequired, (req, res) => {
  const requested = String(req.body?.device_id || "").trim();
  const owned = getOwnedDeviceIds(req.db, req.user.id);
  const deviceId = requested
    ? (owned.includes(requested) ? requested : null)
    : (owned.includes(req.user.selectedDeviceId) ? req.user.selectedDeviceId : (owned[0] || null));

  if (!deviceId) {
    return apiError(req, res, 400, "noAssignedDeviceToUnclaim");
  }

  // Spójne odpięcie: czyścimy rekord urządzenia i wszystkie referencje userów.
  if (req.db.devices?.[deviceId]) {
    delete req.db.devices[deviceId];
  }

  for (const user of req.db.users || []) {
    user.deviceIds = (user.deviceIds || []).filter((id) => id !== deviceId);
    if (user.selectedDeviceId === deviceId) {
      const stillOwned = getOwnedDeviceIds(req.db, user.id);
      user.selectedDeviceId = stillOwned[0] || null;
    }
  }

  saveDb(req.db);
  runMqttAuthSyncHook(`unclaim:${deviceId}`);
  mqttPublish(`${baseTopic(deviceId)}/cmd/pair/unclaim`, {
    command_id: `unclaim_${Date.now()}`,
    account_id: req.user.id,
  });
  const remaining = getOwnedDeviceIds(req.db, req.user.id);
  res.json({ ok: true, device_id: deviceId, devices: remaining, selected: req.user.selectedDeviceId || null });
});

app.get("/api/devices", authRequired, (req, res) => {
  const devices = getOwnedDeviceIds(req.db, req.user.id);
  const selected = devices.includes(req.user.selectedDeviceId) ? req.user.selectedDeviceId : (devices[0] || null);
  res.json({ ok: true, devices, selected });
});

app.get("/api/devices/compatible", authRequired, (req, res) => {
  const legacyCompatActive = isLegacyCompatModeActive();
  const owned = getOwnedDeviceIds(req.db, req.user.id);
  const selected = owned.includes(req.user.selectedDeviceId) ? req.user.selectedDeviceId : (owned[0] || null);
  const ownedNetworks = [];
  const ownedSubnets = new Set();
  for (const ownedDeviceId of owned) {
    const ownedState = getDeviceState(req.db, ownedDeviceId);
    if (!ownedState) continue;
    const ownedStatus = (ownedState.status && typeof ownedState.status === "object") ? ownedState.status : {};
    const ownedIp = normalizeIpv4(String(ownedStatus.ip || ownedState.lastKnownIp || ""));
    const ownedSubnet = ipv4Subnet24(ownedIp);
    if (ownedSubnet) ownedSubnets.add(ownedSubnet);
    const n = networkIdentityFromState(ownedState);
    if (!n.bssid && !n.ssid) continue;
    ownedNetworks.push(n);
  }
  const ownedBssids = new Set(
    ownedNetworks
      .map((n) => String(n.bssid || "").trim())
      .filter((v) => v.length > 0)
  );
  const ownedSsidGateway = new Set(
    ownedNetworks
      .map((n) => {
        const ssid = String(n.ssid || "").trim();
        const gateway = String(n.gateway || "").trim();
        return ssid && gateway ? `${ssid}|${gateway}` : "";
      })
      .filter((v) => v.length > 0)
  );
  const ownedSsids = new Set(
    ownedNetworks
      .map((n) => String(n.ssid || "").trim())
      .filter((v) => v.length > 0)
  );
  const canDiscoverByBssid = ownedBssids.size > 0;
  const canDiscoverBySsid = ownedSsidGateway.size > 0 || ownedSsids.size > 0;
  const canDiscoverByLegacySubnet = legacyCompatActive && ownedSubnets.size > 0;
  const devices = [];

  for (const [deviceId] of Object.entries(req.db.deviceStates || {})) {
    if (!/^WMS_\d{5}$/.test(deviceId)) continue;
    if (selected && deviceId === selected) continue;
    const state = getDeviceState(req.db, deviceId);
    const compatibleOnlineTtlMs = legacyCompatActive
      ? Math.max(COMPATIBLE_DEVICE_ONLINE_TTL_MS, 5 * 60 * 1000)
      : COMPATIBLE_DEVICE_ONLINE_TTL_MS;
    const online = isDeviceOnlineWithin(state, compatibleOnlineTtlMs);

    const meta = req.db.devices?.[deviceId] || null;
    const ownerUserId = String(meta?.ownerUserId || "").trim() || null;
    const assignedToMeDb = ownerUserId === req.user.id;
    const claimable = !ownerUserId;
    if (!claimable && !assignedToMeDb) continue;
    // Strict policy: do not show stale/offline entries in the discovery list.
    if (!online) continue;

    const status = (state.status && typeof state.status === "object") ? state.status : {};
    const settingsPublic = (state.settingsPublic && typeof state.settingsPublic === "object")
      ? state.settingsPublic
      : {};
    let hardware = normalizeHardwareId(meta?.hardware || state?.hardware || inferHardwareFromStatus(status));
    if (!hardware) {
      if (assignedToMeDb || legacyCompatActive) hardware = "unknown";
      else continue;
    }

    const defaultPairState = claimable ? "unassigned" : "assigned";
    const pairState = String(status.pair_state || settingsPublic.pair_state || defaultPairState).trim() || defaultPairState;
    const pairStateNorm = pairState.toLowerCase();
    const assignedAccount = String(status.assigned_account || settingsPublic.assigned_account || "").trim();
    const assignedToWms = status.assigned_to_wms === true || settingsPublic.assigned_to_wms === true;
    const reportedAssignedToMe =
      pairStateNorm === "assigned"
      && (assignedAccount === req.user.id || (assignedAccount.length === 0 && assignedToWms));
    const assignedToMe = assignedToMeDb || (!ownerUserId && reportedAssignedToMe);
    const pairingVisible =
      pairStateNorm === "pairing" ||
      pairStateNorm === "discoverable" ||
      pairStateNorm === "unassigned";
    const candidateNetwork = networkIdentityFromState(state);
    const candidateBssid = candidateNetwork.bssid;
    const candidateSsid = String(candidateNetwork.ssid || "").trim();
    const candidateGateway = String(candidateNetwork.gateway || "").trim();
    const candidateSsidGateway = (candidateSsid && candidateGateway) ? `${candidateSsid}|${candidateGateway}` : "";
    const candidateIp = normalizeIpv4(String(status.ip || state.lastKnownIp || ""));
    const candidateSubnet = ipv4Subnet24(candidateIp);
    const sameByBssid = candidateBssid.length > 0 && ownedBssids.has(candidateBssid);
    const sameBySsidGateway = candidateSsidGateway.length > 0 && ownedSsidGateway.has(candidateSsidGateway);
    const sameBySsid = candidateSsid.length > 0 && ownedSsids.has(candidateSsid);
    const sameBySubnet = candidateSubnet.length > 0 && ownedSubnets.has(candidateSubnet);
    const sameNetworkStrict = sameByBssid || sameBySsidGateway || (!canDiscoverByBssid && sameBySsid);
    const sameNetwork = sameNetworkStrict || (legacyCompatActive && sameBySubnet);

    // Security-first: expose claimable devices only when the selected controller can prove
    // same-network scope (BSSID preferred, SSID fallback) and the candidate is discoverable
    // (pairing/discoverable/unassigned).
    if (claimable) {
      if (!sameNetwork) continue;
      if (!pairingVisible && !reportedAssignedToMe) continue;
    }

    const claimCodeOut = String(status.claim_code || (legacyCompatActive ? deviceId : ""));

    devices.push({
      device_id: deviceId,
      claim_code: claimCodeOut,
      hardware,
      hardware_label: hardwareLabel(hardware),
      fw_version: resolveDeviceFirmwareVersion(state, meta),
      online,
      pair_state: pairState,
      ip: String(status.ip || ""),
      assigned_to_me: assignedToMe,
      reported_assigned_to_me: reportedAssignedToMe,
      claimable,
      selected: assignedToMe,
      last_seen: state.lastSeen || null,
      network_match: sameNetwork,
    });
  }

  devices.sort((a, b) => {
    if (a.assigned_to_me !== b.assigned_to_me) return a.assigned_to_me ? -1 : 1;
    if (a.claimable !== b.claimable) return a.claimable ? -1 : 1;
    return String(a.device_id).localeCompare(String(b.device_id), "pl");
  });

  res.json({
    ok: true,
    count: devices.length,
    devices,
    can_discover: canDiscoverByBssid || canDiscoverBySsid || canDiscoverByLegacySubnet,
    discovery_scope: canDiscoverByBssid
      ? "bssid+ssid_gateway"
      : (canDiscoverBySsid ? "ssid" : (canDiscoverByLegacySubnet ? "legacy-ipv4-subnet" : "none")),
    legacy_mode_active: legacyCompatActive,
    legacy_mode_until: LEGACY_COMPAT_MODE_UNTIL,
    timestamp: formatTs(),
  });
});

app.get("/api/ha/mqtt", authRequired, (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  const ensured = ensureHaMqttCredentials(req.db, deviceId, { rotate: false });
  if (!ensured.credentials) {
    return apiError(req, res, 500, "haMqttDataPrepareFailed");
  }
  if (ensured.changed) {
    saveDb(req.db);
    runMqttAuthSyncHook(`ha-read:${deviceId}`);
  }
  res.json(haMqttPayload(deviceId, ensured.credentials));
});

app.post("/api/ha/mqtt/rotate", authRequired, (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  const ensured = ensureHaMqttCredentials(req.db, deviceId, { rotate: true });
  if (!ensured.credentials) {
    return apiError(req, res, 500, "haMqttRotateFailed");
  }
  saveDb(req.db);
  runMqttAuthSyncHook(`ha-rotate:${deviceId}`);
  res.json(haMqttPayload(deviceId, ensured.credentials));
});

app.post("/api/ha/mqtt/rediscover", authRequired, (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  if (!mqttClient.connected) {
    return apiError(req, res, 503, "cloudMqttOffline");
  }
  const haStatusTopic = `${HA_DISCOVERY_PREFIX}/status`;
  const refreshTopic = `${baseTopic(deviceId)}/global/refresh`;
  const okStatus = mqttPublish(haStatusTopic, "online");
  const okRefresh = mqttPublish(refreshTopic, "");
  if (!okStatus || !okRefresh) {
    return apiError(req, res, 503, "rediscoveryPublishFailed");
  }
  res.json({
    ok: true,
    device_id: deviceId,
    published: [haStatusTopic, refreshTopic],
  });
});

app.post("/api/ha/mqtt/test", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  const ensured = ensureHaMqttCredentials(req.db, deviceId, { rotate: false });
  if (!ensured.credentials) {
    return apiError(req, res, 500, "haMqttTestPrepareFailed");
  }
  if (ensured.changed) {
    saveDb(req.db);
    runMqttAuthSyncHook(`ha-test-read:${deviceId}`);
  }
  const test = await runHaMqttSelfTest(deviceId, ensured.credentials);
  res.json({ ok: !!test.ok, device_id: deviceId, test });
});

app.get("/api/admin/devices", authRequired, adminRequired, (req, res) => {
  const rows = buildDeviceRows(req.db);
  res.json({ ok: true, count: rows.length, devices: rows, timestamp: formatTs() });
});

app.post("/api/admin/devices/:deviceId/smart-climate-access", authRequired, adminRequired, (req, res) => {
  const deviceId = String(req.params?.deviceId || "").trim();
  const enabled = req.body?.enabled !== false;
  const db = loadDb();
  const meta = db?.devices?.[deviceId];
  if (!meta || typeof meta !== "object") {
    return res.status(404).json({ ok: false, error: "Urządzenie nie istnieje" });
  }
  const state = getDeviceState(db, deviceId);
  const hw = normalizeHardwareId(meta?.hardware || state?.hardware || state?.status?.hardware || state?.status?.model);
  if (isSmartPlugDeviceState(state) || hw === "bwshp6") {
    return res.status(400).json({ ok: false, error: "WM Smart Climate nie jest dostępne dla gniazdek." });
  }

  const nowIso = new Date().toISOString();
  meta.smartClimateEnabled = enabled;
  meta.smartClimateAccess = {
    enabled,
    updatedAt: nowIso,
    updatedBy: String(req.user?.id || "admin"),
  };

  let managedZones = 0;
  let engineEnabled = false;
  if (meta.smartClimate && typeof meta.smartClimate === "object") {
    const cfg = meta.smartClimate;
    managedZones = (cfg.zones && typeof cfg.zones === "object")
      ? Object.values(cfg.zones).filter((z) => z && z.enabled === true).length
      : 0;
    if (enabled) {
      // Preserve previous per-zone setup. If zones are already configured, restore engine state.
      if (managedZones > 0) cfg.enabled = true;
    } else {
      cfg.enabled = false;
      cfg.lastEvaluationAt = nowIso;
    }
    cfg.updatedAt = nowIso;
    engineEnabled = cfg.enabled === true && managedZones > 0;
  }

  saveDb(db);
  res.json({
    ok: true,
    deviceId,
    smartClimate: {
      supported: true,
      allowed: enabled,
      managedZones,
      engineEnabled,
      updatedAt: nowIso,
    },
  });
});

app.get("/api/admin/updates/overview", authRequired, adminRequired, (req, res) => {
  const devices = buildDeviceRows(req.db);
  const firmwares = Array.isArray(req.db.firmwares) ? req.db.firmwares : [];
  const firmwareById = new Map(
    firmwares.map((f) => [String(f?.id || ""), f])
  );
  const otaCampaigns = (Array.isArray(req.db.otaCampaigns) ? req.db.otaCampaigns : []).map((campaign) => {
    const firmwareMeta = firmwareById.get(String(campaign?.firmwareId || ""));
    return {
      ...campaign,
      firmwareTarget: sanitizeOtaTarget(campaign?.firmwareTarget || firmwareMeta?.target),
    };
  });
  res.json({
    ok: true,
    timestamp: formatTs(),
    devices,
    firmwares,
    otaCampaigns,
  });
});

app.get("/api/admin/stats/watering", authRequired, adminRequired, (req, res) => {
  const month = String(req.query?.month || "").trim();
  const deviceId = String(req.query?.deviceId || "").trim();
  const rangeMode = normalizeWateringStatsRangeMode(req.query?.rangeMode || req.query?.range);
  const stats = buildWateringStats(req.db, { month, deviceId, rangeMode });
  res.json({
    ok: true,
    timestamp: formatTs(),
    ...stats,
  });
});

app.get("/api/admin/stats/watering/export", authRequired, adminRequired, (req, res) => {
  const month = String(req.query?.month || "").trim();
  const deviceId = String(req.query?.deviceId || "").trim();
  const scopeRaw = String(req.query?.scope || "summary").trim().toLowerCase();
  const scope = scopeRaw === "daily" ? "daily" : "summary";
  const stats = buildWateringStats(req.db, { month, deviceId });
  if (scope === "daily" && !deviceId) {
    return res.status(400).json({ ok: false, error: "Dla eksportu dziennego podaj deviceId." });
  }

  const csv = scope === "daily"
    ? buildWateringDailyCsv(stats, deviceId)
    : buildWateringSummaryCsv(stats);
  const safeMonth = String(stats.month || "month").replace(/[^0-9-]/g, "");
  const safeDevice = deviceId ? deviceId.replace(/[^A-Za-z0-9_-]/g, "_") : "all";
  const fileName = `watering-stats_${safeMonth}_${safeDevice}_${scope}.csv`;
  res.setHeader("Content-Type", "text/csv; charset=utf-8");
  res.setHeader("Content-Disposition", `attachment; filename="${fileName}"`);
  res.send(`\uFEFF${csv}`);
});

app.get("/api/admin/users", authRequired, adminRequired, (req, res) => {
  const deviceRows = buildDeviceRows(req.db);
  const byDeviceId = new Map(deviceRows.map((d) => [d.deviceId, d]));
  const users = (req.db.users || []).map((u) => {
    const owned = getOwnedDeviceIds(req.db, u.id);
    const deviceId = owned.includes(u.selectedDeviceId) ? u.selectedDeviceId : (owned[0] || null);
    const dr = deviceId ? byDeviceId.get(deviceId) : null;
    const lastLoginMs = Date.parse(String(u.lastLoginAt || ""));
    return {
      id: u.id,
      email: u.email,
      lastLoginAt: Number.isFinite(lastLoginMs) ? new Date(lastLoginMs).toISOString() : null,
      deviceId,
      device: dr ? {
        online: dr.online,
        state: dr.state,
        lastSeen: dr.lastSeen,
        claimedAt: dr.claimedAt,
        hardware: dr.hardware,
        firmwareVersion: dr.firmwareVersion,
      } : null,
    };
  });
  users.sort((a, b) => {
    const aOnline = a.device?.online === true;
    const bOnline = b.device?.online === true;
    if (aOnline !== bOnline) return aOnline ? -1 : 1;
    return String(a.email).localeCompare(String(b.email), "pl");
  });
  res.json({ ok: true, timestamp: formatTs(), count: users.length, users });
});

app.get("/api/admin/ha/mqtt", authRequired, adminRequired, (req, res) => {
  const db = req.db;
  const usersById = new Map((db.users || []).map((u) => [u.id, u.email]));
  const devices = Object.entries(db.devices || {}).map(([deviceId, meta]) => ({
    deviceId,
    ownerUserId: String(meta?.ownerUserId || ""),
    ownerEmail: usersById.get(meta?.ownerUserId) || "",
    username: String(meta?.haMqtt?.username || ""),
    discoveryNode: String(meta?.haMqtt?.discoveryNode || ""),
    revision: Number(meta?.haMqtt?.revision || 0),
    rotatedAt: String(meta?.haMqtt?.rotatedAt || ""),
  }));
  devices.sort((a, b) => a.deviceId.localeCompare(b.deviceId, "pl"));
  res.json({
    ok: true,
    timestamp: formatTs(),
    broker: { host: HA_MQTT_HOST, port: HA_MQTT_PORT, tls: HA_MQTT_TLS, discoveryPrefix: HA_DISCOVERY_PREFIX },
    sync: { ...mqttAuthSyncState, pendingReason: mqttAuthSyncPendingReason || "" },
    devices,
  });
});

app.post("/api/admin/ha/mqtt/:deviceId/rotate", authRequired, adminRequired, (req, res) => {
  const deviceId = String(req.params?.deviceId || "").trim();
  const db = loadDb();
  if (!db.devices?.[deviceId]) {
    return res.status(404).json({ ok: false, error: "Urządzenie nie istnieje" });
  }
  const ensured = ensureHaMqttCredentials(db, deviceId, { rotate: true });
  if (!ensured.credentials) {
    return res.status(500).json({ ok: false, error: "Nie udało się obrócić hasła MQTT" });
  }
  saveDb(db);
  runMqttAuthSyncHook(`admin-ha-rotate:${deviceId}`);
  res.json(haMqttPayload(deviceId, ensured.credentials));
});

app.post("/api/admin/ha/mqtt/sync", authRequired, adminRequired, (_req, res) => {
  if (!MQTT_AUTH_SYNC_HOOK) {
    return res.status(400).json({
      ok: false,
      error: "Brak MQTT_AUTH_SYNC_HOOK w .env",
    });
  }
  runMqttAuthSyncHook("admin-manual");
  res.json({ ok: true, state: mqttAuthSyncState });
});

app.post("/api/admin/users/:id/reset-password", authRequired, adminRequired, async (req, res) => {
  const id = String(req.params?.id || "").trim();
  const db = loadDb();
  const user = (db.users || []).find((u) => u.id === id);
  if (!user) return res.status(404).json({ ok: false, error: "Użytkownik nie istnieje" });

  const requested = String(req.body?.password || "").trim();
  const password = requested.length >= 4 ? requested : makeTempPassword();
  user.passwordHash = await bcrypt.hash(password, 10);
  saveDb(db);
  res.json({ ok: true, userId: user.id, email: user.email, password });
});

app.post("/api/admin/users/:id/unclaim", authRequired, adminRequired, (req, res) => {
  const id = String(req.params?.id || "").trim();
  const db = loadDb();
  const user = (db.users || []).find((u) => u.id === id);
  if (!user) return res.status(404).json({ ok: false, error: "Użytkownik nie istnieje" });

  const owned = getOwnedDeviceIds(db, user.id);
  const deviceId = owned.includes(user.selectedDeviceId) ? user.selectedDeviceId : (owned[0] || null);
  if (!deviceId) return res.status(400).json({ ok: false, error: "Użytkownik nie ma przypisanego urządzenia" });

  if (db.devices?.[deviceId]) delete db.devices[deviceId];
  for (const u of db.users || []) {
    u.deviceIds = (u.deviceIds || []).filter((x) => x !== deviceId);
    if (u.selectedDeviceId === deviceId) u.selectedDeviceId = null;
  }
  saveDb(db);
  runMqttAuthSyncHook(`admin-unclaim:${deviceId}`);
  res.json({ ok: true, deviceId, userId: user.id, email: user.email });
});

app.post("/api/admin/users/:id/delete", authRequired, adminRequired, (req, res) => {
  const id = String(req.params?.id || "").trim();
  const db = loadDb();
  const user = (db.users || []).find((u) => u.id === id);
  if (!user) return res.status(404).json({ ok: false, error: "Użytkownik nie istnieje" });

  const owned = getOwnedDeviceIds(db, user.id);
  for (const deviceId of owned) {
    if (db.devices?.[deviceId]) delete db.devices[deviceId];
  }
  for (const u of db.users || []) {
    u.deviceIds = (u.deviceIds || []).filter((x) => !owned.includes(x));
    if (owned.includes(u.selectedDeviceId)) u.selectedDeviceId = null;
  }

  db.users = (db.users || []).filter((u) => u.id !== user.id);
  saveDb(db);
  if (owned.length > 0) runMqttAuthSyncHook(`admin-delete-user:${user.id}`);
  res.json({ ok: true, userId: user.id, email: user.email, removedDevices: owned });
});

app.post("/api/admin/firmware/upload", authRequired, adminRequired, express.raw({
  type: "application/octet-stream",
  // Keep in sync with Nginx `client_max_body_size`.
  limit: "20mb",
}), (req, res) => {
  const version = sanitizeVersion(req.query?.version || req.headers["x-firmware-version"]);
  const channel = sanitizeChannel(req.query?.channel || req.headers["x-firmware-channel"]);
  const target = sanitizeOtaTarget(req.query?.target || req.headers["x-firmware-target"]);
  const hardwareRaw = req.query?.hardware || req.headers["x-firmware-hardware"];
  let notes = "";
  const notesB64 = String(req.query?.notes_b64 || req.headers["x-firmware-notes-b64"] || "");
  if (notesB64) {
    try {
      notes = Buffer.from(notesB64, "base64").toString("utf8");
    } catch {
      notes = "";
    }
  } else {
    notes = String(req.headers["x-firmware-notes"] || "");
  }
  notes = String(notes).slice(0, 400);
  const fileNameRaw = String(req.query?.filename || req.headers["x-filename"] || "firmware.bin").trim() || "firmware.bin";
  const fileName = path.basename(fileNameRaw).replace(/[^A-Za-z0-9._-]/g, "_");
  const hardware = normalizeHardwareId(hardwareRaw) || inferHardwareFromFirmwareMeta({ hardware: hardwareRaw, fileName, notes, version });
  const body = req.body;

  if (!version) {
    return res.status(400).json({
      ok: false,
      error: "Nieprawidłowa wersja firmware (dozwolone: litery/cyfry oraz . _ -)",
    });
  }
  if (!Buffer.isBuffer(body) || body.length === 0) {
    return res.status(400).json({ ok: false, error: "Brak danych firmware" });
  }
  if (!hardware) {
    return res.status(400).json({
      ok: false,
      error: "Brak hardware firmware. Wybierz hardware: esp32, esp32c6, sonoff4ch albo bwshp6.",
    });
  }
  if (body.length < 128 * 1024) {
    return res.status(400).json({ ok: false, error: "Plik firmware jest za mały" });
  }

  const db = loadDb();
  const existing = (db.firmwares || []).find((f) =>
    String(f.version) === version &&
    String(f.channel) === channel &&
    normalizeHardwareId(f.hardware) === hardware &&
    sanitizeOtaTarget(f.target) === target
  );
  if (existing) {
    return res.status(409).json({ ok: false, error: `Firmware ${version} (${channel}, ${hardware}, ${target}) już istnieje` });
  }

  let signedMeta;
  try {
    signedMeta = signedOtaMetaFromBuffer(body, target);
  } catch (err) {
    const details = err?.message || "unknown";
    return res.status(500).json({
      ok: false,
      error: `Nie udało się podpisać OTA (${details}). Ustaw OTA_SIGN_PRIVATE_KEY_PATH lub OTA_SIGN_PRIVATE_KEY_PEM_B64.`,
    });
  }
  const id = `fw_${Date.now()}_${Math.floor(Math.random() * 100000)}`;
  const ext = path.extname(fileName).toLowerCase() === ".bin" ? ".bin" : ".bin";
  const finalName = `${id}${ext}`;
  const storagePath = path.join(FIRMWARE_DIR, finalName);
  fs.writeFileSync(storagePath, body);

  const firmware = {
    id,
    version,
    channel,
    hardware,
    notes,
    fileName,
    storedAs: finalName,
    target: signedMeta.target,
    size: signedMeta.size,
    sha256: signedMeta.sha256,
    signature: signedMeta.signature,
    signature_alg: signedMeta.signature_alg,
    signedAt: signedMeta.signedAt,
    uploadedAt: new Date().toISOString(),
  };
  db.firmwares = Array.isArray(db.firmwares) ? db.firmwares : [];
  db.firmwares.unshift(firmware);
  saveDb(db);
  res.json({ ok: true, firmware });
});

app.delete("/api/admin/firmwares/:id", authRequired, adminRequired, (req, res) => {
  const id = String(req.params?.id || "").trim();
  if (!id) return res.status(400).json({ ok: false, error: "Brak ID firmware" });

  const db = loadDb();
  db.firmwares = Array.isArray(db.firmwares) ? db.firmwares : [];
  db.otaCampaigns = Array.isArray(db.otaCampaigns) ? db.otaCampaigns : [];

  const idx = db.firmwares.findIndex((f) => String(f?.id || "") === id);
  if (idx < 0) {
    return res.status(404).json({ ok: false, error: "Firmware nie istnieje" });
  }
  const firmware = db.firmwares[idx];

  const activeStatuses = new Set(["queued", "sent", "downloading", "flashing", "rebooting"]);
  const blockingCampaigns = db.otaCampaigns.filter((c) =>
    String(c?.firmwareId || "") === id &&
    Array.isArray(c.devices) &&
    c.devices.some((d) => activeStatuses.has(String(d?.status || "")))
  );
  if (blockingCampaigns.length > 0) {
    return res.status(409).json({
      ok: false,
      error: `Nie można usunąć firmware używanego przez aktywną kampanię: ${blockingCampaigns.map((c) => String(c.id || "")).join(", ")}`,
    });
  }

  const storedAs = path.basename(String(firmware?.storedAs || ""));
  if (storedAs) {
    const firmwarePath = path.join(FIRMWARE_DIR, storedAs);
    if (fs.existsSync(firmwarePath)) {
      try {
        fs.unlinkSync(firmwarePath);
      } catch (err) {
        return res.status(500).json({
          ok: false,
          error: `Nie udało się usunąć pliku firmware (${storedAs}): ${String(err?.message || err)}`,
        });
      }
    }
  }

  db.firmwares.splice(idx, 1);
  saveDb(db);
  res.json({ ok: true, deleted: id });
});

app.post("/api/admin/ota/campaigns", authRequired, adminRequired, (req, res) => {
  const firmwareId = String(req.body?.firmwareId || "").trim();
  const target = String(req.body?.target || "all-online").trim();
  const selectedDeviceIds = Array.isArray(req.body?.deviceIds)
    ? req.body.deviceIds.map((x) => String(x || "").trim()).filter(Boolean)
    : [];

  const db = loadDb();
  const firmware = (db.firmwares || []).find((f) => f.id === firmwareId);
  if (!firmware) return res.status(404).json({ ok: false, error: "Firmware nie znaleziony" });
  try {
    ensureFirmwareSigned(firmware);
  } catch (err) {
    const msg = err?.message || "unknown";
    return res.status(500).json({
      ok: false,
      error: `Firmware nie ma poprawnego podpisu OTA (${msg}).`,
    });
  }
  const firmwareHardware = normalizeHardwareId(firmware.hardware || inferHardwareFromFirmwareMeta(firmware));
  if (!firmwareHardware) {
    return res.status(400).json({
      ok: false,
      error: "Firmware nie ma przypisanego hardware. Wgraj firmware ponownie z wyborem hardware (ESP32, ESP32-C6, SONOFF 4CH lub BW-SHP6).",
    });
  }
  firmware.hardware = firmwareHardware;

  const devices = buildDeviceRows(db);
  let requestedTargets = [];
  if (target === "selected") {
    requestedTargets = devices.filter((d) => selectedDeviceIds.includes(d.deviceId));
  } else {
    requestedTargets = devices.filter((d) => d.online);
  }
  if (!requestedTargets.length) {
    return res.status(400).json({ ok: false, error: "Brak urządzeń do kampanii" });
  }

  const hardwareReportRequired = isHardwareReportRequiredNow();
  const unknownHardware = requestedTargets.filter((d) => !normalizeHardwareId(d.hardware));
  if (unknownHardware.length > 0 && hardwareReportRequired) {
    return res.status(400).json({
      ok: false,
      error: `Nieznany hardware urządzeń: ${unknownHardware.map((d) => d.deviceId).join(", ")}. Urządzenia muszą raportować hardware (ESP32/ESP32-C6/SONOFF 4CH/BW-SHP6).`,
    });
  }

  const incompatible = requestedTargets.filter((d) => {
    const deviceHardware = normalizeHardwareId(d.hardware);
    if (!deviceHardware) return false;
    return deviceHardware !== firmwareHardware;
  });
  if (incompatible.length > 0) {
    return res.status(400).json({
      ok: false,
      error: `Niezgodny hardware. Firmware (${hardwareLabel(firmwareHardware)}) nie pasuje do: ${incompatible.map((d) => `${d.deviceId} [${hardwareLabel(d.hardware)}]`).join(", ")}.`,
    });
  }

  const targets = requestedTargets;
  const firmwareTarget = sanitizeOtaTarget(firmware.target);

  const campaign = {
    id: `camp_${Date.now()}_${Math.floor(Math.random() * 100000)}`,
    firmwareId: firmware.id,
    firmwareVersion: firmware.version,
    channel: firmware.channel,
    hardware: firmwareHardware,
    firmwareTarget,
    createdAt: new Date().toISOString(),
    createdBy: req.user.email || "admin",
    target,
    status: "queued",
    devices: targets.map((d) => ({
      deviceId: d.deviceId,
      ownerEmail: d.ownerEmail,
      hardware: d.hardware,
      status: d.online ? "queued" : "offline_skipped",
      progress: d.online ? 0 : null,
      updatedAt: new Date().toISOString(),
      note: d.online ? "" : "Urządzenie offline w chwili tworzenia kampanii",
    })),
  };

  const firmwareUrl = `${PUBLIC_BASE_URL}/fw/${encodeURIComponent(firmware.id)}.bin`;
  const nowTs = formatTs();
  for (const item of campaign.devices) {
    if (item.status !== "queued") continue;
    const commandId = makeCommandId();
    const ok = mqttPublish(`${baseTopic(item.deviceId)}/cmd/ota/start`, {
      command_id: commandId,
      timestamp: nowTs,
      campaign_id: campaign.id,
      firmware_id: firmware.id,
      version: firmware.version,
      channel: firmware.channel,
      hardware: firmwareHardware,
      target: firmwareTarget,
      sha256: firmware.sha256,
      signature: firmware.signature,
      signature_alg: firmware.signature_alg || OTA_SIGNATURE_ALG,
      size: firmware.size,
      url: firmwareUrl,
    });
    if (ok) {
      item.status = "sent";
      item.note = "Komenda OTA wysłana";
      item.updatedAt = new Date().toISOString();
    } else {
      item.status = "publish_failed";
      item.note = "Nie udało się opublikować komendy MQTT";
      item.updatedAt = new Date().toISOString();
    }
  }
  campaign.status = recomputeCampaignStatus(campaign);

  db.otaCampaigns = Array.isArray(db.otaCampaigns) ? db.otaCampaigns : [];
  db.otaCampaigns.unshift(campaign);
  saveDb(db);
  res.json({ ok: true, campaign });
});

app.post("/api/admin/ota/campaigns/:id/cancel", authRequired, adminRequired, (req, res) => {
  const id = String(req.params?.id || "").trim();
  const db = loadDb();
  const campaign = (db.otaCampaigns || []).find((c) => c.id === id);
  if (!campaign) return res.status(404).json({ ok: false, error: "Kampania nie istnieje" });
  if (campaign.status === "cancelled") return res.json({ ok: true, campaign });

  campaign.status = "cancelled";
  const now = new Date().toISOString();
  for (const item of campaign.devices || []) {
    if (["queued", "sent", "downloading", "flashing", "rebooting"].includes(String(item.status || ""))) {
      item.status = "cancelled";
      item.updatedAt = now;
      item.note = "Anulowano przez administratora";
    }
  }
  saveDb(db);
  res.json({ ok: true, campaign });
});

app.delete("/api/admin/ota/campaigns/:id", authRequired, adminRequired, (req, res) => {
  const id = String(req.params?.id || "").trim();
  const db = loadDb();
  const items = Array.isArray(db.otaCampaigns) ? db.otaCampaigns : [];
  const before = items.length;
  db.otaCampaigns = items.filter((c) => String(c?.id || "") !== id);
  if (db.otaCampaigns.length === before) {
    return res.status(404).json({ ok: false, error: "Kampania nie istnieje" });
  }
  saveDb(db);
  res.json({ ok: true, deleted: id });
});

// ---- API zgodne z panelem ESP32 (live)
app.get("/api/status", authRequired, (req, res) => {
  const deviceId = withDevice(req, res);
  if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  const src = state.status || {};
  const telem = (state.plugTelemetry && typeof state.plugTelemetry === "object") ? state.plugTelemetry : {};
  const pairState = String(src.pair_state || state.settingsPublic?.pair_state || "").trim() || null;
  res.json({
    wifi: src.wifi || (isDeviceOnline(state) ? "Połączono" : "Brak połączenia"),
    ip: src.ip || "-",
    time: src.time || new Date().toISOString().slice(0, 16).replace("T", " "),
    device_id: deviceId,
    // Prefer the version last reported by the device itself; OTA metadata is only a fallback.
    fw_version: resolveDeviceFirmwareVersion(state),
    online: isDeviceOnline(state),
    device_type: String(src.device_type || ""),
    pair_state: pairState,
    power_w: Number(telem.power_w ?? src.power_w ?? 0) || 0,
    energy_total_kwh: Number(telem.energy_total_kwh ?? src.energy_total_kwh ?? 0) || 0,
  });
});

function buildUnifiedWeatherSnapshot(state) {
  const weatherSrc = (state?.weather && typeof state.weather === "object") ? state.weather : {};
  const percentSrc = (state?.wateringPercent && typeof state.wateringPercent === "object") ? state.wateringPercent : {};
  const out = { ...weatherSrc };
  const weatherAtMs = isoToMs(state?.weatherAt);
  const percentAtMs = isoToMs(state?.wateringPercentAt);
  const preferPercent = Number.isFinite(percentAtMs) && (!Number.isFinite(weatherAtMs) || percentAtMs >= weatherAtMs);

  const upsertFinite = (srcKey, dstKey, force = false) => {
    const srcVal = safeNumber(percentSrc?.[srcKey], NaN);
    const dstVal = safeNumber(out?.[dstKey], NaN);
    if (!Number.isFinite(srcVal)) return;
    if (force || !Number.isFinite(dstVal)) out[dstKey] = srcVal;
  };

  const upsertDecisionFields = (force = false) => {
    upsertFinite("temp_now", "temp", force);
    upsertFinite("humidity_now", "humidity", force);
    upsertFinite("wind_now_kmh", "wind_kmh", force);
    upsertFinite("rain_24h", "rain_24h_observed", force);
    upsertFinite("rain_24h", "rain_24h", force);
  };

  if (preferPercent) upsertDecisionFields(true);
  upsertDecisionFields(false);

  const fromHistory = Array.isArray(state?.rainHistory) ? sumRainHistory(state.rainHistory, 24) : NaN;
  const rainHistoryAtMs = isoToMs(state?.rainHistoryAt);
  const rainHistoryFresh = Number.isFinite(rainHistoryAtMs)
    && (Date.now() - rainHistoryAtMs) <= SMART_CLIMATE_RAIN_HISTORY_MAX_AGE_MS;
  let observedMm = NaN;
  if (Number.isFinite(fromHistory) && rainHistoryFresh) {
    // Keep UI coherent with rain-history driven analytics and charts.
    observedMm = Math.max(0, fromHistory);
  } else {
    const observedRainCandidates = [
      safeNumber(out.rain_24h_observed, NaN),
      safeNumber(out.rain_24h, NaN),
      safeNumber(percentSrc.rain_24h, NaN),
      fromHistory,
    ].filter((v) => Number.isFinite(v));
    if (observedRainCandidates.length) {
      observedMm = Math.max(...observedRainCandidates.map((v) => Math.max(0, v)));
    }
  }
  if (Number.isFinite(observedMm)) {
    const mm = Number(observedMm.toFixed(1));
    out.rain_24h_observed = mm;
    out.rain_24h = mm;
  }

  const forecastRain = safeNumber(out.rain_24h_forecast, NaN);
  if (Number.isFinite(forecastRain)) {
    const mm = Number(Math.max(0, forecastRain).toFixed(1));
    out.rain_24h_forecast = mm;
  }

  const windKmhRaw = safeNumber(out.wind_kmh, NaN);
  const windMsRaw = safeNumber(out.wind, NaN);
  const normalizedWindKmh = Number.isFinite(windKmhRaw)
    ? Math.max(0, windKmhRaw)
    : (Number.isFinite(windMsRaw) ? Math.max(0, windMsRaw * 3.6) : NaN);
  if (Number.isFinite(normalizedWindKmh)) {
    out.wind_kmh = Number(normalizedWindKmh.toFixed(1));
    // Keep legacy "wind" field aligned with cloud UI, which displays km/h.
    out.wind = out.wind_kmh;
  }

  return out;
}

app.get("/api/weather", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  res.json(buildUnifiedWeatherSnapshot(state));
});

app.get("/api/zones", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const zones = Array.isArray(state.zones) ? state.zones : [];
  if (isSmartPlugDeviceState(state)) {
    return res.json(zones);
  }
  const includeRaw = String(req.query?.include_virtual_plugs || "").trim().toLowerCase();
  const includeHdrRaw = String(req.headers["x-wms-schedules"] || "").trim().toLowerCase();
  const includeVirtualPlugs =
    includeRaw === "1" || includeRaw === "true" || includeRaw === "yes" || includeRaw === "on"
    || includeHdrRaw === "1" || includeHdrRaw === "true" || includeHdrRaw === "yes" || includeHdrRaw === "on";
  if (!includeVirtualPlugs) {
    return res.json(zones);
  }
  const catalog = buildVirtualPlugCatalog(req.db, req.user.id, { excludeDeviceId: deviceId });
  if (!catalog.virtualZones.length) {
    return res.json(zones);
  }
  return res.json([...zones, ...catalog.virtualZones]);
});

app.get("/api/programs", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const merged = buildProgramsResponseWithVirtualPlugs(req.db, req.user.id, deviceId, state);
  res.json(merged);
});

app.get("/api/programs/export", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const merged = buildProgramsResponseWithVirtualPlugs(req.db, req.user.id, deviceId, state);
  res.json(merged);
});

app.get("/api/zones-names", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const zones = Array.isArray(state.zones) ? state.zones : [];
  res.json({ names: zones.map((z, i) => z?.name || `Strefa ${i + 1}`) });
});

app.get("/api/logs", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  res.json(normalizeLogsPayload(state.logs));
});

app.get("/api/settings", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const src = state.settingsPublic || {};
  const zones = Array.isArray(state.zones) ? state.zones : [];
  const topicBase = String(src.mqttTopicBase || src.mqttTopic || `${MQTT_TOPIC_PREFIX}/${deviceId}`);
  const mqttServer = String(src.mqttServer || src.mqttBroker || "");
  const rawZoneCount = Number(src.zoneCount || zones.length || 8);
  const zoneCount = Math.max(1, Math.min(8, Number.isFinite(rawZoneCount) ? Math.round(rawZoneCount) : 8));
  const out = {
    ...src,
    zoneCount,
    mqttTopicBase: topicBase,
    mqttTopic: String(src.mqttTopic || topicBase),
    mqttServer,
    mqttBroker: String(src.mqttBroker || mqttServer),
    mqttPort: Number(src.mqttPort || 8883),
    mqttUser: String(src.mqttUser || ""),
    mqttClientId: String(src.mqttClientId || `wm-sprinkler-${deviceId.toLowerCase()}`),
    mqttPass: "",
    owmApiKeyConfigured: !!src.owmApiKeyConfigured,
    mqttPassConfigured: !!src.mqttPassConfigured,
    pushoverUserConfigured: !!src.pushoverUserConfigured,
    pushoverTokenConfigured: !!src.pushoverTokenConfigured,
    enableMqtt: src.enableMqtt !== false,
  };
  res.json(out);
});

app.post("/api/zones", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const id = Number(req.body?.id);
  const toggle = !!req.body?.toggle;
  const maxZoneId = Array.isArray(state.zones) && state.zones.length > 0 ? state.zones.length - 1 : 7;
  if (!Number.isInteger(id) || id < 0 || id > maxZoneId || !toggle) {
    return apiError(req, res, 400, "invalidData");
  }
  let cmdResult = null;
  try {
    cmdResult = await publishCommandWithFastAck(
      deviceId,
      `cmd/zones/${id}/toggle`,
      { action: "toggle" },
      { allowAckTimeout: true }
    );
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  mqttPublish(`${baseTopic(deviceId)}/global/refresh`, "");
  const dbAfter = loadDb();
  const stateAfter = getDeviceState(dbAfter, deviceId);
  if (cmdResult?.ack?.status === "pending") {
    res.set("X-WMS-Ack", "pending");
  }
  res.json(Array.isArray(stateAfter.zones) ? stateAfter.zones : []);
});

app.post("/api/zones-names", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const names = req.body?.names;
  if (!Array.isArray(names)) return apiError(req, res, 400, "namesArrayRequired");
  try {
    await publishCommandWithAck(deviceId, "cmd/zones-names/set", { names });
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true });
});

app.post("/api/programs", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const selectedIsPlug = isSmartPlugDeviceState(state);
  const scheduleTimezone = resolveScheduleTimezone(req, state);
  const catalog = selectedIsPlug ? null : buildVirtualPlugCatalog(req.db, req.user.id, { excludeDeviceId: deviceId });
  const managedZones = selectedIsPlug ? new Set() : getManagedSmartClimateZoneSet(req.db, deviceId);
  const managedMessage = "Strefa jest sterowana przez WM Smart Climate. Zmień konfigurację algorytmu, aby edytować harmonogram.";
  try {
    if (Array.isArray(req.body)) {
      if (selectedIsPlug) {
        await publishCommandWithAck(deviceId, "cmd/programs/import", { programs: req.body, timezone: scheduleTimezone });
      } else {
        const touchesManaged = req.body.some((program) => managedZones.has(Number(program?.zone)));
        if (touchesManaged) return res.status(409).json({ ok: false, error: managedMessage });
        const centralPrograms = [];
        const plugProgramsByDevice = new Map();
        for (const program of req.body) {
          const zoneCode = decodeVirtualZoneCode(program?.zone);
          if (zoneCode === null) {
            centralPrograms.push(program);
            continue;
          }
          const plugDeviceId = catalog?.codeToDeviceId.get(zoneCode);
          if (!plugDeviceId) return apiError(req, res, 400, "invalidData");
          if (!plugProgramsByDevice.has(plugDeviceId)) plugProgramsByDevice.set(plugDeviceId, []);
          plugProgramsByDevice.get(plugDeviceId).push(makePlugProgramPayload(program));
        }
        await publishCommandWithAck(deviceId, "cmd/programs/import", { programs: centralPrograms });
        for (const [plugDeviceId, programs] of plugProgramsByDevice.entries()) {
          const plugState = getDeviceState(req.db, plugDeviceId);
          if (!requireDeviceOnlineOrFail(res, plugState)) return;
          await publishCommandWithAck(plugDeviceId, "cmd/programs/import", { programs, timezone: scheduleTimezone });
        }
      }
    } else {
      if (!selectedIsPlug && managedZones.has(Number(req.body?.zone))) {
        return res.status(409).json({ ok: false, error: managedMessage });
      }
      const zoneCode = decodeVirtualZoneCode(req.body?.zone);
      if (!selectedIsPlug && zoneCode !== null) {
        const plugDeviceId = catalog?.codeToDeviceId.get(zoneCode);
        if (!plugDeviceId) return apiError(req, res, 400, "invalidData");
        const plugState = getDeviceState(req.db, plugDeviceId);
        if (!requireDeviceOnlineOrFail(res, plugState)) return;
        await publishCommandWithAck(plugDeviceId, "cmd/programs/add", {
          program: makePlugProgramPayload(req.body),
          timezone: scheduleTimezone,
        });
      } else {
        const payload = { program: req.body };
        if (selectedIsPlug) payload.timezone = scheduleTimezone;
        await publishCommandWithAck(deviceId, "cmd/programs/add", payload);
      }
    }
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true });
});

app.post("/api/programs/import", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const scheduleTimezone = resolveScheduleTimezone(req, state);
  if (!Array.isArray(req.body)) {
    return apiError(req, res, 400, "programArrayExpected");
  }
  const selectedIsPlug = isSmartPlugDeviceState(state);
  const managedZones = selectedIsPlug ? new Set() : getManagedSmartClimateZoneSet(req.db, deviceId);
  if (!selectedIsPlug && req.body.some((program) => managedZones.has(Number(program?.zone)))) {
    return res.status(409).json({
      ok: false,
      error: "Strefa jest sterowana przez WM Smart Climate. Zmień konfigurację algorytmu, aby edytować harmonogram.",
    });
  }
  const catalog = selectedIsPlug ? null : buildVirtualPlugCatalog(req.db, req.user.id, { excludeDeviceId: deviceId });
  const centralPrograms = [];
  const plugProgramsByDevice = new Map();
  if (!selectedIsPlug) {
    for (const program of req.body) {
      const zoneCode = decodeVirtualZoneCode(program?.zone);
      if (zoneCode === null) {
        centralPrograms.push(program);
        continue;
      }
      const plugDeviceId = catalog?.codeToDeviceId.get(zoneCode);
      if (!plugDeviceId) return apiError(req, res, 400, "invalidData");
      if (!plugProgramsByDevice.has(plugDeviceId)) plugProgramsByDevice.set(plugDeviceId, []);
      plugProgramsByDevice.get(plugDeviceId).push(makePlugProgramPayload(program));
    }
  }
  try {
    if (selectedIsPlug) {
      await publishCommandWithAck(deviceId, "cmd/programs/import", { programs: req.body, timezone: scheduleTimezone });
    } else {
      await publishCommandWithAck(deviceId, "cmd/programs/import", { programs: centralPrograms });
      for (const [plugDeviceId, programs] of plugProgramsByDevice.entries()) {
        const plugState = getDeviceState(req.db, plugDeviceId);
        if (!requireDeviceOnlineOrFail(res, plugState)) return;
        await publishCommandWithAck(plugDeviceId, "cmd/programs/import", { programs, timezone: scheduleTimezone });
      }
    }
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true });
});

app.put("/api/programs/:id", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const scheduleTimezone = resolveScheduleTimezone(req, state);
  const id = Number(req.params.id);
  if (!Number.isInteger(id) || id < 0) return res.status(400).json({ ok: false });
  const selectedIsPlug = isSmartPlugDeviceState(state);
  const managedZones = selectedIsPlug ? new Set() : getManagedSmartClimateZoneSet(req.db, deviceId);
  const virtual = decodeVirtualProgramId(id);
  const catalog = (!selectedIsPlug && virtual)
    ? buildVirtualPlugCatalog(req.db, req.user.id, { excludeDeviceId: deviceId })
    : null;
  try {
    if (!selectedIsPlug && virtual) {
      const plugDeviceId = catalog?.codeToDeviceId.get(virtual.code);
      if (!plugDeviceId) return apiError(req, res, 400, "invalidData");
      const plugState = getDeviceState(req.db, plugDeviceId);
      if (!requireDeviceOnlineOrFail(res, plugState)) return;
      await publishCommandWithAck(plugDeviceId, `cmd/programs/edit/${virtual.index}`, {
        program: makePlugProgramPayload(req.body),
        timezone: scheduleTimezone,
      });
    } else {
      if (!selectedIsPlug) {
        const rawPrograms = Array.isArray(state.programs) ? state.programs : [];
        const existing = rawPrograms.find((p, idx) => Number(p?.id) === id || idx === id) || null;
        const targetZone = Number(req.body?.zone ?? existing?.zone);
        if (managedZones.has(targetZone)) {
          return res.status(409).json({
            ok: false,
            error: "Strefa jest sterowana przez WM Smart Climate. Zmień konfigurację algorytmu, aby edytować harmonogram.",
          });
        }
      }
      const payload = { program: req.body };
      if (selectedIsPlug) payload.timezone = scheduleTimezone;
      await publishCommandWithAck(deviceId, `cmd/programs/edit/${id}`, payload);
    }
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true });
});

app.delete("/api/programs", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const scheduleTimezone = resolveScheduleTimezone(req, state);
  const id = Number(req.query.id);
  if (!Number.isInteger(id) || id < 0) return res.status(400).json({ ok: false });
  const selectedIsPlug = isSmartPlugDeviceState(state);
  const virtual = decodeVirtualProgramId(id);
  const catalog = (!selectedIsPlug && virtual)
    ? buildVirtualPlugCatalog(req.db, req.user.id, { excludeDeviceId: deviceId })
    : null;
  try {
    if (!selectedIsPlug && virtual) {
      const plugDeviceId = catalog?.codeToDeviceId.get(virtual.code);
      if (!plugDeviceId) return apiError(req, res, 400, "invalidData");
      const plugState = getDeviceState(req.db, plugDeviceId);
      if (!requireDeviceOnlineOrFail(res, plugState)) return;
      const rawPrograms = Array.isArray(plugState.programs) ? plugState.programs : [];
      const programs = normalizeProgramsArray(rawPrograms).map((p) => makePlugProgramPayload(p));
      if (!Number.isInteger(virtual.index) || virtual.index < 0 || virtual.index >= programs.length) {
        return apiError(req, res, 400, "invalidData");
      }
      const filtered = programs.filter((_item, idx) => idx !== virtual.index);
      await publishCommandWithAck(plugDeviceId, "cmd/programs/import", { programs: filtered, timezone: scheduleTimezone });
      mqttPublish(`${baseTopic(plugDeviceId)}/global/refresh`, "");
    } else {
      await publishCommandWithAck(deviceId, `cmd/programs/delete/${id}`, { action: "delete" });
    }
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true });
});

app.delete("/api/logs", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  try {
    await publishCommandWithAck(deviceId, "cmd/logs/clear", { action: "clear" });
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true });
});

app.post("/api/settings", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const incoming = (req.body && typeof req.body === "object") ? req.body : {};
  const mappedSettings = { ...incoming };
  // Map legacy aliases only when they are explicitly provided.
  // Do not inject empty/default MQTT fields for partial updates (e.g. zoneCount only),
  // because that would overwrite valid MQTT config on the device.
  if (Object.prototype.hasOwnProperty.call(incoming, "mqttBroker")
      && !Object.prototype.hasOwnProperty.call(incoming, "mqttServer")) {
    mappedSettings.mqttServer = incoming.mqttBroker;
  }
  if (Object.prototype.hasOwnProperty.call(incoming, "mqttTopicBase")
      && !Object.prototype.hasOwnProperty.call(incoming, "mqttTopic")) {
    mappedSettings.mqttTopic = incoming.mqttTopicBase;
  }
  if (typeof mappedSettings.ssid === "string" && mappedSettings.ssid.length === 0) {
    delete mappedSettings.ssid;
  }
  if (typeof mappedSettings.password === "string") {
    if (mappedSettings.password.length > 0 && typeof mappedSettings.pass !== "string") {
      mappedSettings.pass = mappedSettings.password;
    }
    delete mappedSettings.password;
  }
  if (typeof mappedSettings.pass === "string" && mappedSettings.pass.length === 0) {
    delete mappedSettings.pass;
  }
  delete mappedSettings.mqttBroker;
  delete mappedSettings.mqttTopicBase;
  try {
    await publishCommandWithAck(deviceId, "cmd/settings/set", { settings: mappedSettings });
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true });
});

app.get("/api/rain-history", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  res.json(state.rainHistory || []);
});

app.get("/api/rain/stats", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  const grouping = normalizeRainStatsGrouping(req.query?.grouping);
  const stats = buildRainStats(state, { grouping });
  res.json({
    ok: true,
    timestamp: formatTs(),
    device_id: deviceId,
    ...stats,
  });
});

app.get("/api/rain/stats/export", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  const grouping = normalizeRainStatsGrouping(req.query?.grouping);
  const stats = buildRainStats(state, { grouping });
  const csv = buildRainStatsCsv(stats);
  const safeDevice = deviceId.replace(/[^A-Za-z0-9_-]/g, "_");
  const fileName = `rain-stats_${safeDevice}_${grouping}.csv`;
  res.setHeader("Content-Type", "text/csv; charset=utf-8");
  res.setHeader("Content-Disposition", `attachment; filename=\"${fileName}\"`);
  res.send(`\uFEFF${csv}`);
});

app.get("/api/watering-percent", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  res.json(state.wateringPercent || { percent: 100 });
});

app.get("/api/smart-climate", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  if (!isSmartClimateAllowedForDevice(req.db, deviceId, state)) {
    const payload = smartClimateAccessErrorPayload(state);
    return res.status(payload.status).json(payload.body);
  }
  const zones = Array.isArray(state.zones) ? state.zones : [];
  const cfg = ensureSmartClimateConfig(req.db, deviceId, zones.length);
  saveDb(req.db);
  return res.json(buildSmartClimateApiPayload(req.db, deviceId, state, cfg));
});

app.post("/api/smart-climate", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  if (!isSmartClimateAllowedForDevice(req.db, deviceId, state)) {
    const payload = smartClimateAccessErrorPayload(state);
    return res.status(payload.status).json(payload.body);
  }
  const zones = Array.isArray(state.zones) ? state.zones : [];
  const cfg = ensureSmartClimateConfig(req.db, deviceId, zones.length);
  const body = (req.body && typeof req.body === "object") ? req.body : {};
  cfg.enabled = body.enabled !== false;
  cfg.name = String(body.name || SMART_CLIMATE_NAME).slice(0, 80) || SMART_CLIMATE_NAME;
  cfg.profile = normalizeSmartClimateProfile(body.profile || cfg.profile);
  cfg.timezone = String(body.timezone || cfg.timezone || "").trim();
  cfg.latitude = clampNumber(body.latitude ?? cfg.latitude ?? SMART_CLIMATE_DEFAULT_LAT, -89.5, 89.5);
  cfg.altitudeM = clampNumber(body.altitudeM ?? cfg.altitudeM ?? SMART_CLIMATE_DEFAULT_ALTITUDE_M, -100, 5000);

  if (Array.isArray(body.zones)) {
    const nextZones = {};
    for (const item of body.zones) {
      const zoneId = Number(item?.id);
      if (!Number.isInteger(zoneId) || zoneId < 0 || zoneId >= zones.length) continue;
      const prev = cfg.zones?.[String(zoneId)] || null;
      const normalized = normalizeSmartClimateZoneConfig(zoneId, item, prev);
      if (!normalized || normalized.enabled !== true) continue;
      nextZones[String(zoneId)] = normalized;
    }
    cfg.zones = nextZones;
  } else {
    const normalizedMap = {};
    for (const [zoneKey, zoneCfg] of Object.entries(cfg.zones || {})) {
      const normalized = normalizeSmartClimateZoneConfig(Number(zoneKey), zoneCfg, zoneCfg);
      if (!normalized || normalized.enabled !== true) continue;
      if (normalized.id >= zones.length) continue;
      normalizedMap[String(normalized.id)] = normalized;
    }
    cfg.zones = normalizedMap;
  }
  if (!Object.keys(cfg.zones || {}).length) cfg.enabled = false;
  cfg.updatedAt = new Date().toISOString();

  try {
    const sync = await syncSmartClimateProgramsForDevice(req.db, deviceId, cfg, state);
    if (sync.removed > 0) {
      appendSmartClimateHistory(cfg, {
        ts: new Date().toISOString(),
        level: "info",
        action: "config_update",
        detail: `Tryb Smart Climate przejął ${Object.keys(cfg.zones || {}).length} stref(y).`,
      });
    }
  } catch (err) {
    cfg.pendingProgramSync = true;
    cfg.lastProgramSyncError = String(err?.message || err);
  }

  saveDb(req.db);
  try {
    await runSmartClimateEngine({ force: true, onlyDeviceId: deviceId });
  } catch (err) {
    logEvent(`[SMART-CLIMATE] force evaluation failed: ${String(err?.message || err)}`, "ERROR");
  }
  const dbAfter = loadDb();
  const stateAfter = getDeviceState(dbAfter, deviceId);
  const cfgAfter = ensureSmartClimateConfig(dbAfter, deviceId, Array.isArray(stateAfter.zones) ? stateAfter.zones.length : 0);
  saveDb(dbAfter);
  return res.json(buildSmartClimateApiPayload(dbAfter, deviceId, stateAfter, cfgAfter));
});

app.post("/api/smart-climate/evaluate", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  if (!isSmartClimateAllowedForDevice(req.db, deviceId, state)) {
    const payload = smartClimateAccessErrorPayload(state);
    return res.status(payload.status).json(payload.body);
  }
  try {
    await runSmartClimateEngine({ force: true, onlyDeviceId: deviceId });
  } catch (err) {
    return res.status(500).json({ ok: false, error: String(err?.message || err) });
  }
  const db = loadDb();
  const stateAfter = getDeviceState(db, deviceId);
  const cfg = ensureSmartClimateConfig(db, deviceId, Array.isArray(stateAfter.zones) ? stateAfter.zones.length : 0);
  saveDb(db);
  res.json(buildSmartClimateApiPayload(db, deviceId, stateAfter, cfg));
});

app.get("/api/plug/telemetry", authRequired, (req, res) => {
  const candidate = String(req.query?.device_id || req.body?.device_id || "").trim();
  const deviceId = resolveOwnedDeviceId(req, res, candidate, { allowAutoSelect: true }); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  const telem = (state.plugTelemetry && typeof state.plugTelemetry === "object") ? state.plugTelemetry : {};
  const zones = Array.isArray(state.zones) ? state.zones : [];
  const zone0 = zones[0] || {};
  const relayOn = telem.relay_on === true || zoneIsActive(zone0);
  const fallback = {
    relay_on: relayOn,
    remaining_sec: Number(zone0?.remaining || 0) || 0,
    power_w: Number(state.status?.power_w || 0) || 0,
    energy_total_kwh: Number(state.status?.energy_total_kwh || 0) || 0,
    sampled_at: state.lastSeen || null,
  };
  res.json({ ...fallback, ...telem, relay_on: relayOn, device_id: deviceId });
});

app.get("/api/plugs", authRequired, (req, res) => {
  const owned = getOwnedDeviceIds(req.db, req.user.id);
  const plugs = [];
  for (const deviceId of owned) {
    const state = getDeviceState(req.db, deviceId);
    if (!isSmartPlugDeviceState(state)) continue;
    const status = (state.status && typeof state.status === "object") ? state.status : {};
    const telem = (state.plugTelemetry && typeof state.plugTelemetry === "object") ? state.plugTelemetry : {};
    const zones = Array.isArray(state.zones) ? state.zones : [];
    const zone0 = zones[0] || {};
    const relayOn = telem.relay_on === true || zoneIsActive(zone0);
    const meta = req.db.devices?.[deviceId] || {};
    const statusIp = String(status.ip || "").trim();
    const fallbackIp = String(state.lastKnownIp || "").trim();
    const effectiveIp = !isMissingTextValue(statusIp) ? statusIp : (!isMissingTextValue(fallbackIp) ? fallbackIp : "");
    plugs.push({
      device_id: deviceId,
      online: isDeviceOnline(state),
      relay_on: relayOn,
      remaining_sec: Number(telem.remaining_sec ?? zone0?.remaining ?? 0) || 0,
      power_w: Number(telem.power_w ?? status?.power_w ?? 0) || 0,
      voltage_v: Number(telem.voltage_v ?? 0) || 0,
      current_a: Number(telem.current_a ?? 0) || 0,
      energy_total_kwh: Number(telem.energy_total_kwh ?? status?.energy_total_kwh ?? 0) || 0,
      pair_state: String(status.pair_state || state.settingsPublic?.pair_state || ""),
      ip: effectiveIp,
      fw_version: resolveDeviceFirmwareVersion(state, meta),
      last_seen: state.lastSeen || null,
      sampled_at: String(telem.sampled_at || state.lastSeen || ""),
      assigned_account: String(status.assigned_account || state.settingsPublic?.assigned_account || ""),
    });
  }
  plugs.sort((a, b) => String(a.device_id).localeCompare(String(b.device_id), "pl"));
  res.json({ ok: true, count: plugs.length, plugs });
});

app.post("/api/plug/set", authRequired, async (req, res) => {
  const candidate = String(req.body?.device_id || req.query?.device_id || "").trim();
  const deviceId = resolveOwnedDeviceId(req, res, candidate, { allowAutoSelect: true }); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  if (!isSmartPlugDeviceState(state)) {
    return apiError(req, res, 400, "invalidData");
  }

  const mode = String(req.body?.mode || "").trim().toLowerCase();
  const onRaw = req.body?.on;
  let on = null;
  if (mode === "toggle") on = !zoneIsActive((Array.isArray(state.zones) ? state.zones[0] : {}) || {});
  else if (typeof onRaw === "boolean") on = onRaw;
  else if (typeof onRaw === "number") on = onRaw !== 0;
  else if (typeof onRaw === "string") {
    const v = onRaw.trim().toLowerCase();
    if (v === "1" || v === "true" || v === "on") on = true;
    else if (v === "0" || v === "false" || v === "off") on = false;
  }
  if (on === null) return apiError(req, res, 400, "invalidData");

  let seconds = Number(req.body?.seconds);
  if (!Number.isFinite(seconds) || seconds < 0) seconds = 0;
  seconds = Math.round(Math.min(seconds, 7 * 24 * 3600));

  let cmdResult = null;
  try {
    cmdResult = await publishCommandWithFastAck(deviceId, "cmd/plug/set", {
      action: "plug_set",
      on,
      seconds,
    });
  } catch (err) {
    return sendCommandError(req, res, err);
  }

  try {
    const dbNow = loadDb();
    const plugState = getDeviceState(dbNow, deviceId);
    plugState.lastPlugEventHint = {
      mode: "RECZNY (cloud)",
      source: "api/plug/set",
      expiresAt: new Date(Date.now() + 120000).toISOString(),
    };
    dbNow.deviceStates[deviceId] = plugState;
    saveDb(dbNow);
  } catch (err) {
    logEvent(`[PLUG-NOTIFY] hint save failed plug=${deviceId} err=${String(err?.message || err)}`, "WARN");
  }

  mqttPublish(`${baseTopic(deviceId)}/global/refresh`, "");
  if (cmdResult?.ack?.status === "pending") {
    res.set("X-WMS-Ack", "pending");
  }
  res.json({ ok: true, device_id: deviceId, on, seconds, ack: cmdResult?.ack?.status || "accepted" });
});

app.get("/api/pair/state", authRequired, (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId);
  const status = (state.status && typeof state.status === "object") ? state.status : {};
  const settingsPublic = (state.settingsPublic && typeof state.settingsPublic === "object") ? state.settingsPublic : {};
  const pairState = String(status.pair_state || settingsPublic.pair_state || "unassigned").trim() || "unassigned";
  res.json({
    ok: true,
    device_id: deviceId,
    online: isDeviceOnline(state),
    pair_state: pairState,
    assigned_to_wms: pairState === "assigned",
    provisioning_method: String(status.provisioning_method || settingsPublic.provisioningMethod || "wifi_ap"),
    ble_supported: false,
  });
});

app.post("/api/pair/start", authRequired, async (req, res) => {
  const deviceId = withDevice(req, res); if (!deviceId) return;
  const state = getDeviceState(req.db, deviceId); if (!requireDeviceOnlineOrFail(res, state)) return;
  try {
    await publishCommandWithAck(deviceId, "cmd/pair/start", { action: "pair_start" });
  } catch (err) {
    return sendCommandError(req, res, err);
  }
  res.json({ ok: true, device_id: deviceId });
});

app.get("/health", (_req, res) => {
  const status = {
    ok: mqttClient.connected,
    timestamp: formatTs(),
    uptime_sec: Math.floor(process.uptime()),
    mqtt: {
      connected: mqttClient.connected,
      broker: MQTT_URL,
      last_event_at: lastMqttEventTs,
    },
    mqtt_auth_sync: {
      hook_enabled: !!MQTT_AUTH_SYNC_HOOK,
      running: mqttAuthSyncState.running,
      pending_reason: mqttAuthSyncPendingReason || "",
      last_ok_at: mqttAuthSyncState.lastOkAt,
      last_error_at: mqttAuthSyncState.lastErrorAt,
      last_error: mqttAuthSyncState.lastError,
      last_reason: mqttAuthSyncState.lastReason,
      last_duration_ms: mqttAuthSyncState.lastDurationMs,
    },
  };
  res.status(status.ok ? 200 : 503).json(status);
});

// ---- UI routes
app.get("/login", (_req, res) => {
  return sendHtmlFile(res, path.join(__dirname, "..", "public", "login.html"));
});

app.get("/devices", authRequired, (req, res) => {
  if (isAdminUser(req.user)) return res.redirect("/admin");
  return sendHtmlFile(res, path.join(__dirname, "..", "public", "devices.html"));
});

app.get("/admin", authRequired, (req, res) => {
  if (!isAdminUser(req.user)) return res.redirect("/devices");
  return sendHtmlFile(res, path.join(__dirname, "..", "public", "admin.html"));
});

app.get("/fw/:id.bin", (req, res) => {
  const id = String(req.params?.id || "").trim();
  const db = loadDb();
  const firmware = (db.firmwares || []).find((f) => String(f.id) === id);
  if (!firmware) return res.status(404).send("Firmware not found");
  const filePath = path.join(FIRMWARE_DIR, String(firmware.storedAs || ""));
  if (!fs.existsSync(filePath)) return res.status(404).send("Firmware file missing");
  res.setHeader("Content-Type", "application/octet-stream");
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("X-Firmware-Version", String(firmware.version || ""));
  res.sendFile(filePath);
});

app.get("/offline", authRequired, (_req, res) => {
  return sendHtmlFile(res, path.join(__dirname, "..", "public", "offline.html"));
});

function routeMainApp(req, res) {
  const loginPage = path.join(__dirname, "..", "public", "login.html");
  const token = req.cookies?.wms_token;
  if (!token) {
    return sendHtmlFile(res, loginPage);
  }
  try {
    const payload = jwt.verify(token, JWT_SECRET);
    const sessionUserId = sessionUserIdFromPayload(payload);
    const activity = readSessionActivity(req, sessionUserId);
    const idleTimeoutMs = idleTimeoutMsForRequest(req);
    if (!activity.ok || Date.now() - activity.ts > idleTimeoutMs) {
      clearSessionCookies(req, res);
      return sendHtmlFile(res, loginPage);
    }
    if (payload?.userId === "__admin__" || String(payload?.role || "").toLowerCase() === "admin") {
      return res.redirect("/admin");
    }
    const db = loadDb();
    const user = db.users.find((u) => u.id === payload.userId);
    if (!user) {
      clearSessionCookies(req, res);
      return sendHtmlFile(res, loginPage);
    }
    if (isAdminUser(user)) {
      return res.redirect("/admin");
    }
    const owned = getOwnedDeviceIds(db, user.id);
    if (!owned.length) return res.redirect("/devices");
    const selected = owned.includes(user.selectedDeviceId) ? user.selectedDeviceId : owned[0];
    const state = getDeviceState(db, selected);
    if (!isDeviceOnline(state)) return res.redirect("/offline");
    if (isSmartPlugDeviceState(state)) {
      return sendHtmlFile(res, path.join(__dirname, "..", "public", "plug.html"));
    }
    return sendMainAppHtml(res, path.join(DATA_ROOT, "index.html"));
  } catch {
    clearSessionCookies(req, res);
    return sendHtmlFile(res, loginPage);
  }
}

const LANDING_LANG_CODES = ["pl", "en", "de", "es"];
const LANDING_LANG_SET = new Set(LANDING_LANG_CODES);
const LANDING_PRIMARY_LANG = "pl";
const LANDING_FALLBACK_LANG = "en";
const LANDING_LANG_COOKIE = "wms_lang";

function normalizeLandingLang(value) {
  const candidate = String(value || "").trim().toLowerCase();
  return LANDING_LANG_SET.has(candidate) ? candidate : LANDING_PRIMARY_LANG;
}

function landingQuerySuffix(req) {
  const idx = String(req.originalUrl || "").indexOf("?");
  return idx >= 0 ? String(req.originalUrl).slice(idx) : "";
}

function detectLandingLangFromAcceptLanguage(headerValue) {
  const raw = String(headerValue || "").trim().toLowerCase();
  if (!raw) return "";
  const scored = [];
  raw.split(",").forEach((chunk, idx) => {
    const [langPart, ...params] = String(chunk || "").trim().split(";");
    const candidate = String(langPart || "").trim();
    if (!candidate || candidate === "*") return;
    const base = candidate.split("-")[0];
    if (!LANDING_LANG_SET.has(base)) return;
    let q = 1;
    const qPart = params.find((part) => String(part || "").trim().startsWith("q="));
    if (qPart) {
      const parsed = Number(String(qPart).split("=")[1]);
      if (Number.isFinite(parsed) && parsed >= 0 && parsed <= 1) q = parsed;
    }
    scored.push({ lang: base, q, idx });
  });
  if (!scored.length) return "";
  scored.sort((a, b) => {
    if (b.q !== a.q) return b.q - a.q;
    return a.idx - b.idx;
  });
  return scored[0].lang;
}

function preferredLandingLang(req) {
  const cookieLang = String(req.cookies?.[LANDING_LANG_COOKIE] || "").trim().toLowerCase();
  if (LANDING_LANG_SET.has(cookieLang)) return cookieLang;
  const detected = detectLandingLangFromAcceptLanguage(req.get("accept-language"));
  return detected || LANDING_FALLBACK_LANG;
}

function landingLanguageUrl(req, langCode) {
  const host = String(req.get("host") || "").trim();
  if (!host) return `/${langCode}`;
  return `${req.protocol}://${host}/${langCode}`;
}

function setLandingSeoHeaders(req, res, langCode) {
  const canonicalUrl = landingLanguageUrl(req, langCode);
  const links = [`<${canonicalUrl}>; rel="canonical"`];
  for (const code of LANDING_LANG_CODES) {
    links.push(`<${landingLanguageUrl(req, code)}>; rel="alternate"; hreflang="${code}"`);
  }
  links.push(`<${landingLanguageUrl(req, LANDING_FALLBACK_LANG)}>; rel="alternate"; hreflang="x-default"`);
  res.setHeader("Link", links.join(", "));
  res.setHeader("Content-Language", langCode);
}

function sendLandingPage(res) {
  return sendHtmlFile(res, LANDING_INDEX_PATH);
}

app.get("/app", (req, res) => routeMainApp(req, res));

app.get("/", (req, res) => {
  const lang = preferredLandingLang(req);
  res.setHeader("Vary", "Accept-Language, Cookie");
  return res.redirect(302, `/${lang}`);
});

app.get(/^\/(pl|en|de|es)\/?$/i, (req, res) => {
  const lang = normalizeLandingLang(req.params?.[0]);
  const canonicalPath = `/${lang}`;
  if (req.path !== canonicalPath) {
    return res.redirect(301, `${canonicalPath}${landingQuerySuffix(req)}`);
  }
  setLandingSeoHeaders(req, res, lang);
  return sendLandingPage(res);
});

app.get(/^\/(pl|en|de|es)\/.+$/i, (req, res) => {
  const lang = normalizeLandingLang(req.params?.[0]);
  return res.redirect(301, `/${lang}${landingQuerySuffix(req)}`);
});

app.use("/assets", express.static(path.join(DATA_ROOT, "assets")));
app.use("/cloud-overlay", express.static(CLOUD_OVERLAY_DIR));
app.use("/landing", express.static(path.join(__dirname, "..", "public", "landing")));
app.get("/favicon.png", (_req, res) => res.sendFile(path.join(DATA_ROOT, "favicon.png")));
app.get("/favicon.ico", (_req, res) => res.redirect(302, "/favicon.png"));
app.get("/icon-192.png", (_req, res) => res.sendFile(path.join(DATA_ROOT, "icon-192.png")));
app.get("/icon-512.png", (_req, res) => res.sendFile(path.join(DATA_ROOT, "icon-512.png")));
app.get("/sw.js", (_req, res) => {
  res.setHeader("Cache-Control", "no-store, no-cache, must-revalidate, proxy-revalidate");
  res.setHeader("Pragma", "no-cache");
  res.setHeader("Expires", "0");
  res.setHeader("Service-Worker-Allowed", "/");
  return res.sendFile(path.join(DATA_ROOT, "sw.js"));
});
app.get("/manifest.json", (_req, res) => res.sendFile(path.join(DATA_ROOT, "manifest.json")));
app.get(/^\/(?!api\/|assets\/|landing\/|login$|devices$|admin$|app$|offline$).*/, routeMainApp);

app.listen(PORT, () => {
  ensureDb();
  logEvent(`[HTTP] cloud panel listening on :${PORT}`);
  logEvent(`[MQTT] broker: ${MQTT_URL}, prefix: ${MQTT_TOPIC_PREFIX}`);
  const gcTimer = setInterval(() => {
    try {
      ensureDb();
    } catch (err) {
      logEvent(`[GC] periodic ensureDb failed: ${String(err?.message || err)}`, "ERROR");
    }
  }, DB_HOUSEKEEPING_INTERVAL_MS);
  if (typeof gcTimer.unref === "function") gcTimer.unref();
  const smartClimateTimer = setInterval(() => {
    runSmartClimateEngine().catch((err) => {
      logEvent(`[SMART-CLIMATE] periodic run failed: ${String(err?.message || err)}`, "ERROR");
    });
  }, SMART_CLIMATE_ENGINE_INTERVAL_MS);
  if (typeof smartClimateTimer.unref === "function") smartClimateTimer.unref();
  setTimeout(() => {
    runSmartClimateEngine({ force: false }).catch((err) => {
      logEvent(`[SMART-CLIMATE] startup run failed: ${String(err?.message || err)}`, "ERROR");
    });
  }, 1800);
  if (MQTT_AUTH_SYNC_HOOK) {
    setTimeout(() => runMqttAuthSyncHook("startup"), 1200);
  }
});
