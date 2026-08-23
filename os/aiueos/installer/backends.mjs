import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { open, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { sha256File } from "./receipt.mjs";

function run(command, args) {
  return execFileSync(command, args, { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
}

function macPlist(commandArgs) {
  const [verb, ...rest] = commandArgs;
  const plist = execFileSync("diskutil", [verb, "-plist", ...rest], { encoding: "buffer", stdio: ["ignore", "pipe", "pipe"] });
  return JSON.parse(execFileSync("plutil", ["-convert", "json", "-o", "-", "-"], { input: plist, encoding: "utf8" }));
}

function macDiskInfo(device) {
  const value = macPlist(["info", device]);
  const list = macPlist(["list", device]);
  const partitions = list?.AllDisksAndPartitions?.[0]?.Partitions ?? [];
  const canonical = value.DeviceNode;
  const systemDisks = new Set();
  const collectPhysicalWholeDisks = (entry, seen = new Set()) => {
    const identifier = entry?.DeviceIdentifier;
    if (!identifier || seen.has(identifier)) return;
    seen.add(identifier);
    if (entry.WholeDisk === true && entry.Internal === true && entry.VirtualOrPhysical !== "Virtual") {
      systemDisks.add(entry.DeviceNode ?? `/dev/${identifier}`);
    }
    const related = [];
    if (entry.ParentWholeDisk) related.push(entry.ParentWholeDisk);
    for (const store of entry.APFSPhysicalStores ?? []) {
      const storeIdentifier = store.DeviceIdentifier ?? store.APFSPhysicalStore;
      if (storeIdentifier) related.push(storeIdentifier);
    }
    for (const next of related) {
      try { collectPhysicalWholeDisks(macPlist(["info", next]), seen); } catch { /* unresolved means the final empty set fails closed */ }
    }
  };
  for (const mount of ["/", "/System/Volumes/Data"]) {
    try {
      const mounted = macPlist(["info", mount]);
      collectPhysicalWholeDisks(mounted);
    } catch { /* one successful system-volume lookup is sufficient */ }
  }
  return {
    info: {
      path: canonical,
      type: value.WholeDisk ? "disk" : "partition",
      whole: value.WholeDisk === true,
      internal: value.Internal === true,
      empty: partitions.length === 0 && !value.Content,
      mounted: Boolean(value.Mounted || partitions.some((p) => p.MountPoint)),
      boot: value.Bootable === true && systemDisks.has(canonical),
      system: systemDisks.has(canonical),
      bytes: value.TotalSize,
      model: value.MediaName ?? "unknown",
    },
    systemDisks: [...systemDisks],
  };
}

function linuxDiskInfo(device) {
  const tree = JSON.parse(run("lsblk", ["--json", "--bytes", "--output", "PATH,TYPE,SIZE,MODEL,RM,MOUNTPOINTS,FSTYPE,PTTYPE", device]));
  const root = tree.blockdevices?.[0];
  if (!root) throw new Error(`lsblk did not return ${device}`);
  const systemSource = run("findmnt", ["--noheadings", "--output", "SOURCE", "/"]).trim();
  const ancestors = JSON.parse(run("lsblk", ["--json", "--inverse", "--paths", "--output", "PATH,TYPE", systemSource])).blockdevices ?? [];
  const flatten = (nodes) => nodes.flatMap((node) => [node, ...flatten(node.children ?? [])]);
  const systemPaths = flatten(ancestors).filter((node) => node.type === "disk").map((node) => node.path);
  const children = root.children ?? [];
  return {
    info: {
      path: root.path,
      type: root.type,
      whole: root.type === "disk",
      internal: root.rm === false || root.rm === 0,
      empty: children.length === 0 && !root.fstype && !root.pttype,
      mounted: (root.mountpoints ?? []).some(Boolean) || children.some((c) => (c.mountpoints ?? []).some(Boolean)),
      boot: systemPaths.includes(root.path),
      system: systemPaths.includes(root.path),
      bytes: root.size,
      model: root.model ?? "unknown",
    },
    systemDisks: systemPaths,
  };
}

async function copyAndVerify(imagePath, device, expected) {
  const source = await open(imagePath, "r");
  let clear;
  try {
    clear = await source.readFile();
  } finally {
    await source.close();
  }
  const digest = createHash("sha256").update(clear).digest("hex");
  if (clear.length !== expected.bytes || digest !== expected.sha256) {
    throw new Error("release image changed after receipt validation; target was not opened");
  }
  const target = await open(device, "r+");
  try {
    let offset = 0;
    while (offset < expected.bytes) {
      const bytesRead = Math.min(4 * 1024 * 1024, expected.bytes - offset);
      let written = 0;
      while (written < bytesRead) {
        const result = await target.write(clear, offset + written, bytesRead - written, offset + written);
        if (result.bytesWritten === 0) throw new Error("target accepted a zero-byte write");
        written += result.bytesWritten;
      }
      offset += bytesRead;
    }
    await target.sync();
  } finally {
    await target.close();
  }
  const readback = await sha256File(device, expected.bytes);
  if (readback.bytes !== expected.bytes || readback.sha256 !== expected.sha256) throw new Error("installed image readback does not match the validated release image");
  return readback;
}

export function realBackend(platform = process.platform) {
  return {
    kind: "real",
    inspect(device) {
      if (platform === "darwin") return macDiskInfo(device);
      if (platform === "linux") return linuxDiskInfo(device);
      throw new Error(`unsupported installer platform: ${platform}`);
    },
    writeImage: copyAndVerify,
  };
}

export async function fakeBackend(configPath) {
  if (process.env.NODE_ENV !== "test" || process.env.AIUEOS_INSTALLER_ALLOW_FAKE !== "1") {
    throw new Error("fake-device backend is only available with NODE_ENV=test and AIUEOS_INSTALLER_ALLOW_FAKE=1");
  }
  const config = JSON.parse(await readFile(configPath, "utf8"));
  return {
    kind: "fake",
    inspect(device) {
      if (device !== config.info?.path) throw new Error(`fake backend has no device ${device}`);
      return { info: config.info, systemDisks: config.systemDisks };
    },
    async writeImage(imagePath, _device, expected) {
      if (!config.outputPath) throw new Error("fake backend requires outputPath");
      const outputPath = resolve(config.outputPath);
      if (outputPath === "/dev" || outputPath.startsWith("/dev/")) throw new Error("fake backend refuses device paths");
      await writeFile(outputPath, Buffer.alloc(expected.bytes), { flag: "wx" });
      return copyAndVerify(imagePath, outputPath, expected);
    },
  };
}
