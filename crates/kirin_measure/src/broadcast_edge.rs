//! Bounded memory for one-shot producer notifications.
//!
//! All Keep and All Stop files are durable pointers to the latest immutable generation edge.
//! Elapsed wall time is never permission to replay an unchanged edge. One key per originator is
//! sufficient because that originator's next generation replaces its current file and key.

use std::collections::HashMap;

#[derive(Debug, Default)]
pub(crate) struct BroadcastEdgeMemory {
    last_key_by_originator: HashMap<String, String>,
}

impl BroadcastEdgeMemory {
    pub(crate) fn contains(&self, originator: &str, edge_key: &str) -> bool {
        self.last_key_by_originator
            .get(originator)
            .is_some_and(|processed| processed == edge_key)
    }

    pub(crate) fn remember(&mut self, originator: &str, edge_key: &str) {
        self.last_key_by_originator
            .insert(originator.to_string(), edge_key.to_string());
    }

    #[cfg(test)]
    fn len(&self) -> usize {
        self.last_key_by_originator.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn completed_edge_never_expires_or_replays() {
        let mut memory = BroadcastEdgeMemory::default();
        memory.remember("post-a", "generation-1");

        // There is deliberately no time/expiry input: process uptime and the old 30-second cache
        // boundary cannot turn the same generation into a second start event.
        assert!(memory.contains("post-a", "generation-1"));
        assert!(memory.contains("post-a", "generation-1"));
        assert_eq!(memory.len(), 1);
    }

    #[test]
    fn next_generation_replaces_the_same_bounded_originator_slot() {
        let mut memory = BroadcastEdgeMemory::default();
        memory.remember("post-a", "generation-1");
        memory.remember("post-a", "generation-2");

        assert!(!memory.contains("post-a", "generation-1"));
        assert!(memory.contains("post-a", "generation-2"));
        assert_eq!(memory.len(), 1);
    }
}
