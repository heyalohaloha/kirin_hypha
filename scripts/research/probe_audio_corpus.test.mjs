import { test } from 'node:test';
import assert from 'node:assert/strict';
import { selectDevelopment } from './probe_audio_corpus.mjs';

test('reserved files and the other task are never selected for development probing', () => {
  const items = Array.from({ length: 20 }, (_, index) => ({
    id: `attack_development-${String(index + 1).padStart(2, '0')}`, purpose: 'attack_development',
    path: `/fake/audio-${index}.wav`, artist_group_sha256: `artist-${index}`,
    excerpt_start_seconds: 30, excerpt_duration_seconds: 30 }));
  const proposal = { schema: 'hypha.research.corpus-proposal.v1', partitions: [...items,
    { purpose: 'attack_evaluation_reserved', path: '/do-not-open' },
    { purpose: 'space_development', path: '/do-not-open-either' }] };
  assert.deepEqual(selectDevelopment(proposal, 'attack'), items);
  assert.throws(() => selectDevelopment(proposal, 'holdout'));
  assert.throws(() => selectDevelopment(proposal, 'space'));
  assert.throws(() => selectDevelopment({ ...proposal, partitions: [...items,
    { purpose: 'attack_evaluation_reserved', path: items[0].path }] }, 'attack'));
  assert.throws(() => selectDevelopment({ ...proposal, partitions: [...items,
    { purpose: 'attack_evaluation_reserved', path: '/elsewhere',
      artist_group_sha256: items[0].artist_group_sha256 }] }, 'attack'));
});
