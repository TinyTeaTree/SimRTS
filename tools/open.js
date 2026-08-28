#!/usr/bin/env node
'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const UPROJECT = path.join(ROOT, 'SimRTS', 'SimRTS.uproject');

if (!fs.existsSync(UPROJECT)) {
  console.error(`\nERROR: uproject not found: ${UPROJECT}`);
  process.exit(1);
}

console.log('==> Opening SimRTS.uproject');
const result = spawnSync('open', [UPROJECT], { stdio: 'inherit' });
if (result.error) {
  console.error(`\nERROR: ${result.error.message}`);
  process.exit(1);
}
if (result.status !== 0) {
  console.error(`\nERROR: open failed with exit code ${result.status}`);
  process.exit(result.status ?? 1);
}
console.log('    Launched.');
console.log('    If LAN Login fails with "No route to host", run: npm run troubleshoot');
