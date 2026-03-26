#!/usr/bin/env node
import { createHash, createPrivateKey, createPublicKey, generateKeyPairSync, sign as edSign, verify as edVerify } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

function usage() {
  console.log(`Usage:
  node scripts/ota-sign-ed25519.mjs keygen [--out-dir ./secrets]
  node scripts/ota-sign-ed25519.mjs sign --file <firmware.bin|littlefs.bin> --private <private.pem> [--target firmware|fs] [--out ./ota-signature.json]
  node scripts/ota-sign-ed25519.mjs pubhex --public <public.pem>
`);
}

function arg(name, fallback = "") {
  const i = process.argv.indexOf(name);
  if (i < 0 || i + 1 >= process.argv.length) return fallback;
  return process.argv[i + 1];
}

function extractRawPublicKeyHex(publicKey) {
  const der = publicKey.export({ type: "spki", format: "der" });
  const marker = Buffer.from([0x03, 0x21, 0x00]); // BIT STRING (33), 0 unused bits
  const i = der.indexOf(marker);
  if (i >= 0 && i + marker.length + 32 <= der.length) {
    return der.subarray(i + marker.length, i + marker.length + 32).toString("hex");
  }
  // Fallback for unexpected DER layout.
  return der.subarray(der.length - 32).toString("hex");
}

function keygen() {
  const outDir = resolve(arg("--out-dir", "./secrets"));
  if (!existsSync(outDir)) mkdirSync(outDir, { recursive: true });

  const { privateKey, publicKey } = generateKeyPairSync("ed25519");
  const privatePem = privateKey.export({ type: "pkcs8", format: "pem" });
  const publicPem = publicKey.export({ type: "spki", format: "pem" });
  const publicHex = extractRawPublicKeyHex(publicKey);

  const privatePath = resolve(outDir, "ota_ed25519_private.pem");
  const publicPath = resolve(outDir, "ota_ed25519_public.pem");
  const publicHexPath = resolve(outDir, "ota-ed25519-pubkey.hex");

  writeFileSync(privatePath, privatePem, { mode: 0o600 });
  writeFileSync(publicPath, publicPem);
  writeFileSync(publicHexPath, publicHex + "\n");

  console.log("Key pair generated:");
  console.log(`- private: ${privatePath}`);
  console.log(`- public:  ${publicPath}`);
  console.log(`- pubhex:  ${publicHexPath}`);
  console.log("");
  console.log("Embed public key:");
  console.log(`- Build flag: -DOTA_ED25519_PUBLIC_KEY_HEX=\\\"${publicHex}\\\"`);
  console.log(`- OR file in LittleFS: /ota-ed25519-pubkey.hex with content ${publicHex}`);
}

function signFile() {
  const filePath = arg("--file");
  const privatePath = arg("--private");
  const target = (arg("--target", "firmware") || "firmware").toLowerCase();
  const outPath = arg("--out");

  if (!filePath || !privatePath) {
    console.error("Missing required --file or --private");
    usage();
    process.exit(2);
  }

  const payload = readFileSync(resolve(filePath));
  const digest = createHash("sha256").update(payload).digest();
  const sha256 = digest.toString("hex");

  const privateKey = createPrivateKey(readFileSync(resolve(privatePath), "utf8"));
  const signature = edSign(null, digest, privateKey); // Ed25519 signs raw 32-byte digest buffer here.
  const signatureHex = signature.toString("hex");

  // Sanity-check signature before printing.
  const publicKey = createPublicKey(privateKey);
  if (!edVerify(null, digest, publicKey, signature)) {
    console.error("Signature self-check failed.");
    process.exit(1);
  }

  const result = {
    target: target === "fs" ? "fs" : "firmware",
    size: payload.length,
    sha256,
    signature: signatureHex,
    signature_alg: "ed25519"
  };

  if (outPath) {
    const absOut = resolve(outPath);
    writeFileSync(absOut, JSON.stringify(result, null, 2) + "\n");
    console.log(`Saved: ${absOut}`);
  }

  console.log(JSON.stringify(result, null, 2));
}

function pubhex() {
  const publicPath = arg("--public");
  if (!publicPath) {
    console.error("Missing required --public");
    usage();
    process.exit(2);
  }
  const pub = createPublicKey(readFileSync(resolve(publicPath), "utf8"));
  console.log(extractRawPublicKeyHex(pub));
}

const cmd = process.argv[2];
if (!cmd || cmd === "-h" || cmd === "--help") {
  usage();
  process.exit(0);
}
if (cmd === "keygen") {
  keygen();
  process.exit(0);
}
if (cmd === "sign") {
  signFile();
  process.exit(0);
}
if (cmd === "pubhex") {
  pubhex();
  process.exit(0);
}

console.error(`Unknown command: ${cmd}`);
usage();
process.exit(2);
