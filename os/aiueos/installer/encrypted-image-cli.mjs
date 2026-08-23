#!/usr/bin/env node
import { resolve } from "node:path";
import { decryptFile, encryptFile, generateRecoveryKey } from "./encrypted-image.mjs";

function arg(name) {
  const index = process.argv.indexOf(name);
  return index < 0 ? undefined : process.argv[index + 1];
}

async function main() {
  const command = process.argv[2];
  const input = arg("--input");
  const output = arg("--output");
  const key = arg("--recovery-key");
  if (command === "keygen") return process.stdout.write(`${generateRecoveryKey()}\n`);
  if (!input || !output || !key) throw new Error("encrypt/decrypt require --input, --output, and --recovery-key");
  if (command === "encrypt") await encryptFile(resolve(input), resolve(output), key);
  else if (command === "decrypt") await decryptFile(resolve(input), resolve(output), key);
  else throw new Error("usage: encrypted-image-cli.mjs keygen | encrypt/decrypt --input PATH --output PATH --recovery-key KEY");
}

main().catch((error) => { process.stderr.write(`error: ${error.message}\n`); process.exitCode = 1; });
