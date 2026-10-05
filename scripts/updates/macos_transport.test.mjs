import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const strip = source => source.replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/[^\n]*/gm, '');
const code = strip(read('juce_shell/src/update/UpdateTransportMac.mm'));
const parser = strip(read('juce_shell/src/update/UpdateHttpResponse.cpp'));
const header = read('juce_shell/src/update/UpdateHttpResponse.h');
const entry = read('juce_shell/src/update/UpdateTransport.cpp');

test('macOS updater sends exactly one raw fixed GET with no native HTTP resend layer', () => {
  assert.match(entry, /#elif JUCE_MAC\s+return fetchOfficialManifestMac \(cancelled\)/);
  assert.doesNotMatch(entry + code, /\bWebInputStream\b|NSMutableData|NSURLSession|curl_easy|CFHTTP/);
  assert.match(code, /"GET \/updates\/hypha-stable\.v1\.json HTTP\/1\.1\\r\\n"/);
  assert.match(code, /"Host: kirinmastering\.com\\r\\nAccept: application\/json/);
  assert.match(code, /return fetch \(state, "kirinmastering\.com", "443", officialRequest, true\)/);
  assert.equal((code.match(/\bnw_connection_send\s*\(/g) ?? []).length, 1);
  assert.match(code, /if \(state->sent \|\| state->stopping/);
  assert.match(code, /state->sent = true/);
  assert.match(code, /phase == nw_connection_state_ready\) \{ sendOnce/);
  assert.doesNotMatch(code, /NW_CONNECTION_SEND_IDEMPOTENT_CONTENT|nw_connection_restart|nw_connection_cancel_current_endpoint/);
  assert.match(code, /nw_parameters_set_fast_open_enabled \(parameters, false\)/);
  assert.match(code, /nw_tcp_options_set_enable_fast_open \(options, false\)/);
  assert.match(code, /phase == nw_connection_state_failed \|\| phase == nw_connection_state_waiting/);
});

test('macOS raw connection has system certificate validation, fixed SNI and HTTP1.1 only', () => {
  assert.match(code, /sec_protocol_options_set_tls_server_name \(secure, "kirinmastering\.com"\)/);
  assert.match(code, /sec_protocol_options_set_peer_authentication_required \(secure, true\)/);
  assert.match(code, /sec_protocol_options_set_min_tls_protocol_version \(secure, tls_protocol_version_TLSv12\)/);
  assert.match(code, /sec_protocol_options_add_tls_application_protocol \(secure, "http\/1\.1"\)/);
  assert.doesNotMatch(code, /set_verify_block|set_local_identity|Authorization|Cookie|set_challenge_block/);
  assert.match(code, /sec_protocol_options_set_tls_resumption_enabled \(secure, false\)/);
  assert.match(code, /sec_protocol_options_set_tls_false_start_enabled \(secure, false\)/);
});

test('native receive and HTTP header/body/chunk buffers are bounded before retention', () => {
  assert.match(code, /nw_connection_receive \(state->connection, 1, 4096/);
  assert.match(header, /maximumHeaderBytes = 8 \* 1024/);
  assert.match(header, /std::array<char, maximumManifestBytes> body/);
  assert.match(header, /std::array<char, maximumHeaderBytes> header/);
  assert.match(parser, /if \(bodyUsed == body\.size\(\)\) return fail\(\)/);
  assert.match(parser, /if \(headerUsed == header\.size\(\)/);
  assert.match(parser, /number \(value, 10, maximumManifestBytes, remaining\)/);
  assert.match(parser, /body\.size\(\) - bodyUsed, remaining/);
  assert.match(parser, /status\.substr \(9, 4\) != "200 "/);
  assert.match(parser, /"application\/json"/);
  assert.match(parser, /"identity"/);
  assert.match(parser, /length && transfer/);
  assert.match(code, /std::memchr/);
  assert.match(code, /CharPointer_UTF8::isValidString/);
});

test('deadline cancellation drains final callback and every send/receive before releasing handles', () => {
  assert.match(code, /totalTimeout = std::chrono::seconds \(3\)/);
  assert.match(code, /Clock::now\(\) >= deadline/);
  assert.match(code, /wait_until \(lock, std::min \(state\.deadline/);
  assert.match(code, /dispatch_queue_create \("KirinHyphaUpdateTransport", DISPATCH_QUEUE_SERIAL\)/);
  assert.match(code, /state\.connectionCancelled && state\.outstandingSends == 0 && state\.outstandingReceives == 0/);
  assert.match(code, /--state->outstandingSends/);
  assert.match(code, /--state->outstandingReceives/);
  const destroy = code.slice(code.indexOf('~NativeRequest()'), code.indexOf('bool start'));
  const drain = destroy.indexOf('nw_connection_set_state_changed_handler (active->connection, nullptr)');
  assert.ok(destroy.indexOf('state.changed.wait') < drain && destroy.indexOf('nw_release (state.connection)') > drain);
  assert.doesNotMatch(code, /static (?:NativeRequest|CallbackState|nw_connection_t)/);
});

test('test-only transport endpoint is fixed loopback and native dropped socket remains one GET', () => {
  assert.match(code, /#if defined \(HYPHA_UPDATE_TRANSPORT_TESTING\)/);
  assert.match(code, /fetch \(state, "127\.0\.0\.1", port\.toRawUTF8\(\), request\.toRawUTF8\(\), false\)/);
  const fixture = read('juce_shell/tests/update_transport_mac_test.mm');
  assert.match(fixture, /htonl \(INADDR_LOOPBACK\)/);
  assert.match(fixture, /require \(server\.requests == 1/);
  assert.match(fixture, /require \(probe\.sends == 1/);
  assert.match(fixture, /dropped fresh connection fails/);
  assert.doesNotMatch(fixture, /manifestEndpoint|kirinmastering\.com|Application Support|UpdateStore|state\.json/);
});
