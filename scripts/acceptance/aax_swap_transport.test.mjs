import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { connectWorker } from './aax_swap_transport.mjs';
import { fixture } from './aax_swap_fixture.mjs';
const macTest = process.platform === 'darwin' ? test : test.skip;

test('startup errors include exit status and spawn error even without stderr', async () => {
  const command = { ipcDirectory: '/nonexistent-hypha-test', recoveryDirectory: '/nonexistent-hypha-recovery' };
  await assert.rejects(connectWorker(command, spawn(process.execPath, ['-e', 'process.exit(42)']), { timeout: 15000 }), /exit code 42, signal none; no diagnostic output/);
  await assert.rejects(connectWorker(command, spawn('/nonexistent-hypha-executable'), { timeout: 15000 }), /worker startup failed:.*ENOENT/);
});

macTest('silent system protection failure reports its startup stage', async t => {
  const f = await fixture(t, { edit: body => {
    assert.ok(body.includes('\nsystem_guard\n'));
    return body.replace('\nsystem_guard\n', '\nfalse\n');
  } });
  await assert.rejects(f.connect(), /worker startup failed at system-protection \(exit 1\)/);
  await f.exited;
  assert.equal(fs.existsSync(f.command.recoveryDirectory), false);
});

for (const [name, protection, diagnostic] of [
  ['SIP disabled', { sip: 'disabled' }, /SIP verification failed/],
  ['authenticated root disabled', { authenticatedRoot: 'disabled' }, /authenticated root verification failed/],
  ['unsealed system volume', { sealed: false }, /sealed read-only system volume verification failed/],
  ['writable system volume', { readOnly: false }, /sealed read-only system volume verification failed/],
]) {
  macTest(`${name} is refused by the worker protection predicate`, async t => {
    const f = await fixture(t, { protection });
    await assert.rejects(f.connect(), diagnostic);
    await f.exited;
    assert.equal(fs.existsSync(f.command.recoveryDirectory), false);
    assert.equal(f.content('PRE'), 'original-PRE');
    assert.equal(f.content('POST'), 'original-POST');
  });
}

macTest('unsafe fixture root reports its mode before creating a recovery directory', async t => {
  const f = await fixture(t, { prepare: ({ root }) => fs.chmodSync(root, 0o770) });
  await assert.rejects(f.connect(), /unsafe mode: .*770/);
  await f.exited;
  assert.equal(fs.existsSync(f.command.recoveryDirectory), false);
});

macTest('system shell transfers bytes, installs, restores and exits in one worker', async t => {
  const f = await fixture(t);
  const worker = await f.connect();
  await worker.install();
  assert.equal(f.content('PRE'), 'candidate-PRE');
  // Simulate temporary-area cleaning; recovery data is outside that area.
  const disposable = path.join(f.root, 'temporary-cleaner-target'); fs.mkdirSync(disposable);
  fs.writeFileSync(path.join(disposable, 'aged'), 'temporary');
  const cleaner = fs.readFileSync('/usr/libexec/tmp_cleaner', 'utf8');
  assert.ok(cleaner.includes('daily_clean_tmps_dirs="/tmp"') && cleaner.includes('daily_clean_tmps_days="3"'));
  const mapped = cleaner.replace('daily_clean_tmps_dirs="/tmp"', `daily_clean_tmps_dirs="${disposable}"`).replace('daily_clean_tmps_days="3"', 'daily_clean_tmps_days="-1"');
  const cleaned = spawnSync('/bin/sh', ['-c', mapped], { encoding: 'utf8' });
  assert.equal(cleaned.status, 0, cleaned.stderr);
  assert.equal(fs.existsSync(path.join(disposable, 'aged')), false);
  assert.equal(fs.existsSync(path.join(f.command.recoveryDirectory, 'original-PRE/Contents/payload')), true);
  await worker.restore();
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.equal(fs.readFileSync(path.join(f.live, 'Other.aaxplugin/keep'), 'utf8'), 'outside');
  assert.equal((await f.exited).code, 0, f.stderr());
  assert.equal(fs.existsSync(f.command.recoveryDirectory), false);
  assert.equal(fs.existsSync(f.command.ipcDirectory), false);
});

macTest('copy hash mismatch leaves both installed originals unmoved', async t => {
  const f = await fixture(t, { edit: body => body.replace('  tree_matches "$session/candidate-$role" "$expected" || return 1', '  printf tamper >> "$session/candidate-$role/Contents/payload"\n  tree_matches "$session/candidate-$role" "$expected" || return 1') });
  const worker = await f.connect();
  await assert.rejects(worker.install(), /recovery|worker/);
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.equal(fs.existsSync(path.join(f.command.recoveryDirectory, 'original-PRE')), false);
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /stopped/);
  await f.exited;
});

for (const role of ['PRE', 'POST']) {
  for (const point of ['backup-complete', 'install-complete']) {
    macTest(`failure after ${point} ${role} restores both originals`, async t => {
      const f = await fixture(t, { edit: body => body.replace(`  journal "${point} $role"`, `  journal "${point} $role"\n  [ "$role" != ${role} ]`) });
      const worker = await f.connect();
      await assert.rejects(worker.install(), /recovery|worker/);
      await f.exited;
      assert.equal(f.content('PRE'), 'original-PRE', f.stderr()); assert.equal(f.content('POST'), 'original-POST');
      assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /rollback-verified/);
    });
  }
}

macTest('modified backup refuses restoration before changing either candidate', async t => {
  const f = await fixture(t); const worker = await f.connect(); await worker.install();
  fs.appendFileSync(path.join(f.command.recoveryDirectory, 'original-POST/Contents/payload'), 'tamper');
  await assert.rejects(worker.restore(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'candidate-PRE'); assert.equal(f.content('POST'), 'candidate-POST');
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /recovery-needed/);
});

macTest('receipt failure restores and exits while retaining the durable journal', async t => {
  const f = await fixture(t, { receipt: async () => { throw new Error('fixture receipt failure'); } });
  const worker = await f.connect(); await assert.rejects(worker.install(), /receipt failure/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /restored-verified/);
});

macTest('large transferred snapshot tolerates partial writes and backpressure', async t => {
  const f = await fixture(t, { sizeBytes: 1024 * 1024 }); const worker = await f.connect();
  await worker.install(); assert.equal(f.content('PRE').length, 1024 * 1024);
  await worker.restore(); assert.equal((await f.exited).code, 0);
});

macTest('ACL on the permanent recovery root is refused before any backup rename', async t => {
  const f = await fixture(t, { prepare: ({ root, command }) => {
    const base = path.dirname(command.recoveryDirectory); fs.mkdirSync(base, { recursive: true, mode: 0o700 });
    const result = spawnSync('/bin/chmod', ['+a', 'everyone allow add_file,add_subdirectory,delete_child', base], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
  } });
  await assert.rejects(f.connect(), /startup/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.equal(fs.existsSync(path.join(f.command.recoveryDirectory, 'original-PRE')), false);
});

macTest('termination during acceptance restores the original pair and retains evidence', async t => {
  const f = await fixture(t); const worker = await f.connect(); await worker.install();
  f.child.kill('SIGTERM'); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /rollback-verified/);
});

macTest('recovery ancestor symlink is refused without entering its destination', async t => {
  let outside;
  const f = await fixture(t, { prepare: ({ root }) => {
    outside = path.join(root, 'outside'); fs.mkdirSync(outside);
    fs.symlinkSync(outside, path.join(root, 'Library/Application Support/Kirin'));
  } });
  await assert.rejects(f.connect(), /startup/); await f.exited;
  assert.deepEqual(fs.readdirSync(outside), []);
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
});

macTest('changed original hash is refused before the first backup move', async t => {
  const f = await fixture(t, { prepare: ({ live }) => fs.appendFileSync(path.join(live, 'Kirin Hypha POST.aaxplugin/Contents/payload'), 'changed') });
  const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POSTchanged');
  assert.equal(fs.existsSync(path.join(f.command.recoveryDirectory, 'original-PRE')), false);
});

macTest('missing receipt acknowledgement retains the verified restoration journal', async t => {
  const f = await fixture(t, { edit: body => body.replace('control 120', 'control 1'),
    receipt: async value => { if (value.status === 'restored') await f.exited; } });
  const worker = await f.connect(); await worker.install();
  await assert.rejects(worker.restore()); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /restored-verified/);
});

macTest('journal write failure blocks restoration before its first rename', async t => {
  const f = await fixture(t, { edit: body => body.replace('printf \'%s\\n\' "$*" >> "$session/journal"', 'log="$session/journal"; [ "$*" != "restore-intent PRE" ] || log="$session/missing/journal"; printf \'%s\\n\' "$*" >> "$log"') });
  const worker = await f.connect(); await worker.install();
  await assert.rejects(worker.restore()); await f.exited;
  assert.equal(f.content('PRE'), 'candidate-PRE'); assert.equal(f.content('POST'), 'candidate-POST');
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /recovery-needed/);
});

macTest('truncated transfer times out without moving the installed pair', async t => {
  const f = await fixture(t, { edit: body => body.replace('deadline=$((SECONDS+600))', 'deadline=$((SECONDS+1))'), prepare: ({ command }) => { command.payload = ''; } });
  const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.equal(fs.existsSync(path.join(f.command.recoveryDirectory, 'original-PRE')), false);
});

macTest('another-volume anchor is refused before any backup move', async t => {
  const f = await fixture(t, { edit: body => body.replace('volume_device=$(/usr/bin/stat -f %d "$LIVE")', 'volume_device=999999999') });
  await assert.rejects(f.connect(), /startup/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
});

for (const flag of ['', '-s']) {
  macTest(`copied candidate ${flag ? 'symlink' : 'hardlink'} is refused without changing another bundle`, async t => {
    const f = await fixture(t, {
      prepare: ({ live }) => {
        fs.writeFileSync(path.join(live, 'Other.aaxplugin/keep'), 'candidate-PRE');
        fs.chmodSync(path.join(live, 'Other.aaxplugin/keep'), 0o600);
      },
      edit: body => body.replace('  tree_matches "$session/candidate-$role" "$expected" || return 1',
        `  if [ "$role" = PRE ]; then /bin/rm "$session/candidate-$role/Contents/payload"; /bin/ln ${flag} "$LIVE/Other.aaxplugin/keep" "$session/candidate-$role/Contents/payload"; fi\n  tree_matches "$session/candidate-$role" "$expected" || return 1`),
    });
    const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
    assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
    const other = path.join(f.live, 'Other.aaxplugin/keep');
    assert.equal(fs.readFileSync(other, 'utf8'), 'candidate-PRE'); assert.equal(fs.statSync(other).mode & 0o777, 0o600);
  });
}

macTest('tree validation: rejects a trailing symlink in the copied candidate before install', async t => {
  const marker = '  /usr/bin/ditto --rsrc --extattr --acl "$inbound" "$session/candidate-$role"';
  const f = await fixture(t, { edit: body => {
    assert.ok(body.includes(marker));
    return body.replace(marker, `${marker}\n  /bin/ln -s "$LIVE/Other.aaxplugin/keep" "$session/candidate-$role/zz-unvalidated"`);
  } });
  const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
  assert.equal(fs.existsSync(path.join(f.command.recoveryDirectory, 'original-PRE')), false);
});

macTest('tree validation: rejects an unsupported xattr on the final copied file before install', async t => {
  const marker = '  /usr/bin/ditto --rsrc --extattr --acl "$inbound" "$session/candidate-$role"';
  const f = await fixture(t, { edit: body => {
    assert.ok(body.includes(marker));
    const injection = String.raw`  /usr/bin/xattr -w com.example.unvalidated extra "$session/candidate-$role/quote'\$()-日本語"`;
    return body.replace(marker, `${marker}\n${injection}`);
  } });
  const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
});

macTest('tree validation: refuses a trailing symlink in a backup before moving either candidate', async t => {
  const f = await fixture(t); const worker = await f.connect(); await worker.install();
  fs.symlinkSync('absent', path.join(f.command.recoveryDirectory, 'original-PRE/zz-unvalidated'));
  await assert.rejects(worker.restore(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'candidate-PRE'); assert.equal(f.content('POST'), 'candidate-POST');
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /recovery-needed/);
  assert.equal(fs.lstatSync(path.join(f.command.recoveryDirectory, 'original-PRE/zz-unvalidated')).isSymbolicLink(), true);
});

macTest('tree validation: refuses a tree enumeration failure even when it outputs the expected hash', async t => {
  const marker = '/usr/bin/find -x "$tree" -print | /usr/bin/sort';
  const f = await fixture(t, { edit: body => {
    assert.ok(body.includes(marker));
    return body.replace(marker, '(/usr/bin/find -x "$tree" -print; false) | /usr/bin/sort');
  } });
  const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
});

macTest('tree validation: propagates digest command failure even when it prints a digest', () => {
  const body = fs.readFileSync(new URL('./aax_swap_root.sh', import.meta.url), 'utf8');
  const definitions = body.split('\nsystem_guard\n')[0]
    .replace('TRUSTED_UID=0', `TRUSTED_UID=${process.getuid()}`)
    .replace('TRUSTED_GID=0', `TRUSTED_GID=${process.getgid()}`)
    .replace('/usr/bin/openssl dgst -sha256', '/usr/bin/openssl dgst -sha256; false');
  const result = spawnSync('/usr/bin/env', ['-i', 'PATH=/usr/bin:/bin:/usr/sbin:/sbin', 'LC_ALL=C', 'HOME=/var/empty',
    '/bin/sh', '-c', definitions + '\nset +e\nif output=$(digest < /dev/null); then exit 0; else exit 1; fi\n', '--', String(process.getuid()), String(process.pid),
    'a'.repeat(32), 'b'.repeat(64), '1', '4'], { encoding: 'utf8' });
  assert.notEqual(result.status, 0, 'failed digest must not succeed even if stdout is a valid digest');
});

macTest('tree validation: stat failure cannot be masked by matching stdout', async t => {
  const marker = '/usr/bin/stat -f %d "$item"';
  const f = await fixture(t, { edit: body => {
    assert.ok(body.includes(marker));
    return body.replace(marker, '/usr/bin/stat -f %d "$item"; false');
  } });
  const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
});

for (const [kind, injection] of [
  ['hardlink', '/bin/ln "$LIVE/Other.aaxplugin/keep" "$session/candidate-$role/zz-unvalidated"'],
  ['FIFO', '/usr/bin/mkfifo -m 600 "$session/candidate-$role/zz-unvalidated"'],
  ['ACL', '/bin/mkdir -m 700 "$session/candidate-$role/zz-unvalidated"; /bin/chmod +a "everyone allow read" "$session/candidate-$role/zz-unvalidated"'],
]) {
  macTest(`tree validation: rejects a trailing unvalidated ${kind}`, async t => {
    const marker = '  /usr/bin/ditto --rsrc --extattr --acl "$inbound" "$session/candidate-$role"';
    const f = await fixture(t, { edit: body => {
      assert.ok(body.includes(marker)); return body.replace(marker, `${marker}\n  ${injection}`);
    } });
    const worker = await f.connect(); await assert.rejects(worker.install(), /recovery|worker/); await f.exited;
    assert.equal(f.content('PRE'), 'original-PRE'); assert.equal(f.content('POST'), 'original-POST');
    assert.equal(fs.readFileSync(path.join(f.live, 'Other.aaxplugin/keep'), 'utf8'), 'outside');
  });
}

macTest('tree validation: second backup failure leaves the first candidate unmoved', async t => {
  const f = await fixture(t); const worker = await f.connect(); await worker.install();
  fs.symlinkSync('absent', path.join(f.command.recoveryDirectory, 'original-POST/zz-unvalidated'));
  await assert.rejects(worker.restore(), /recovery|worker/); await f.exited;
  assert.equal(f.content('PRE'), 'candidate-PRE'); assert.equal(f.content('POST'), 'candidate-POST');
  assert.match(fs.readFileSync(path.join(f.command.recoveryDirectory, 'journal'), 'utf8'), /recovery-needed/);
});
