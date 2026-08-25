#!/usr/bin/env node
'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const SERVER_DIR = path.join(ROOT, 'RTSServer');
const BIN_DIR = path.join(SERVER_DIR, 'bin');
const BIN = path.join(BIN_DIR, process.platform === 'win32' ? 'rtsserver.exe' : 'rtsserver');
const LOCAL_PORT = 8080;
const LOCAL_UDP_PORT = 8081;

function fail(message, code = 1) {
  console.error(`\nERROR: ${message}`);
  process.exit(code);
}

function findGo() {
  const fromPath = spawnSync('go', ['version'], { encoding: 'utf8' });
  if (!fromPath.error && fromPath.status === 0) {
    return { bin: 'go', version: (fromPath.stdout || '').trim() };
  }

  const candidates = [
    '/opt/homebrew/bin/go',
    '/usr/local/go/bin/go',
    '/usr/local/bin/go',
  ];
  for (const bin of candidates) {
    if (!fs.existsSync(bin)) {
      continue;
    }
    const result = spawnSync(bin, ['version'], { encoding: 'utf8' });
    if (!result.error && result.status === 0) {
      return { bin, version: (result.stdout || '').trim() };
    }
  }
  return null;
}

function parsePids(text) {
  const pids = new Set();
  for (const line of (text || '').split(/\s+/)) {
    const pid = Number(line);
    if (Number.isInteger(pid) && pid > 0 && pid !== process.pid) {
      pids.add(pid);
    }
  }
  return pids;
}

function commandForPid(pid) {
  const result = spawnSync('ps', ['-p', String(pid), '-o', 'command='], { encoding: 'utf8' });
  return (result.stdout || '').trim();
}

function pidsOnPort(port) {
  const result = spawnSync('lsof', ['-nP', `-iTCP:${port}`, '-sTCP:LISTEN', '-t'], { encoding: 'utf8' });
  return parsePids(result.stdout);
}

function pidsMatchingServer() {
  const result = spawnSync('ps', ['-ax', '-o', 'pid=,command='], { encoding: 'utf8' });
  const pids = new Set();
  const binNeedle = BIN.toLowerCase();
  const dirNeedle = SERVER_DIR.toLowerCase();
  for (const line of (result.stdout || '').split('\n')) {
    const trimmed = line.trim();
    const match = trimmed.match(/^(\d+)\s+(.*)$/);
    if (!match) {
      continue;
    }
    const pid = Number(match[1]);
    const cmd = match[2].toLowerCase();
    if (pid === process.pid) {
      continue;
    }
    const isBinary = cmd.includes(binNeedle) || cmd.includes('/rtsserver');
    const isGoRun = cmd.includes('go') && cmd.includes('run') && cmd.includes(dirNeedle);
    if (isBinary || isGoRun) {
      pids.add(pid);
    }
  }
  return pids;
}

function signalPid(pid, signal) {
  try {
    process.kill(pid, signal);
    return true;
  } catch (err) {
    if (err && err.code === 'ESRCH') {
      return false;
    }
    console.warn(`    warn: ${signal} pid ${pid}: ${err.message}`);
    return false;
  }
}

function sleepMs(ms) {
  spawnSync('sleep', [String(ms / 1000)]);
}

function shutdownLocalHost() {
  console.log(`==> Shutting down local RTSServer (port ${LOCAL_PORT})`);
  const pids = new Set([...pidsOnPort(LOCAL_PORT), ...pidsMatchingServer()]);
  if (pids.size === 0) {
    console.log('    Nothing listening. Port is free.');
    return;
  }

  for (const pid of pids) {
    const cmd = commandForPid(pid);
    console.log(`    SIGTERM pid ${pid}${cmd ? ` (${cmd})` : ''}`);
    signalPid(pid, 'SIGTERM');
  }

  const deadline = Date.now() + 3000;
  let remaining = [...pids];
  while (Date.now() < deadline) {
    remaining = remaining.filter((pid) => {
      try {
        process.kill(pid, 0);
        return true;
      } catch {
        return false;
      }
    });
    if (remaining.length === 0) {
      break;
    }
    sleepMs(150);
  }

  for (const pid of remaining) {
    console.log(`    SIGKILL pid ${pid}`);
    signalPid(pid, 'SIGKILL');
  }
  sleepMs(200);

  const stillOnPort = pidsOnPort(LOCAL_PORT);
  if (stillOnPort.size > 0) {
    fail(`Port ${LOCAL_PORT} still in use (pids: ${[...stillOnPort].join(', ')}).`);
  }
  console.log(`    Port ${LOCAL_PORT} is free.`);
}

function runGo(goBin, args, opts = {}) {
  const result = spawnSync(goBin, args, {
    cwd: SERVER_DIR,
    encoding: 'utf8',
    stdio: 'inherit',
    env: process.env,
    ...opts,
  });
  if (result.error) {
    fail(result.error.message);
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

function isPrivateIPv4(address) {
  const parts = address.split('.').map(Number);
  if (parts.length !== 4 || parts.some((n) => !Number.isInteger(n))) {
    return false;
  }
  const [a, b] = parts;
  return a === 10 || (a === 192 && b === 168) || (a === 172 && b >= 16 && b <= 31);
}

function isIPv4(addr) {
  return addr.family === 'IPv4' || addr.family === 4;
}

function lanIPv4s() {
  const found = [];
  for (const [name, addrs] of Object.entries(os.networkInterfaces())) {
    for (const addr of addrs || []) {
      if (!addr || addr.internal || !isIPv4(addr)) {
        continue;
      }
      if (addr.address.startsWith('169.254.')) {
        continue;
      }
      found.push({ name, address: addr.address, private: isPrivateIPv4(addr.address) });
    }
  }
  found.sort((a, b) => {
    if (a.private !== b.private) {
      return a.private ? -1 : 1;
    }
    // Wi-Fi is often en0 on Mac; prefer cable / other NICs when both exist.
    const aWifi = a.name === 'en0' ? 1 : 0;
    const bWifi = b.name === 'en0' ? 1 : 0;
    return aWifi - bWifi;
  });
  return found;
}

function printLanReachable(lanAddrs) {
  if (lanAddrs.length === 0) {
    fail(
      'No LAN IPv4 address found.\n' +
        '    Connect ethernet or Wi-Fi, then re-run.'
    );
  }

  const primary = lanAddrs[0];
  console.log(`==> RTSServer LAN host (bind 0.0.0.0:${LOCAL_PORT} / udp ${LOCAL_UDP_PORT})`);
  console.log(`    this machine : http://127.0.0.1:${LOCAL_PORT}`);
  console.log(`    LAN          : http://${primary.address}:${LOCAL_PORT}  (${primary.name})`);
  if (lanAddrs.length > 1) {
    for (const extra of lanAddrs.slice(1)) {
      console.log(`    also         : http://${extra.address}:${LOCAL_PORT}  (${extra.name})`);
    }
  }
  console.log('');
  console.log('    Other computers on this network — Networking.json:');
  console.log(`        "ip": "${primary.address}",`);
  console.log(`        "port": ${LOCAL_PORT},`);
  console.log(`        "udp_port": ${LOCAL_UDP_PORT}`);
  console.log('');
}

const mode = process.argv[2];
if (mode !== 'build' && mode !== 'local' && mode !== 'nat' && mode !== 'shutdown') {
  fail('Usage: node tools/rtsserver.js build|local|nat|shutdown');
}

if (!fs.existsSync(SERVER_DIR)) {
  fail(`RTSServer not found: ${SERVER_DIR}`);
}

if (mode === 'shutdown') {
  shutdownLocalHost();
  process.exit(0);
}

const go = findGo();
if (!go) {
  fail(
    'Go is not installed or not on PATH.\n' +
      '    macOS: brew install go\n' +
      '    Then re-run this script.'
  );
}

console.log(`==> ${go.version}`);
console.log(`    dir : ${SERVER_DIR}`);

if (mode === 'build') {
  fs.mkdirSync(BIN_DIR, { recursive: true });
  console.log(`==> Building ${path.relative(ROOT, BIN)}`);
  runGo(go.bin, ['build', '-o', BIN, '.']);
  console.log('    OK');
  process.exit(0);
}

if (mode === 'nat') {
  const lanAddrs = lanIPv4s();
  printLanReachable(lanAddrs);
  runGo(go.bin, ['run', '.', '-bind', '0.0.0.0', '-port', String(LOCAL_PORT), '-udp-port', String(LOCAL_UDP_PORT)]);
  process.exit(0);
}

if (mode === 'local') {
  console.log(`==> RTSServer local host (127.0.0.1:${LOCAL_PORT} / udp ${LOCAL_UDP_PORT})`);
  runGo(go.bin, ['run', '.', '-bind', '127.0.0.1', '-port', String(LOCAL_PORT), '-udp-port', String(LOCAL_UDP_PORT)]);
  process.exit(0);
}

fail('Usage: node tools/rtsserver.js build|local|nat|shutdown');
