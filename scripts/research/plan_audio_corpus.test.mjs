import { test } from 'node:test';
import assert from 'node:assert/strict';
import { planCorpus } from './plan_audio_corpus.mjs';

const fixture = () => ({ schema: 'hypha.research.audio-inventory.v1', entries:
  Array.from({ length: 80 }, (_, i) => ['song.flac', 'song-remaster.flac'].map(file => ({
    root: '/music', relative: `artist-${i}/album/${file}`, path: `/music/artist-${i}/album/${file}`,
    extension: '.flac', bytes: 42,
  }))).flat() });

test('both tasks have separate development/evaluation artists; alternate versions remain together', () => {
  const result = planCorpus(fixture(), '/music');
  assert.equal(result.partitions.length, 64);
  assert.equal(new Set(result.partitions.map(item => item.artist_group_sha256)).size, 64);
  assert.deepEqual(result.counts, { attack_development: 20, attack_evaluation_reserved: 20,
    space_development: 12, space_evaluation_reserved: 12 });
  assert(result.partitions.every(item => item.other_artist_files.length === 1
    && !item.source_duration_verified && item.prior_exposure === 'unknown'));
  assert.deepEqual(result, planCorpus(fixture(), '/music'));
});
test('unknown schemas, wrong roots and inadequate groups are rejected', () => {
  assert.throws(() => planCorpus({ ...fixture(), schema: 'other' }, '/music'));
  assert.throws(() => planCorpus(fixture(), '/missing'));
  assert.throws(() => planCorpus({ ...fixture(), entries: fixture().entries.slice(0, 20) }, '/music'));
});
test('unknown directory layout is explicitly excluded, not treated as an artist', () => {
  const input = fixture();
  input.entries.push({ root: '/music', relative: 'album/file.flac', path: '/music/album/file.flac', extension: '.flac' });
  const result = planCorpus(input, '/music');
  assert.equal(result.excluded.length, 1);
  assert.equal(result.available_artist_groups, 80);
});
