#!/usr/bin/env node

import path from 'node:path';
import { pathToFileURL } from 'node:url';

import { buildPack } from './build_review_pack.mjs';

export const spaceFollowupSelection = [
  ['space', '11'], ['space', '06'],
  ['space', '04'], ['space', '09'],
  ['space', '10'], ['space', '02'],
];

const selectionReasons = {
  'space_development-11': 'modern production screening candidate A; possible audible effect and traceable decay; human judgement pending',
  'space_development-06': 'modern production screening candidate B; possible audible effect and traceable decay; human judgement pending',
  'space_development-04': 'modern dense-production screening candidate A; possible overlapping tails; human judgement pending',
  'space_development-09': 'modern dense-production screening candidate B; possible overlapping tails; human judgement pending',
  'space_development-10': 'modern contrast screening candidate A; traceable decay may be absent; human judgement pending',
  'space_development-02': 'modern contrast screening candidate B; traceable decay may be absent; human judgement pending',
};

export async function buildSpaceFollowupPack(root, output) {
  return buildPack(root, output, {
    selection: spaceFollowupSelection,
    selectionReasons,
    packId: 'hypha-space-followup-20260907-01',
    answerProtocol: 'development-space-followup-v1',
    reviewWindow: ({ frames }) => [0, frames],
    readme: 'Hypha SPACE 追加判定パック 01\n\n'
      + 'index.html をChromeで開いてください。ネット接続は不要です。\n'
      + '現代の制作を中心にしたSPACE候補6件です。各30秒全体を確認できます。\n'
      + '候補は3つの検証層を各2件にするための仮説選定で、残響や減衰の有無は未確定です。\n'
      + '減衰を実際に追える範囲だけを指定し、空間効果が聞こえても追えなければ「該当なし」にしてください。\n'
      + '途中でも「回答JSONを保存」で書き出し、そのJSONをCodexへ添付してください。\n'
      + 'audioフォルダはindex.htmlと一緒に保持してください。音源は私用の検証用で、再配布しないでください。\n'
      + 'これは開発用の追加判定です。製品精度や最終Coverageの合格を意味しません。\n',
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  if (process.argv.length !== 4) {
    throw new Error('Usage: build_space_followup_pack.mjs RESEARCH_ROOT NEW_OUTPUT_DIRECTORY');
  }
  process.umask(0o077);
  console.log(JSON.stringify(await buildSpaceFollowupPack(
    path.resolve(process.argv[2]), path.resolve(process.argv[3])), null, 2));
}
