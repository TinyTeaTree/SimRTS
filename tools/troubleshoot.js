#!/usr/bin/env node
'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const NETWORKING = path.join(ROOT, 'SimRTS', 'Content', 'Data', 'Networking.json');
const UE_ROOT = process.env.UE_ROOT || '/Users/Shared/Epic Games/UE_5.6';
const UNREAL_EDITOR_APP = path.join(UE_ROOT, 'Engine', 'Binaries', 'Mac', 'UnrealEditor.app');
const LAUNCHER_CANDIDATES = [
  '/Users/Shared/Epic Games/EpicGamesLauncher/Epic Games Launcher.app',
  '/Applications/Epic Games Launcher.app',
];
const SETTINGS_URLS = [
  'x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_LocalNetwork',
  'x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork',
];

function fail(message, code = 1) {
  console.error(`\nERROR: ${message}`);
  process.exit(code);
}

function isLoopback(ip) {
  return ip === '127.0.0.1' || ip === 'localhost' || ip === '::1';
}

function isPrivateIPv4(address) {
  const parts = address.split('.').map(Number);
  if (parts.length !== 4 || parts.some((n) => !Number.isInteger(n))) {
    return false;
  }
  const [a, b] = parts;
  return a === 10 || (a === 192 && b === 168) || (a === 172 && b >= 16 && b <= 31);
}

function macosVersion() {
  const result = spawnSync('sw_vers', ['-productVersion'], { encoding: 'utf8' });
  return (result.stdout || '').trim() || os.release();
}

function macosMajor(version) {
  const major = Number(String(version).split('.')[0]);
  return Number.isInteger(major) ? major : 0;
}

function firstExisting(paths) {
  for (const candidate of paths) {
    if (candidate && fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return null;
}

function editorRunning() {
  const result = spawnSync('ps', ['-ax', '-o', 'command='], { encoding: 'utf8' });
  const text = (result.stdout || '').toLowerCase();
  return text.includes('unrealeditor') && !text.includes('shadercompileworker');
}

function probeLogin(ip, port) {
  const url = `http://${ip}:${port}/Login`;
  const result = spawnSync(
    'curl',
    [
      '-sS',
      '-m',
      '5',
      '-w',
      '\nhttp_code=%{http_code}',
      '-X',
      'POST',
      url,
      '-H',
      'Content-Type: application/json',
      '-d',
      '{"username":"troubleshoot"}',
    ],
    { encoding: 'utf8' }
  );
  const stdout = result.stdout || '';
  const stderr = (result.stderr || '').trim();
  const codeMatch = stdout.match(/http_code=(\d+)\s*$/);
  const httpCode = codeMatch ? Number(codeMatch[1]) : 0;
  const body = stdout.replace(/\nhttp_code=\d+\s*$/, '').trim();
  return {
    ok: result.status === 0 && httpCode === 200,
    httpCode,
    body,
    stderr,
    status: result.status,
    error: result.error,
  };
}

function openLocalNetworkSettings() {
  for (const url of SETTINGS_URLS) {
    const result = spawnSync('open', [url], { encoding: 'utf8' });
    if (!result.error && result.status === 0) {
      return url;
    }
  }
  return null;
}

const noOpen = process.argv.includes('--no-open');
const darwin = process.platform === 'darwin';

if (!fs.existsSync(NETWORKING)) {
  fail(`Networking.json not found: ${NETWORKING}`);
}

let config;
try {
  config = JSON.parse(fs.readFileSync(NETWORKING, 'utf8'));
} catch (err) {
  fail(`Networking.json is not valid JSON: ${err.message}`);
}

const ip = typeof config.ip === 'string' ? config.ip.trim() : '';
const port = Number(config.port);
const udpPort = Number(config.udp_port);
if (!ip || !Number.isInteger(port) || port < 1 || port > 65535) {
  fail('Networking.json needs ip and port (1-65535).');
}

const loopback = isLoopback(ip);
const lan = isPrivateIPv4(ip);

console.log('==> Networking.json');
console.log(`    file : ${NETWORKING}`);
console.log(`    http : http://${ip}:${port}`);
console.log(`    udp  : ${ip}:${Number.isInteger(udpPort) ? udpPort : '?'}`);
console.log(`    kind : ${loopback ? 'loopback (this computer only)' : lan ? 'LAN' : 'other'}`);
console.log('');

console.log(`==> Probe from this process (npm / Terminal)`);
const probe = probeLogin(ip, port);
if (probe.error) {
  console.log(`    curl failed: ${probe.error.message}`);
} else if (probe.ok) {
  console.log(`    Login HTTP 200 in 5s budget`);
} else {
  console.log(`    Login failed (curl status=${probe.status ?? '?'}, http=${probe.httpCode || 'none'})`);
  if (probe.stderr) {
    console.log(`    ${probe.stderr}`);
  } else if (probe.body) {
    console.log(`    ${probe.body}`);
  }
}
console.log('');

if (!darwin) {
  if (!probe.ok) {
    fail(`Host ${ip}:${port} is not reachable from this machine.`);
  }
  console.log('==> Local Network permission');
  console.log('    This check is macOS-only (Sequoia+).');
  process.exit(0);
}

const version = macosVersion();
const needsLocalNetwork = macosMajor(version) >= 15 && !loopback;
const editorApp = firstExisting([UNREAL_EDITOR_APP]);
const launcherApp = firstExisting(LAUNCHER_CANDIDATES);

console.log('==> macOS Local Network');
console.log(`    macOS              : ${version}`);
console.log(`    Terminal / npm     : usually allowed (system app). This probe is not Unreal.`);
console.log(
  `    Unreal Editor      : ${editorApp || `not found (UE_ROOT=${UE_ROOT})`}`
);
console.log(`    Epic Games Launcher: ${launcherApp || 'not found'}`);
console.log(`    Editor running     : ${editorRunning() ? 'yes' : 'no'}`);
console.log('');
console.log('    Apple does not let a script read or grant Local Network for another app.');
console.log('    npm run open launches UnrealEditor.app directly, so it needs its own toggle.');
console.log('    Opening via the Epic launcher uses the launcher’s permission instead.');
console.log('');

if (loopback) {
  console.log('    Target is loopback. Local Network does not apply.');
  if (!probe.ok) {
    fail(`Host ${ip}:${port} is not reachable. Is npm run server_local_host running?`);
  }
  process.exit(0);
}

if (!needsLocalNetwork) {
  console.log('    macOS < 15 has no Local Network privacy pane.');
  if (!probe.ok) {
    fail(`Host ${ip}:${port} is not reachable (firewall, Wi-Fi, or the NAT host is down).`);
  }
  process.exit(0);
}

if (probe.ok) {
  console.log('    Terminal can reach the host. If PIE Login says "No route to host",');
  console.log('    enable Local Network for Unreal Editor (and Epic Games Launcher if you use it).');
} else {
  console.log('    Terminal also failed. That is a real route/firewall/host problem, not editor TCC.');
}

console.log('');
console.log('    System Settings → Privacy & Security → Local Network');
console.log('        [on]  UnrealEditor');
console.log('        [on]  Epic Games Launcher   (only if you open from the launcher)');
console.log('    Then restart the editor.');
console.log('');

if (!noOpen) {
  const opened = openLocalNetworkSettings();
  if (opened) {
    console.log('    Opened the Local Network settings pane.');
  } else {
    console.log('    Could not open System Settings. Open Privacy & Security → Local Network manually.');
  }
} else {
  console.log('    Skipped opening System Settings (--no-open).');
}

if (!probe.ok) {
  fail(`Host ${ip}:${port} is not reachable from this machine.`);
}
