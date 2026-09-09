import { createHash } from 'node:crypto';

export const evidenceSchema = 'hypha.review.evaluation-evidence.v1';

export function evaluationEvidenceIdentitySha(evidence) {
  return createHash('sha256').update(JSON.stringify({
    schema: evidence.schema,
    pack_id: evidence.pack_id,
    items: evidence.items,
  })).digest('hex');
}
