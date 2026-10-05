#!/usr/bin/env node
// Generates public test vectors with a fresh, memory-only DISPOSABLE key.
// No private key is exported; this must never supply a production signing key.
import crypto from 'node:crypto';
import { jucePublicKey } from './update_manifest.mjs';

export function generateTestFixture() {
  const pair = crypto.generateKeyPairSync('rsa', { modulusLength: 2048, publicExponent: 65537 });
  const now = 1791158400;
  const payload = { schema: 1, product: 'kirin-hypha', channel: 'stable', version: '1.1.51',
    source_commit: 'a'.repeat(40), published_at: now, expires_at: now + 86400,
    publication_sequence: 5, withdrawn: false, platforms: ['macos', 'windows'], formats: ['AU', 'VST3', 'AAX'] };
  const rawWire = (bytes, algorithm = 'sha256', padding = crypto.constants.RSA_PKCS1_PADDING) =>
    JSON.stringify({ payload: bytes.toString('base64'),
      signature: crypto.sign(algorithm, bytes, { key: pair.privateKey, padding }).toString('base64') });
  const wire = p => rawWire(Buffer.from(JSON.stringify(p)));
  const cases = [{ name: 'valid', wire: wire(payload), expected: true, previous: 4 },
    { name: 'same sequence permitted for cache payload binding', wire: wire(payload), expected: true, previous: 5 },
    { name: 'rollback', wire: wire(payload), expected: false, previous: 6 },
    { name: 'withdrawn', wire: wire({ ...payload, withdrawn: true }), expected: true, previous: 0 }];
  const invalid = [ ['schema string', { schema: '1' }], ['unknown product', { product: 'other' }],
    ['prerelease channel', { channel: 'preview' }], ['prerelease version', { version: '1.2.3-beta' }],
    ['leading zero', { version: '01.2.3' }], ['semver overflow', { version: '2147483648.0.0' }],
    ['bad commit', { source_commit: 'A'.repeat(40) }], ['future', { published_at: now + 1 }],
    ['expired', { expires_at: now }], ['too long lifetime', { expires_at: now + 2592001 }],
    ['fractional time', { published_at: now - 0.5 }], ['negative sequence', { publication_sequence: -1 }],
    ['zero sequence', { publication_sequence: 0 }], ['unsafe sequence', { publication_sequence: 9007199254740992 }],
    ['nonboolean withdrawal', { withdrawn: 0 }], ['unknown field', { url: 'https://invalid.example' }],
    ['empty platforms', { platforms: [] }], ['duplicate platforms', { platforms: ['macos', 'macos'] }],
    ['unknown platform', { platforms: ['linux'] }], ['unknown format', { formats: ['CLAP'] }],
    ['windows AU', { platforms: ['windows'], formats: ['AU'] }] ];
  for (const [name, changes] of invalid) cases.push({ name, wire: wire({ ...payload, ...changes }), expected: false, previous: 0 });
  const validBytes = Buffer.from(JSON.stringify(payload));
  cases.push({ name: 'envelope surrounding JSON whitespace', wire: ` \t\r\n${wire(payload)}\r\n\t `, expected: true, previous: 0 });
  cases.push({ name: 'envelope trailing garbage', wire: `${wire(payload)}garbage`, expected: false, previous: 0 });
  cases.push({ name: 'envelope trailing second object', wire: `${wire(payload)}{}`, expected: false, previous: 0 });
  cases.push({ name: 'envelope leading nonobject', wire: `0${wire(payload)}`, expected: false, previous: 0 });
  cases.push({ name: 'signed payload trailing garbage', wire: rawWire(Buffer.concat([validBytes, Buffer.from('garbage')])), expected: false, previous: 0 });
  cases.push({ name: 'signed payload trailing second object', wire: rawWire(Buffer.concat([validBytes, Buffer.from('{}')])), expected: false, previous: 0 });
  cases.push({ name: 'signed payload surrounding JSON whitespace', wire: rawWire(Buffer.concat([Buffer.from(' \t\r\n'), validBytes, Buffer.from('\r\n\t ')])), expected: true, previous: 0 });
  const duplicate = JSON.stringify(payload).replace('"schema":1', '"schema":1,"schema":1');
  const escaped = JSON.stringify(payload).replace('"schema":1', '"\\u0073chema":1');
  for (const [name, bytes] of [['duplicate payload field', Buffer.from(duplicate)],
    ['escaped payload field', Buffer.from(escaped)], ['invalid UTF8', Buffer.from([0xff, 0xfe])],
    ['embedded NUL', Buffer.concat([validBytes, Buffer.from([0])])]])
    cases.push({ name, wire: rawWire(bytes), expected: false, previous: 0 });
  cases.push({ name: 'wrong hash', wire: rawWire(validBytes, 'sha512'), expected: false, previous: 0 });
  cases.push({ name: 'PSS signature', wire: rawWire(validBytes, 'sha256', crypto.constants.RSA_PKCS1_PSS_PADDING), expected: false, previous: 0 });
  const e = JSON.parse(wire(payload));
  for (const [name, signature] of [['short signature', Buffer.alloc(255)], ['oversized signature', Buffer.alloc(257)],
    ['zero signature', Buffer.alloc(256)], ['signature at modulus', Buffer.from(pair.publicKey.export({ format: 'jwk' }).n, 'base64url')]])
    cases.push({ name, wire: JSON.stringify({ ...e, signature: signature.toString('base64') }), expected: false, previous: 0 });
  cases.push({ name: 'tampered payload', wire: JSON.stringify({ ...e, payload: Buffer.from('tampered').toString('base64') }), expected: false, previous: 0 });
  cases.push({ name: 'duplicate envelope', wire: JSON.stringify(e).replace('"payload":', `"payload":"${e.payload}","payload":`), expected: false, previous: 0 });
  const encoded = Buffer.alloc(256, 0xff); encoded[0] = 0; encoded[1] = 1; encoded[204] = 0;
  Buffer.from('3031300d060960864801650304020105000420', 'hex').copy(encoded, 205);
  crypto.createHash('sha256').update(validBytes).digest().copy(encoded, 224);
  // Correct digest with non-FF padding must fail the strict encoded-message comparison.
  encoded[30] = 0xfe;
  cases.push({ name: 'malformed PKCS padding with correct digest', wire: JSON.stringify({ ...e,
    signature: crypto.privateEncrypt({ key: pair.privateKey, padding: crypto.constants.RSA_NO_PADDING }, encoded).toString('base64') }),
    expected: false, previous: 0 });
  return { notice: 'Fresh disposable test key. Private key not retained. NEVER production.',
    publicKey: jucePublicKey(pair.publicKey), now, cases };
}

if (process.argv[1]?.endsWith('generate_test_fixture.mjs')) console.log(JSON.stringify(generateTestFixture(), null, 2));
