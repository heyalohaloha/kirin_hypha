import crypto from 'node:crypto';
import fs from 'node:fs';
import { quoteShell } from './aax_swap_preflight.mjs';
import { shellPayload } from './aax_swap_shell_plan.mjs';

export const SYSTEM_TOOLS = Object.freeze(['/bin/sh', '/bin/mv', '/bin/mkdir', '/bin/rmdir', '/bin/rm', '/bin/chmod', '/bin/ls', '/bin/sync', '/usr/bin/env', '/usr/bin/id', '/usr/bin/ditto', '/usr/bin/find', '/usr/bin/stat', '/usr/bin/openssl', '/usr/bin/base64', '/usr/bin/xattr', '/usr/bin/sort', '/usr/bin/tr', '/usr/bin/mkfifo', '/usr/bin/csrutil', '/usr/bin/grep', '/usr/sbin/chown', '/sbin/mount']);
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
export function fixedShellScript() { return fs.readFileSync(new URL('./aax_swap_root.sh', import.meta.url), 'utf8'); }
export function appleScriptFor(shellScript) {
  const literal = shellScript.replaceAll('\\', '\\\\').replaceAll('"', '\\"').replaceAll('\n', '\\n').replaceAll('\r', '\\r');
  return `with timeout of 43200 seconds\n do shell script "${literal}" with administrator privileges\nend timeout`;
}
export function buildAuthorization(request, { uid, parentPid, token } = {}) {
  if (!request || Object.keys(request).join(',') !== 'candidates' || !Array.isArray(request.candidates)
      || request.candidates.map(item => item.role).sort().join(',') !== 'POST,PRE') throw new Error('invalid candidate roles');
  const fields = ['role', 'target', 'sha256', 'metadataSha256', 'originalHash', 'originalMetadataSha256', 'treeSha256', 'originalTreeSha256', 'snapshot'].sort().join(',');
  for (const item of request.candidates) {
    if (Object.keys(item).sort().join(',') !== fields) throw new Error('unexpected candidate fields');
    if (item.target !== `/Library/Application Support/Avid/Audio/Plug-Ins/Kirin Hypha ${item.role}.aaxplugin`) throw new Error('unexpected installation target');
    for (const field of ['sha256', 'metadataSha256', 'originalHash', 'originalMetadataSha256', 'treeSha256', 'originalTreeSha256']) {
      if (!/^[a-f0-9]{64}$/.test(item[field] || '')) throw new Error('invalid candidate hash');
    }
    if (item.snapshot.metadataSha256 !== item.metadataSha256) throw new Error('metadata hash mismatch');
  }
  if (!Number.isInteger(uid) || uid <= 0 || uid >= 2 ** 31) throw new Error('invalid caller uid');
  if (!Number.isInteger(parentPid) || parentPid <= 1 || parentPid >= 2 ** 31) throw new Error('invalid caller pid');
  if (!/^[a-f0-9]{32}$/.test(token || '')) throw new Error('invalid session token');
  const payload = shellPayload(request), payloadSize = Buffer.byteLength(payload);
  if (payloadSize > 768 * 1024 * 1024) throw new Error('authorization payload exceeds limit');
  const wireSize = 4 * Math.ceil(payloadSize / 3);
  if (wireSize > 768 * 1024 * 1024) throw new Error('wire payload exceeds limit');
  const config = Object.freeze({ uid, parentPid, token, envelopeSha256: hash(payload), payloadSize, wireSize });
  const rootScript = fixedShellScript(), scriptSha256 = hash(rootScript);
  const shellScript = `/usr/bin/env -i PATH=/usr/bin:/bin:/usr/sbin:/sbin HOME=/var/empty LC_ALL=C OPENSSL_CONF=/dev/null /bin/sh -c ${quoteShell(rootScript)} -- ${[uid, parentPid, token, config.envelopeSha256, payloadSize, wireSize].map(value => quoteShell(String(value))).join(' ')}`;
  const args = Object.freeze(['-e', appleScriptFor(shellScript)]);
  if (Buffer.byteLength(args[1]) > 120 * 1024) throw new Error('fixed script exceeds command size');
  return Object.freeze({ executable: '/usr/bin/osascript', args, rootScript, shellScript, scriptSha256,
    commandSha256: hash(shellScript), payload, config,
    ipcDirectory: `/Library/Application Support/Kirin/Hypha/acceptance-swap-ipc/${token}`,
    recoveryDirectory: `/Library/Application Support/Kirin/Hypha/acceptance-swap/${token}` });
}
export function verifyAuthorization(command) {
  if (hash(command.rootScript) !== command.scriptSha256 || hash(fixedShellScript()) !== command.scriptSha256
      || hash(command.shellScript) !== command.commandSha256 || hash(command.payload) !== command.config.envelopeSha256
      || Buffer.byteLength(command.payload) !== command.config.payloadSize
      || 4 * Math.ceil(command.config.payloadSize / 3) !== command.config.wireSize
      || command.executable !== '/usr/bin/osascript' || command.args[0] !== '-e'
      || command.args[1] !== appleScriptFor(command.shellScript)) throw new Error('authorization command changed before authentication');
  return true;
}
