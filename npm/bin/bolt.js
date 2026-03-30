#!/usr/bin/env node
/**
 * Bolt CLI wrapper — finds and runs the native binary.
 */

const { spawn } = require("child_process");
const path = require("path");
const os = require("os");
const fs = require("fs");

function findBinary() {
  const ext = os.platform() === "win32" ? ".exe" : "";
  const name = "bolt" + ext;

  // 1. Check in package's bin directory (downloaded by postinstall)
  const packageBin = path.join(__dirname, name);
  if (fs.existsSync(packageBin)) return packageBin;

  // 2. Check parent directory (some npm versions put it there)
  const parentBin = path.join(__dirname, "..", "bin", name);
  if (fs.existsSync(parentBin)) return parentBin;

  // 3. Check package root
  const rootBin = path.join(__dirname, "..", name);
  if (fs.existsSync(rootBin)) return rootBin;

  // 4. Check for locally built binary (development mode)
  for (const dir of ["build", "build_perf"]) {
    const local = path.join(__dirname, "..", "..", dir, name);
    if (fs.existsSync(local)) return local;
  }

  // 5. Check common install locations
  if (os.platform() === "win32") {
    const appLocal = path.join(process.env.LOCALAPPDATA || "", "Bolt", name);
    if (fs.existsSync(appLocal)) return appLocal;
  } else {
    for (const p of ["/usr/local/bin/bolt", "/usr/bin/bolt"]) {
      if (fs.existsSync(p)) return p;
    }
  }

  // Not found — show helpful error
  console.error("");
  console.error("  \x1b[31mError: bolt binary not found.\x1b[0m");
  console.error("");
  console.error("  The native binary was not downloaded during npm install.");
  console.error("  This usually happens when:");
  console.error("    1. npm scripts are disabled (npm config set ignore-scripts false)");
  console.error("    2. Network blocked GitHub (try with proxy/VPN)");
  console.error("    3. Platform not supported");
  console.error("");
  console.error("  \x1b[1mFix:\x1b[0m Run the postinstall script manually:");
  console.error(`    node "${path.join(__dirname, '..', 'install.js')}"`);
  console.error("");
  console.error("  \x1b[1mOr install directly:\x1b[0m");

  if (os.platform() === "win32") {
    console.error("    iwr -useb https://raw.githubusercontent.com/General-zzz-trade/Bolt/master/install.ps1 | iex");
  } else {
    console.error("    curl -fsSL https://raw.githubusercontent.com/General-zzz-trade/Bolt/master/install.sh | bash");
  }

  console.error("");
  console.error("  \x1b[1mOr download manually:\x1b[0m");
  console.error("    https://github.com/General-zzz-trade/Bolt/releases");
  console.error("");
  process.exit(1);
}

const binary = findBinary();
const args = process.argv.slice(2);

// Default to interactive agent mode
if (args.length === 0) {
  args.push("agent");
}

const child = spawn(binary, args, {
  stdio: "inherit",
  env: process.env,
});

child.on("error", (err) => {
  if (err.code === "ENOENT") {
    console.error("Error: bolt binary not executable. Try:");
    console.error(`  chmod +x "${binary}"`);
  } else {
    console.error("Failed to start bolt:", err.message);
  }
  process.exit(1);
});

child.on("exit", (code) => {
  process.exit(code || 0);
});
