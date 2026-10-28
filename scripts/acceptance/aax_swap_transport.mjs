import fs from 'node:fs';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { SYSTEM_TOOLS, verifyAuthorization } from './aax_swap_command.mjs';

const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
export function checkSystemTools() {
  const checked = command => {
    const result = spawnSync(command[0], command.slice(1), { encoding: 'utf8', env: { PATH: '/usr/bin:/bin:/usr/sbin:/sbin', LC_ALL: 'C' } });
    if (result.status !== 0 || result.error) throw new Error('system protection could not be verified');
    return result.stdout.trim();
  };
  const mount = checked(['/sbin/mount']).split('\n').find(line => / on \/ \(/.test(line));
  if (!/^\/dev\/\S+ on \/ \(apfs, sealed, .*read-only[,)]/.test(mount || '')
      || checked(['/usr/bin/csrutil', 'status']) !== 'System Integrity Protection status: enabled.'
      || checked(['/usr/bin/csrutil', 'authenticated-root', 'status']) !== 'Authenticated Root status: enabled') {
    throw new Error('a sealed read-only system volume and enabled SIP are required');
  }
  const device = fs.statSync('/').dev;
  for (const tool of SYSTEM_TOOLS) {
    const info = fs.lstatSync(tool);
    if (!info.isFile() || info.uid !== 0 || info.dev !== device || (info.mode & 0o022)) throw new Error('unprotected system tool: ' + tool);
  }
  return { mount, systemTools: [...SYSTEM_TOOLS] };
}

export async function authorizeCommand(command, receipt) {
  if (process.platform !== 'darwin' || typeof process.getuid !== 'function' || process.getuid() === 0) throw new Error('run from a regular macOS user account');
  verifyAuthorization(command);
  const protection = checkSystemTools();
  await receipt({ status: 'system-tools-checked', ...protection });
  verifyAuthorization(command); // Last check immediately before the single authentication request.
  const child = spawn(command.executable, command.args, { stdio: ['ignore', 'pipe', 'pipe'], env: { PATH: '/usr/bin:/bin', HOME: process.env.HOME || '/', LC_ALL: 'C' } });
  return connectWorker(command, child, { ownerUid: 0, receipt });
}

// Tests replace only the root anchors and UID; the shell and actual system tools run unchanged.
export async function connectWorker(command, child, { ownerUid = 0, receipt = async () => {}, timeout = 180000 } = {}) {
  let stderr = '', exit;
  child.stderr?.on('data', data => { stderr = (stderr + data.toString()).slice(-65536); });
  child.stdout?.resume();
  const exited = new Promise(resolve => {
    child.once('error', error => { exit = { error }; resolve(exit); });
    child.once('close', (code, signal) => { if (!exit) exit = { code, signal }; resolve(exit); });
  });
  async function waitExit() {
    const end = Date.now() + 40000;
    while (!exit && Date.now() < end) await delay(25);
    if (!exit) throw new Error('worker exit not confirmed: ' + command.recoveryDirectory);
    return exit;
  }
  let handle;
  try {
    const end = Date.now() + timeout;
    while (true) {
      if (exit) throw new Error(`worker startup failed: ${exit.error ? exit.error.message : `exit code ${exit.code}, signal ${exit.signal || 'none'}`}; ${stderr.trim() || 'no diagnostic output'}`);
      try {
        const [directory, fifo, status] = await Promise.all([command.ipcDirectory, path.join(command.ipcDirectory, 'request'), path.join(command.ipcDirectory, 'status')].map(file => fs.promises.lstat(file)));
        if (!directory.isDirectory() || directory.uid !== ownerUid || (directory.mode & 0o022)
            || !fifo.isFIFO() || fifo.uid !== command.config.uid || fifo.nlink !== 1
            || !status.isFile() || status.uid !== ownerUid || status.nlink !== 1 || (status.mode & 0o022)) throw new Error('untrusted worker communication paths');
        break;
      } catch (error) { if (!['ENOENT', 'EACCES'].includes(error.code)) throw error; }
      if (Date.now() > end) throw new Error('worker connection timed out: ' + command.recoveryDirectory);
      await delay(50);
    }
    while (!handle) {
      try { handle = await fs.promises.open(path.join(command.ipcDirectory, 'request'), fs.constants.O_WRONLY | fs.constants.O_NOFOLLOW | fs.constants.O_NONBLOCK); }
      catch (error) {
        if (error.code !== 'ENXIO' || exit || Date.now() > end) throw error;
        await delay(25);
      }
    }
    const info = await handle.stat();
    if (!info.isFIFO() || info.uid !== command.config.uid || info.nlink !== 1) throw new Error('worker FIFO identity changed');
    const write = async text => {
      const bytes = Buffer.from(text);
      for (let offset = 0; offset < bytes.length;) {
        try {
          const { bytesWritten } = await handle.write(bytes, offset, Math.min(65536, bytes.length - offset));
          if (!bytesWritten) throw new Error('short worker write');
          offset += bytesWritten;
        } catch (error) {
          if (error.code !== 'EAGAIN' || exit) throw error;
          await delay(5);
        }
      }
    };
    let seen = 0;
    async function rawStatus() {
      const end = Date.now() + Math.max(timeout, 600000);
      while (Date.now() < end) {
        const lines = (await fs.promises.readFile(path.join(command.ipcDirectory, 'status'), 'utf8')).split('\n');
        if (seen < lines.length - 1) {
          const value = JSON.parse(lines[seen++]);
          if (!['installed', 'restored', 'error'].includes(value.status)) throw new Error('invalid worker status');
          return value;
        }
        if (exit) throw new Error('worker stopped: ' + stderr);
        await delay(25);
      }
      throw new Error('worker status timed out: ' + command.recoveryDirectory);
    }
    async function status() {
      const value = await rawStatus();
      await receipt(value);
      if (value.status === 'error') { await waitExit(); throw new Error('recovery state retained at ' + command.recoveryDirectory); }
      return value;
    }
    async function abort(error) {
      try {
        if (!exit) {
          await write('restore\n');
          const value = await rawStatus();
          if (value.status === 'restored') await write('retain-journal\n');
        }
        await waitExit();
      } catch (recoveryError) {
        error = new AggregateError([error, recoveryError], 'inspect retained recovery state: ' + command.recoveryDirectory);
      }
      throw error;
    }
    await write(Buffer.from(command.payload).toString('base64'));
    let installed = false;
    return {
      install: async () => {
        try {
          if (installed) throw new Error('already installed');
          if ((await status()).status !== 'installed') throw new Error('pair was not installed');
          installed = true;
        } catch (error) { try { await abort(error); } finally { await handle.close(); } }
      },
      restore: async () => {
        try {
          if (!installed) throw new Error('no installed pair');
          await write('restore\n');
          if ((await status()).status !== 'restored') throw new Error('pair was not restored');
          await write('ack\n');
          const result = await waitExit();
          if (result.code !== 0) throw new Error('worker cleanup failed: ' + stderr);
        } catch (error) { await abort(error); }
        finally { await handle.close(); }
      },
    };
  } catch (error) {
    await handle?.close();
    if (!exit) { child.unref(); child.stdout?.destroy(); child.stderr?.destroy(); }
    throw error;
  }
}
