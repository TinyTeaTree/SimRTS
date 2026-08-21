#!/usr/bin/env node
'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const UPROJECT = path.join(ROOT, 'SimRTS', 'SimRTS.uproject');
const SOURCE_DIRS = [
  path.join(ROOT, 'SimRTS', 'Source', 'SimRTS'),
  path.join(ROOT, 'SimRTS', 'Source', 'RTSEngine'),
  path.join(ROOT, 'SimRTS', 'Source', 'RTSComms'),
];
const DYLIB = path.join(ROOT, 'SimRTS', 'Binaries', 'Mac', 'UnrealEditor-SimRTS.dylib');
const ENGINE_DYLIB = path.join(ROOT, 'SimRTS', 'Binaries', 'Mac', 'UnrealEditor-RTSEngine.dylib');
const COMMS_DYLIB = path.join(ROOT, 'SimRTS', 'Binaries', 'Mac', 'UnrealEditor-RTSComms.dylib');
const MODULES = path.join(ROOT, 'SimRTS', 'Binaries', 'Mac', 'UnrealEditor.modules');

const UE_ROOT =
  process.env.UE_ROOT ||
  '/Users/Shared/Epic Games/UE_5.6';
const BUILD_SH = path.join(UE_ROOT, 'Engine', 'Build', 'BatchFiles', 'Mac', 'Build.sh');
const EDITOR_QUIT_TIMEOUT_MS = 60_000;

function usage() {
  console.log(`Usage:
  npm run compile
  npm run compile_open
  npm run compile:hot

Compiles SimRTSEditor (Mac Development) for SimRTS.uproject.
Closes any Unreal Editor instance running SimRTS before building (use --no-close to skip).
Use compile_open (or pass -open) to launch the project after a successful build + sanity check.
Use compile:hot (or pass --hot) to rebuild while the editor stays open (UBT hot-reload).

Flags:
  -open, --open       Open the uproject after a successful compile
  --no-close          Do not quit Unreal Editor before compiling
  -hot, --hot         Hot recompile: editor must be open; do not quit/relaunch

Env:
  UE_ROOT   Unreal Engine root (default: ${UE_ROOT})
`);
}

function newestModuleBinaryMtimeMs() {
  const dir = path.dirname(DYLIB);
  if (!fs.existsSync(dir)) {
    return { mtimeMs: 0, path: null };
  }

  let newest = 0;
  let newestPath = null;
  for (const name of fs.readdirSync(dir)) {
    // Hot reload writes suffixed modules: UnrealEditor-SimRTS-0001.dylib
    // RTSEngine / RTSComms changes only rebuild those dylibs — count all three.
    if (!/^UnrealEditor-(SimRTS|RTSEngine|RTSComms)(-\d+)?\.dylib$/i.test(name)) {
      continue;
    }
    const full = path.join(dir, name);
    const mtimeMs = fs.statSync(full).mtimeMs;
    if (mtimeMs > newest) {
      newest = mtimeMs;
      newestPath = full;
    }
  }
  return { mtimeMs: newest, path: newestPath };
}

function fail(message, code = 1) {
  console.error(`\nERROR: ${message}`);
  process.exit(code);
}

function formatTime(date) {
  return date.toISOString().replace('T', ' ').replace(/\.\d+Z$/, ' UTC');
}

function walkFiles(dir, out = []) {
  if (!fs.existsSync(dir)) {
    return out;
  }
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      walkFiles(full, out);
    } else if (/\.(cpp|h|c|cc|cxx|hpp|Build\.cs)$/i.test(entry.name)) {
      out.push(full);
    }
  }
  return out;
}

function newestSourceMtimeMs() {
  let newest = 0;
  let newestPath = null;
  for (const sourceDir of SOURCE_DIRS) {
    for (const file of walkFiles(sourceDir)) {
      const mtimeMs = fs.statSync(file).mtimeMs;
      if (mtimeMs > newest) {
        newest = mtimeMs;
        newestPath = file;
      }
    }
  }
  return { newest, newestPath };
}

function sleepMs(ms) {
  spawnSync('sleep', [String(ms / 1000)]);
}

/** UnrealEditor processes that appear to be running this SimRTS project. */
function findSimRtsEditorProcs() {
  const result = spawnSync('ps', ['-ax', '-o', 'pid=,command='], { encoding: 'utf8' });
  if (result.error || result.status !== 0) {
    return [];
  }

  const uprojectNeedle = path.resolve(UPROJECT).toLowerCase();
  const projectDirNeedle = path.resolve(path.dirname(UPROJECT)).toLowerCase();
  const procs = [];

  for (const line of (result.stdout || '').split('\n')) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }
    const match = trimmed.match(/^(\d+)\s+(.*)$/);
    if (!match) {
      continue;
    }
    const pid = Number(match[1]);
    const cmd = match[2];
    const cmdLower = cmd.toLowerCase();
    if (!cmdLower.includes('unrealeditor')) {
      continue;
    }
    // Skip ShaderCompileWorker / derived data helpers.
    if (cmdLower.includes('shadercompileworker') || cmdLower.includes('unrealeditor-cmd')) {
      continue;
    }
    if (cmdLower.includes(uprojectNeedle) || cmdLower.includes('simrts.uproject') || cmdLower.includes(projectDirNeedle)) {
      procs.push({ pid, cmd });
    }
  }
  return procs;
}

function closeSimRtsEditor() {
  let procs = findSimRtsEditorProcs();
  if (procs.length === 0) {
    console.log('    No SimRTS Unreal Editor process found.');
    return;
  }

  console.log(`    Quitting ${procs.length} UnrealEditor process(es) for SimRTS...`);
  for (const { pid, cmd } of procs) {
    console.log(`    SIGTERM pid ${pid}`);
    try {
      process.kill(pid, 'SIGTERM');
    } catch (err) {
      // Already exited.
      if (err && err.code !== 'ESRCH') {
        console.warn(`    warn: could not signal pid ${pid}: ${err.message}`);
      }
    }
    // Keep cmd out of spam; useful if debugging stuck quit.
    if (process.env.COMPILE_VERBOSE) {
      console.log(`      ${cmd}`);
    }
  }

  const deadline = Date.now() + EDITOR_QUIT_TIMEOUT_MS;
  while (Date.now() < deadline) {
    procs = findSimRtsEditorProcs();
    if (procs.length === 0) {
      // Give the OS a moment to release dylib locks.
      sleepMs(1000);
      console.log('    Editor closed.');
      return;
    }
    sleepMs(500);
  }

  fail(
    `Timed out waiting for Unreal Editor to quit (pids: ${procs.map((p) => p.pid).join(', ')}).\n` +
      `    Save/close it manually, then run npm run compile again.`
  );
}

const args = process.argv.slice(2);
if (args.includes('-h') || args.includes('--help')) {
  usage();
  process.exit(0);
}

const hot = args.includes('-hot') || args.includes('--hot');
const openAfter = !hot && (args.includes('-open') || args.includes('--open'));
const closeEditor = !hot && !args.includes('--no-close');

if (!fs.existsSync(UPROJECT)) {
  fail(`uproject not found: ${UPROJECT}`);
}
if (!fs.existsSync(BUILD_SH)) {
  fail(`Build.sh not found: ${BUILD_SH}\nSet UE_ROOT to your Unreal 5.6 install.`);
}

if (hot) {
  const procs = findSimRtsEditorProcs();
  if (procs.length === 0) {
    fail(
      'Hot compile requires SimRTS Unreal Editor to be running.\n' +
        '    Open the project first, or use npm run compile / compile_open for a full rebuild.'
    );
  }
  console.log(`==> Hot compile (editor open, pids: ${procs.map((p) => p.pid).join(', ')})`);
  console.log('');
} else if (closeEditor) {
  console.log('==> Closing Unreal Editor (if running SimRTS)');
  closeSimRtsEditor();
  console.log('');
}

const buildStartedAt = Date.now();
console.log(`==> Compiling SimRTSEditor (Mac Development)${hot ? ' [hot]' : ''}`);
console.log(`    Project : ${UPROJECT}`);
console.log(`    UE_ROOT : ${UE_ROOT}`);
console.log(`    Hot     : ${hot ? 'yes' : 'no'}`);
console.log(`    Close   : ${closeEditor ? 'yes' : 'no'}`);
console.log(`    Open    : ${openAfter ? 'yes' : 'no'}`);
console.log('');

const build = spawnSync(
  BUILD_SH,
  ['SimRTSEditor', 'Mac', 'Development', `-Project=${UPROJECT}`],
  {
    cwd: ROOT,
    encoding: 'utf8',
    env: process.env,
  }
);

if (build.stdout) {
  process.stdout.write(build.stdout);
}
if (build.stderr) {
  process.stderr.write(build.stderr);
}

if (build.error) {
  fail(build.error.message);
}
if (build.status !== 0) {
  fail(`Build failed with exit code ${build.status}`, build.status ?? 1);
}

const buildOutput = `${build.stdout || ''}\n${build.stderr || ''}`;
const targetUpToDate = /Target is up to date/i.test(buildOutput);

console.log('\n==> Sanity check');

if (!fs.existsSync(DYLIB)) {
  fail(`Missing module binary: ${DYLIB}`);
}
if (!fs.existsSync(ENGINE_DYLIB)) {
  fail(`Missing module binary: ${ENGINE_DYLIB}`);
}
if (!fs.existsSync(COMMS_DYLIB)) {
  fail(`Missing module binary: ${COMMS_DYLIB}`);
}
if (!fs.existsSync(MODULES)) {
  fail(`Missing modules manifest: ${MODULES}`);
}

const dylibStat = fs.statSync(DYLIB);
const engineStat = fs.statSync(ENGINE_DYLIB);
const commsStat = fs.statSync(COMMS_DYLIB);
const modulesStat = fs.statSync(MODULES);
const hotBinary = newestModuleBinaryMtimeMs();
const effectiveBinaryMtime = Math.max(dylibStat.mtimeMs, engineStat.mtimeMs, commsStat.mtimeMs, hotBinary.mtimeMs);
const rebuiltDuringThisRun = effectiveBinaryMtime >= buildStartedAt - 5_000;
const modulesTouched = modulesStat.mtimeMs >= buildStartedAt - 5_000;
const { newest, newestPath } = newestSourceMtimeMs();
// Allow small filesystem timestamp skew.
const binaryCoversSources = newest === 0 || effectiveBinaryMtime + 2_000 >= newest;

console.log(`    dylib   : ${DYLIB}`);
console.log(`    mtime   : ${formatTime(dylibStat.mtime)}`);
console.log(`    size    : ${dylibStat.size} bytes`);
if (hotBinary.path && hotBinary.path !== DYLIB && hotBinary.path !== ENGINE_DYLIB && hotBinary.path !== COMMS_DYLIB) {
  console.log(`    hot dylib: ${hotBinary.path} (${formatTime(new Date(hotBinary.mtimeMs))})`);
}
console.log(`    engine  : ${ENGINE_DYLIB} (${formatTime(engineStat.mtime)}, ${engineStat.size} bytes)`);
console.log(`    comms   : ${COMMS_DYLIB} (${formatTime(commsStat.mtime)}, ${commsStat.size} bytes)`);
console.log(`    modules : ${MODULES} (${formatTime(modulesStat.mtime)})`);
if (newestPath) {
  console.log(`    newest source : ${path.relative(ROOT, newestPath)} (${formatTime(new Date(newest))})`);
}
console.log(`    up to date    : ${targetUpToDate ? 'yes' : 'no'}`);

if (dylibStat.size < 50_000) {
  fail(`UnrealEditor-SimRTS.dylib looks suspiciously small (${dylibStat.size} bytes).`);
}
if (engineStat.size < 10_000) {
  fail(`UnrealEditor-RTSEngine.dylib looks suspiciously small (${engineStat.size} bytes).`);
}
if (commsStat.size < 10_000) {
  fail(`UnrealEditor-RTSComms.dylib looks suspiciously small (${commsStat.size} bytes).`);
}

if (rebuiltDuringThisRun) {
  console.log(hot ? '    OK: hot module binary was produced by this compile.' : '    OK: module binary was rebuilt by this compile.');
} else if (hot && modulesTouched) {
  console.log('    OK: UnrealEditor.modules was updated (hot-reload patch).');
} else if (targetUpToDate && binaryCoversSources) {
  console.log('    OK: target already up to date; binary covers current sources.');
} else if (binaryCoversSources) {
  console.log('    OK: binary is at least as new as current sources.');
} else if (hot) {
  fail(
    `Hot compile finished, but no updated SimRTS/RTSEngine/RTSComms module binary was detected.\n` +
      `    Tip: use the editor Compile button, or npm run compile (full rebuild with editor closed).`
  );
} else {
  fail(
    `Module binaries are older than sources.\n` +
      `    SimRTS dylib mtime    = ${formatTime(dylibStat.mtime)}\n` +
      `    RTSEngine dylib mtime = ${formatTime(engineStat.mtime)}\n` +
      `    RTSComms dylib mtime  = ${formatTime(commsStat.mtime)}\n` +
      `    newest source = ${newestPath ? path.relative(ROOT, newestPath) : '?'} (${formatTime(new Date(newest))})\n` +
      `    Tip: quit Unreal Editor (or omit --no-close) and run npm run compile again.`
  );
}

if (openAfter) {
  console.log('\n==> Opening SimRTS.uproject');
  const openResult = spawnSync('open', [UPROJECT], { stdio: 'inherit' });
  if (openResult.error) {
    fail(openResult.error.message);
  }
  if (openResult.status !== 0) {
    fail(`open failed with exit code ${openResult.status}`, openResult.status ?? 1);
  }
  console.log('    Launched.');
}

console.log('\nCompile finished successfully.');
