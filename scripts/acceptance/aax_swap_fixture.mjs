import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { inspectBundle } from './aax_swap_preflight.mjs';
import { prepareSession } from './aax_swap_session.mjs';
import { buildAuthorization } from './aax_swap_command.mjs';
import { connectWorker } from './aax_swap_transport.mjs';

// Test-only anchor mapping. No production entry point imports this helper.
export async function fixture(t, { edit = body => body, prepare = () => {}, receipt = async () => {}, sizeBytes = 0 } = {}) {
  const root = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'aax-shell-'));
  const live = path.join(root, 'Library/Application Support/Avid/Audio/Plug-Ins');
  fs.mkdirSync(live, { recursive: true, mode: 0o700 });
  const sources = ['PRE', 'POST'].map(role => {
    const source = path.join(root, role);
    const installed = path.join(live, `Kirin Hypha ${role}.aaxplugin`);
    for (const [dir, content] of [[source, sizeBytes ? Buffer.alloc(sizeBytes, 65) : `candidate-${role}`], [installed, `original-${role}`]]) {
      fs.mkdirSync(dir, { mode: 0o700 });
      fs.mkdirSync(path.join(dir, 'Contents'), { mode: 0o700 });
      fs.writeFileSync(path.join(dir, 'Contents/payload'), content, { mode: 0o600 });
      fs.writeFileSync(path.join(dir, "quote'$()-日本語"), 'literal', { mode: 0o600 });
    }
    return { role, source, sha256: inspectBundle(source).sha256 };
  });
  const other = path.join(live, 'Other.aaxplugin'); fs.mkdirSync(other); fs.writeFileSync(path.join(other, 'keep'), 'outside');
  const request = await prepareSession({ sources, manifest: ['PRE', 'POST'].map(role => ({ role,
    install_relative: `Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha ${role}.aaxplugin` })),
  inspectInstalled: async ({ role }) => inspectBundle(path.join(live, `Kirin Hypha ${role}.aaxplugin`)) });
  const built = buildAuthorization(request, { uid: process.getuid(), parentPid: process.pid, token: 'd'.repeat(32) });
  const command = { ...built, ipcDirectory: root + built.ipcDirectory, recoveryDirectory: root + built.recoveryDirectory };
  let body = built.rootScript.replaceAll('/Library', root + '/Library').replace('TRUSTED_UID=0', `TRUSTED_UID=${process.getuid()}`).replace('TRUSTED_GID=0', `TRUSTED_GID=${process.getgid()}`).replace('secure_dir /\n', `secure_dir '${root}'\n`);
  body = edit(body);
  prepare({ root, command, live });
  const child = spawn('/usr/bin/env', ['-i', 'PATH=/usr/bin:/bin:/usr/sbin:/sbin', 'LC_ALL=C', 'HOME=/var/empty', '/bin/sh', '-c', body, '--', String(process.getuid()), String(process.pid), built.config.token, built.config.envelopeSha256, String(built.config.payloadSize), String(built.config.wireSize)], { stdio: ['ignore', 'pipe', 'pipe'] });
  let stderr = '';
  child.stderr.on('data', data => { stderr += data; });
  const exited = new Promise(resolve => child.once('close', (code, signal) => resolve({ code, signal })));
  t.after(async () => {
    if (child.exitCode === null && child.signalCode === null) child.kill('SIGTERM');
    await exited;
    fs.rmSync(root, { recursive: true, force: true });
  });
  return { root, live, command, child, exited, stderr: () => stderr,
    connect: () => connectWorker(command, child, { ownerUid: process.getuid(), receipt, timeout: 15000 }),
    content: role => fs.readFileSync(path.join(live, `Kirin Hypha ${role}.aaxplugin/Contents/payload`), 'utf8') };
}
