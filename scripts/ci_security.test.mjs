import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const directory = path.join(root, '.github', 'workflows');
const ci = fs.readFileSync(path.join(directory, 'ci.yml'), 'utf8');
const aax = fs.readFileSync(path.join(directory, 'aax-phase-a.yml'), 'utf8');

// Source policy complements YAML/actionlint validation and server-side review rules.
// It intentionally permits only literal hosted runner labels and one read-only policy.
function verifyBlockMappings(lines) {
  let scalarIndent = null;
  for (const line of lines) {
    const indent = line.match(/^ */)[0].length;
    if (scalarIndent !== null) {
      if (!line.trim() || indent > scalarIndent) continue;
      scalarIndent = null;
    }
    if (/^\s*(?:-\s+)?[\w.'"-]+\s*:\s*[|>][+-]?\d?\s*(?:#.*)?$/.test(line)) {
      scalarIndent = indent;
      continue;
    }
    // Ignore quoted scalars, comments and GitHub expressions; script bodies above
    // remain free to use braces. Workflow mappings must use the scanned block style.
    const yaml = line.replace(/\$\{\{.*?\}\}/g, 'expression');
    let quote = null;
    let visible = '';
    for (let i = 0; i < yaml.length; i += 1) {
      const char = yaml[i];
      if (quote) {
        if (quote === '"' && char === '\\') i += 1;
        else if (char === quote) {
          if (quote === "'" && yaml[i + 1] === "'") i += 1;
          else quote = null;
        }
      } else if (char === '"' || char === "'") quote = char;
      else if (char === '#') break;
      else visible += char;
    }
    assert.doesNotMatch(visible, /[{}]/, 'workflow flow mappings bypass block-style policy scans');
  }
}

function verifyPublicWorkflow(source) {
  const lines = source.replace(/\r\n/g, '\n').split('\n');
  verifyBlockMappings(lines);
  const code = lines.filter(line => !/^\s*#/.test(line)).join('\n');
  assert.doesNotMatch(code, /\$\{\{[^}]*\bsecrets\b/, 'public CI must have no secret expressions');
  assert.doesNotMatch(code, /\bESIGNER_/, 'public CI must have no eSigner credential bindings');
  assert.doesNotMatch(code, /\b(?:pull_request_target|workflow_run|repository_dispatch)\s*:/,
    'public CI must have no privileged downstream event');
  const permissions = lines.filter(line => /^\s*["']?permissions["']?\s*:/.test(line));
  assert.deepEqual(permissions, ['permissions:'], 'one explicit workflow permission policy required');
  const block = code.match(/^permissions:\s*\n((?:[ ]{2}[^\n]*\n)*)/m)?.[1]?.trim();
  assert.equal(block, 'contents: read', 'public CI token must have only contents: read');

  let runners = 0;
  for (let i = 0; i < lines.length; i += 1) {
    const line = lines[i];
    if (/^\s*runs-on:/.test(line)) {
      runners += 1;
      assert.match(line, /^\s*runs-on: (?:ubuntu-latest|macos-15|windows-latest)\s*(?:#.*)?$/,
        'public CI must use a literal GitHub-hosted runner');
    }
    const action = line.match(/^\s*(?:- )?uses:\s*(.*?)\s*(?:#.*)?$/);
    if (action) {
      assert.match(action[1], /^[\w.-]+\/[\w./-]+@[0-9a-f]{40}$/,
        'every external Action must use a full commit SHA');
      const indent = line.match(/^ */)[0].length;
      const options = [];
      for (let j = i + 1; j < lines.length; j += 1) {
        if (/^\s*- /.test(lines[j]) || (lines[j].trim() && lines[j].match(/^ */)[0].length < indent)) break;
        options.push(lines[j]);
      }
      if (action[1].startsWith('actions/checkout@')) {
        assert.match(options.join('\n'), /^\s*persist-credentials: false\s*$/m,
          'checkout must not persist the token for contributor scripts');
      }
      if (action[1].startsWith('dtolnay/rust-toolchain@')) {
        assert.match(options.join('\n'), /^\s*toolchain: stable\s*$/m,
          'pinned Rust Action must retain the explicit stable toolchain');
      }
    }
    const run = line.match(/^( *)(?:run):\s*(.*)$/);
    if (run) {
      const script = [run[2]];
      for (let j = i + 1; j < lines.length; j += 1) {
        if (lines[j].trim() && lines[j].match(/^ */)[0].length <= run[1].length) break;
        script.push(lines[j]);
      }
      assert.doesNotMatch(script.join('\n'), /\$\{\{/, 'pass context values through env, not shell source');
    }
  }
  assert.ok(runners > 0, 'workflow must contain a hosted validation job');
}

function verifyUnsignedWindows(source) {
  const modes = [...source.matchAll(/^\s*WINDOWS_SIGNING:\s*(.*?)\s*$/gm)].map(match => match[1]);
  assert.deepEqual(modes, ['unsigned', 'unsigned', 'unsigned'], 'all Windows steps must remain unsigned');
  assert.match(source, /^\s*WINDOWS_EXTERNAL_VALIDATION: pending\s*$/m);
  assert.doesNotMatch(source, /inputs\.windows_(?:signing|external_validation)/);
  assert.doesNotMatch(source, /setup-java|CodeSignTool/);
}

test('every tracked public workflow keeps a hosted read-only credential-free boundary', () => {
  const files = fs.readdirSync(directory).filter(name => /\.ya?ml$/.test(name));
  assert.ok(files.includes('ci.yml') && files.includes('aax-phase-a.yml'));
  for (const file of files) verifyPublicWorkflow(fs.readFileSync(path.join(directory, file), 'utf8'));
  verifyUnsignedWindows(ci);
});

test('secret routes fail even when unsigned mode remains selected', () => {
  for (const expression of ['secrets.ESIGNER_PASSWORD', "secrets['SIGNING_PASSWORD']"]) {
    const bad = ci.replace('WINDOWS_SIGNING: unsigned', 'WINDOWS_SIGNING: unsigned\n          CREDENTIAL: ${{ ' + expression + ' }}');
    assert.throws(() => verifyPublicWorkflow(bad), /no secret expressions/);
  }
});

test('token write, job overrides and omitted permissions fail closed', () => {
  for (const bad of [ci.replace('contents: read', 'contents: write'),
    ci.replace('permissions:\n  contents: read\n', ''),
    ci.replace('    runs-on: ubuntu-latest', '    permissions: write-all\n    runs-on: ubuntu-latest')]) {
    assert.throws(() => verifyPublicWorkflow(bad), /permission|contents: read/);
  }
});

test('inline job permissions and self-hosted runners cannot bypass block-style scans', () => {
  const bad = ci.replace('  public-history:',
    '  injected: { runs-on: self-hosted, permissions: write-all, steps: [{ run: "echo inline" }] }\n  public-history:');
  assert.throws(() => verifyPublicWorkflow(bad), /flow mappings/);
});

test('same-repository and manual SDK work cannot regain a public self-hosted route', () => {
  assert.throws(() => verifyPublicWorkflow(aax.replace('runs-on: ubuntu-latest',
    'runs-on: [self-hosted, aax-sdk, macOS]')), /literal GitHub-hosted/);
  assert.throws(() => verifyPublicWorkflow(aax.replace('runs-on: ubuntu-latest',
    'runs-on: ${{ inputs.runner }}')), /literal GitHub-hosted/);
});

test('mutable Action refs and persistent checkout tokens are rejected', () => {
  assert.throws(() => verifyPublicWorkflow(ci.replace(/actions\/checkout@[a-f0-9]{40}/,
    'actions/checkout@v6')), /full commit SHA/);
  assert.throws(() => verifyPublicWorkflow(ci.replace('persist-credentials: false',
    'persist-credentials: true')), /must not persist/);
});

test('privileged events and direct event-text shell expansion are rejected', () => {
  assert.throws(() => verifyPublicWorkflow(ci.replace('  pull_request:', '  pull_request_target:')),
    /privileged downstream event/);
  assert.throws(() => verifyPublicWorkflow(ci.replace('run: node --test scripts/ci_security.test.mjs',
    'run: echo "${{ github.event.pull_request.title }}"')), /through env/);
});

test('manual dispatch cannot sign candidates or claim external acceptance', () => {
  assert.throws(() => verifyUnsignedWindows(ci.replace('WINDOWS_SIGNING: unsigned',
    'WINDOWS_SIGNING: signed')), /remain unsigned/);
  assert.throws(() => verifyUnsignedWindows(ci.replace('WINDOWS_EXTERNAL_VALIDATION: pending',
    'WINDOWS_EXTERNAL_VALIDATION: complete')), /WINDOWS_EXTERNAL_VALIDATION/);
});

test('dependency updates cover only present ecosystems without automatic merge', () => {
  const config = fs.readFileSync(path.join(root, '.github', 'dependabot.yml'), 'utf8');
  assert.deepEqual([...config.matchAll(/package-ecosystem:\s*(\S+)/g)].map(match => match[1]),
    ['github-actions', 'cargo']);
  assert.ok(fs.existsSync(path.join(root, 'Cargo.toml')) && fs.existsSync(path.join(root, 'Cargo.lock')));
  assert.equal((config.match(/open-pull-requests-limit: 2/g) || []).length, 2);
  assert.doesNotMatch(ci + aax + config, /--auto(?:\s|$)|enablePullRequestAutoMerge|automerge/);
});

// GitHub runs a Windows step under pwsh unless it names another shell, and pwsh
// fails the step only on the last native command's exit code. An earlier native
// command needs its own $LASTEXITCODE check, or its failure is hidden.
function hiddenWindowsNativeFailures(source) {
  const nativeCommand = /^(?:&\s*)?(?:cargo|node|npm|npx|cmake|ctest|python3?|bash|git|iscc|signtool|pluginval|choco|rustup)(?:\.exe|\.cmd)?\b/i;
  const findings = [];
  const lines = source.split('\n');
  let windows = false;
  let step = null;
  const finish = () => {
    if (!step || !windows || step.run === null || !/^(?:pwsh|powershell)$/.test(step.shell)) return;
    let pending = null;
    for (const line of step.run.replace(/`\n/g, ' ').split('\n').map(value => value.trim())) {
      if (!line || line.startsWith('#')) continue;
      if (/\$LASTEXITCODE/.test(line)) pending = null;
      if (nativeCommand.test(line)) {
        if (pending) findings.push(`${step.name}: "${pending}" has no $LASTEXITCODE check before the next command`);
        pending = line.slice(0, 60);
      }
    }
  };
  for (let i = 0; i < lines.length; i += 1) {
    const line = lines[i];
    if (/^  [\w-]+:\s*$/.test(line)) { finish(); step = null; windows = false; continue; }
    const runner = line.match(/^    runs-on:\s*(\S+)/);
    if (runner) windows = runner[1].includes('windows');
    if (/^      - /.test(line)) { finish(); step = { name: '', shell: 'pwsh', run: null }; }
    if (!step) continue;
    const name = line.match(/^      (?:- |  )name:\s*(.+)$/);
    if (name) step.name = name[1];
    const shell = line.match(/^        shell:\s*(\S+)/);
    if (shell) step.shell = shell[1];
    const run = line.match(/^      (?:- |  )run:\s*(.*)$/);
    if (run) {
      if (/^[|>]/.test(run[1])) {
        const body = [];
        while (i + 1 < lines.length && (!lines[i + 1].trim() || /^ {10}/.test(lines[i + 1]))) body.push(lines[(i += 1)]);
        step.run = body.join('\n');
      } else step.run = run[1];
    }
  }
  finish();
  return findings;
}

test('no Windows pwsh step hides an earlier native command failure', () => {
  for (const [name, source] of [['ci.yml', ci], ['aax-phase-a.yml', aax]]) {
    assert.deepEqual(hiddenWindowsNativeFailures(source), [], name);
  }
  const step = run => `jobs:\n  w:\n    runs-on: windows-latest\n    steps:\n      - name: x\n        run: |\n${run}`;
  assert.equal(hiddenWindowsNativeFailures(step('          cargo test a\n          cargo test b\n')).length, 1);
  assert.equal(hiddenWindowsNativeFailures(step('          cargo test a\n          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }\n          cargo test b\n')).length, 0);
  assert.equal(hiddenWindowsNativeFailures(step('          cargo test a\n          cargo test b\n').replace('        run: |', '        shell: bash\n        run: |')).length, 0);
});
