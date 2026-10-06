import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '../..');
const source = fs.readFileSync(path.join(root, 'juce_shell/src/update/UpdateTransportWindows.cpp'), 'utf8');
const header = fs.readFileSync(path.join(root, 'juce_shell/src/update/UpdateTransport.h'), 'utf8');
const stripComments = value => value.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/[^\n]*/gm, '');
const code = stripComments(source);

test('Windows updater has exactly one GET submission and never uses WinINet/JUCE retry transport', () => {
  assert.equal((code.match(/\bWinHttpSendRequest\s*\(/g) || []).length, 1);
  assert.equal((code.match(/\bWinHttpReceiveResponse\s*\(/g) || []).length, 1);
  assert.doesNotMatch(code, /\b(?:WebInputStream|InternetOpen|InternetConnect|HttpSendRequest|WinHttpSetCredentials)\b/);
  assert.match(code, /WinHttpOpenRequest\s*\(connection, L"GET", route/);
  assert.match(code, /WINHTTP_FLAG_SECURE/);
  assert.match(code, /static_assert\s*\(std::string_view\s*\(manifestEndpoint\)/);
  assert.match(header, /https:\/\/kirinmastering\.com\/updates\/hypha-stable\.v1\.json/);
});

test('auth, cookies, redirects, pooling and connection resends are disabled before submitting', () => {
  for (const token of ['WINHTTP_ACCESS_TYPE_NO_PROXY', 'WINHTTP_DISABLE_REDIRECTS',
    'WINHTTP_DISABLE_COOKIES', 'WINHTTP_DISABLE_AUTHENTICATION', 'WINHTTP_DISABLE_KEEP_ALIVE',
    'WINHTTP_OPTION_REDIRECT_POLICY_NEVER', 'WINHTTP_OPTION_CONNECT_RETRIES']) assert.ok(code.includes(token));
  assert.match(code, /DWORD attempts = 1/);
  assert.match(code, /FailedConnectionRetries retries \{ 0, 0 \}/);
  assert.match(code, /WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2/);
  assert.match(code, /failedConnectionRetriesOption, &retries/);
  assert.match(code, /disableGlobalPoolingOption, &disablePooling/);
  assert.match(code, /if \(! handles\.open\(\) \|\| ! handles\.prepare \(state\)/);
});

test('callback state remains alive through session unload and callbacks cannot chain API calls', () => {
  assert.match(code, /WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT, &unloaded/);
  assert.match(code, /if \(unloadNotification\) WaitForSingleObject \(unloaded, INFINITE\)/);
  assert.match(code, /CallbackState state;\s*Handles handles;/);
  const callback = code.slice(code.indexOf('void CALLBACK onStatus'), code.indexOf('struct Handles'));
  assert.doesNotMatch(callback, /\bWinHttp\w+\s*\(/);
  assert.match(callback, /noexcept/);
  assert.match(callback, /catch \(\.\.\.\)/);
  assert.match(code, /WINHTTP_CALLBACK_STATUS_REQUEST_ERROR/);
});

test('whole-operation deadline, cancel, status/MIME, byte bound and UTF8/NUL rejection are explicit', () => {
  assert.match(code, /totalTimeout = std::chrono::seconds \(3\)/);
  assert.match(code, /const auto deadline = Clock::now\(\) \+ totalTimeout/);
  assert.match(code, /Clock::now\(\) >= deadline/);
  assert.match(code, /milliseconds \(50\)/);
  assert.match(code, /status != 200/);
  assert.match(code, /equalsIgnoreCase \("application\/json"\)/);
  assert.match(code, /maximumManifestBytes \+ 1/);
  assert.match(code, /used > maximumManifestBytes/);
  assert.match(code, /std::memchr/);
  assert.match(code, /CharPointer_UTF8::isValidString/);
  assert.match(code, /WinHttpReadData \(handles\.request, state\.bytes\.data\(\) \+ used, capacity, nullptr\)/);
});
