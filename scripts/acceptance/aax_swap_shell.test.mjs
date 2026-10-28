import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { fixedShellScript, SYSTEM_TOOLS } from './aax_swap_command.mjs';

test('root program contains only fixed system commands and no general interpreter', () => {
  const body = fixedShellScript();
  assert.doesNotMatch(body, /\b(?:python\d*|perl|node)\b/i);
  assert.doesNotMatch(body, /\/(?:private\/)?tmp|\/var\/folders/);
  assert.match(body, /acceptance-swap/);
  assert.match(body, /\/usr\/bin\/ditto/);
  assert.match(body, /\/usr\/bin\/openssl dgst -sha256/);
  assert.match(body, /restored-verified/);
  assert.ok(SYSTEM_TOOLS.every(tool => tool.startsWith('/') && !tool.includes('CommandLineTools')));
});

test('old interpreter bootstrap and worker entry are removed after the audit', () => {
  for (const name of ['aax_swap_bootstrap.pl', 'aax_swap_entry.py', 'aax_swap_worker.py', 'aax_swap_runtime.py']) {
    assert.equal(fs.existsSync(new URL(name, import.meta.url)), false, name);
  }
});

test('external tool references are absolute and drawn only from the documented system list', () => {
  const body = fixedShellScript();
  const executablePaths = body.match(/\/(?:usr\/(?:s?bin)|s?bin)\/[A-Za-z0-9_.-]+/g) || [];
  assert.ok(executablePaths.length > 30);
  for (const name of executablePaths) assert.ok(SYSTEM_TOOLS.includes(name), name);
  const noComments = body.split('\n').filter(line => !line.trim().startsWith('#')).join('\n');
  const names = SYSTEM_TOOLS.map(value => value.split('/').at(-1)).join('|');
  assert.doesNotMatch(noComments, new RegExp(`(?<![/\\w])(?:${names})(?=[\\s;])`, 'm'));
  assert.doesNotMatch(noComments, /(?:^|\s)(?:\.\.?\/|(?:bin|usr|sbin)\/)/m);
  assert.match(body, /for role in PRE POST; do/);
  assert.match(body, /LIVE='\/Library\/Application Support\/Avid\/Audio\/Plug-Ins'/);
  assert.doesNotMatch(body, /Other\.aaxplugin|CommandLineTools|Frameworks|site[_-]packages|Library\/Perl/);
});

test('root worker uses bounded reads without a background monitor or process termination', () => {
  const body = fixedShellScript();
  assert.doesNotMatch(body, /watchdog|reader_pid|kill -TERM/);
  assert.match(body, /read -r -t 2 -n/);
});
