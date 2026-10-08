// Hypha opt-in extension to MIT-licensed ebur128 0.1.10.
// Preserve the canonical, unquantized gating energies. An AVL order-statistics tree avoids
// rescanning/sorting a growing song at every readout. Only the measurement thread uses it.
use super::{EbuR128, Error, Mode};
use crate::utils::energy_to_loudness;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};

type Link = Option<Box<Node>>;

struct Node {
    energy: f64,
    copies: u64,
    count: u64,
    sum: f64,
    height: u32,
    left: Link,
    right: Link,
}

fn count(node: &Link) -> u64 {
    node.as_ref().map_or(0, |node| node.count)
}
fn sum(node: &Link) -> f64 {
    node.as_ref().map_or(0.0, |node| node.sum)
}
fn height(node: &Link) -> u32 {
    node.as_ref().map_or(0, |node| node.height)
}

impl Node {
    fn refresh(&mut self) {
        self.count = count(&self.left) + self.copies + count(&self.right);
        self.sum = sum(&self.left) + self.energy * self.copies as f64 + sum(&self.right);
        self.height = 1 + height(&self.left).max(height(&self.right));
    }

    fn rotate_left(mut root: Box<Self>) -> Box<Self> {
        let mut next = root.right.take().unwrap();
        root.right = next.left.take();
        root.refresh();
        next.left = Some(root);
        next.refresh();
        next
    }

    fn rotate_right(mut root: Box<Self>) -> Box<Self> {
        let mut next = root.left.take().unwrap();
        root.left = next.right.take();
        root.refresh();
        next.right = Some(root);
        next.refresh();
        next
    }

    fn insert(root: Link, energy: f64) -> Box<Self> {
        let Some(mut root) = root else {
            return Box::new(Self {
                energy,
                copies: 1,
                count: 1,
                sum: energy,
                height: 1,
                left: None,
                right: None,
            });
        };
        if energy < root.energy {
            root.left = Some(Self::insert(root.left.take(), energy));
        } else if energy > root.energy {
            root.right = Some(Self::insert(root.right.take(), energy));
        } else {
            root.copies += 1;
        }
        root.refresh();
        if height(&root.left) > height(&root.right) + 1 {
            let left = root.left.as_ref().unwrap();
            if height(&left.right) > height(&left.left) {
                root.left = Some(Self::rotate_left(root.left.take().unwrap()));
            }
            root = Self::rotate_right(root);
        } else if height(&root.right) > height(&root.left) + 1 {
            let right = root.right.as_ref().unwrap();
            if height(&right.left) > height(&right.right) {
                root.right = Some(Self::rotate_right(root.right.take().unwrap()));
            }
            root = Self::rotate_left(root);
        }
        root
    }
}

#[derive(Default)]
pub(super) struct Energies {
    root: Link,
    invalid: bool,
    // Canonical I's ungated queue sum is in insertion order. Keep exactly that order so an
    // energy on its relative gate does not move to the other side through reassociation.
    input_sum: f64,
    range_ready: AtomicBool,
    range_bits: AtomicU64,
}

impl Energies {
    pub(super) fn add(&mut self, energy: f64) {
        // Same absolute gate as History::add; do not quantize into HISTOGRAM bins.
        if energy < crate::histogram_bins::BOUNDARIES[0] {
            return;
        }
        *self.range_ready.get_mut() = false;
        if energy.is_nan() {
            self.invalid = true;
            return;
        }
        self.input_sum += energy;
        self.root = Some(Node::insert(self.root.take(), energy));
    }

    // Query a suffix in one root-to-leaf walk. Whole subtrees supply count and raw energy sum.
    fn above(&self, threshold: f64) -> (u64, f64) {
        let mut node = self.root.as_deref();
        let (mut n, mut energy) = (0, 0.0);
        while let Some(current) = node {
            if current.energy >= threshold {
                n += current.copies + count(&current.right);
                energy += current.energy * current.copies as f64 + sum(&current.right);
                node = current.left.as_deref();
            } else {
                node = current.right.as_deref();
            }
        }
        (n, energy)
    }

    fn rank(&self, mut index: u64) -> f64 {
        let mut node = self.root.as_deref().unwrap();
        loop {
            let before = count(&node.left);
            if index < before {
                node = node.left.as_deref().unwrap();
            } else if index < before + node.copies {
                return node.energy;
            } else {
                index -= before + node.copies;
                node = node.right.as_deref().unwrap();
            }
        }
    }

    fn integrated(&self) -> f64 {
        if self.invalid {
            return f64::NAN;
        }
        if count(&self.root) == 0 {
            return -f64::INFINITY;
        }
        let threshold = self.input_sum / count(&self.root) as f64 * 0.1;
        let (n, energy) = self.above(threshold);
        if n == 0 {
            -f64::INFINITY
        } else {
            energy_to_loudness(energy / n as f64)
        }
    }

    /// Two differently associated sums can put a raw energy on opposite sides of the LRA
    /// relative gate. Bound both positive sums conservatively, then check whether the enclosed
    /// interval contains any energy in O(log N). Such a case must use the canonical sorted sum.
    fn ambiguous_range_gate(&self, threshold: f64, total: u64) -> bool {
        let gamma = |operations: f64| {
            // EPSILON is twice IEEE's unit roundoff, providing margin over gamma_k's usual u.
            let ku = operations * f64::EPSILON;
            (ku < 0.25).then(|| ku / (1.0 - ku))
        };
        let n = total as f64;
        let Some(canonical) = gamma(n + 4.0) else {
            return true;
        };
        let Some(tree) = gamma(3.0 * n + 6.0) else {
            return true;
        };
        if !threshold.is_finite() || threshold <= 0.0 {
            return true;
        }
        // Canonical: <=N sequential adds. Tree: <=N products and <=2N adds. Include
        // the shared division/multiplication, bound conversion and interval construction.
        let relative = (canonical + tree) / (1.0 - tree) + 8.0 * f64::EPSILON;
        let error = threshold * relative;
        // Outward rounding by one representable positive value encloses arithmetic roundoff.
        let lower = f64::from_bits((threshold - error).max(0.0).to_bits().saturating_sub(1));
        let upper = f64::from_bits((threshold + error).to_bits().saturating_add(1));
        self.above(lower).0 != self.above(upper).0
    }

    fn range_fast(&self) -> Option<f64> {
        if self.invalid {
            return Some(f64::NAN);
        }
        let total = count(&self.root);
        if total == 0 {
            return Some(0.0);
        }
        let threshold = sum(&self.root) / total as f64 * f64::powf(10.0, -20.0 / 10.0);
        if self.ambiguous_range_gate(threshold, total) {
            return None;
        }
        let (n, _) = self.above(threshold);
        if n == 0 {
            return Some(0.0);
        }
        let base = total - n;
        let index = |q| base + ((n - 1) as f64 * q + 0.5) as u64;
        Some(energy_to_loudness(self.rank(index(0.95))) - energy_to_loudness(self.rank(index(0.1))))
    }

    fn range_with_canonical(&self, canonical: impl FnOnce() -> f64) -> f64 {
        if self.range_ready.load(Ordering::Acquire) {
            return f64::from_bits(self.range_bits.load(Ordering::Relaxed));
        }
        let value = self.range_fast().unwrap_or_else(canonical);
        // EbuR128 mutation requires &mut self, so concurrent immutable queries can only store
        // the same history's result. Atomics preserve the original analyzer's Send/Sync traits.
        self.range_bits.store(value.to_bits(), Ordering::Relaxed);
        self.range_ready.store(true, Ordering::Release);
        value
    }
}

#[derive(Default)]
pub(super) struct SummaryCache {
    pub(super) integrated: Energies,
    pub(super) range: Energies,
}

impl EbuR128 {
    /// Enable exact-energy I/LRA readouts before the first input, with unlimited history only.
    /// The canonical scalar APIs remain unchanged and are the numerical oracle. Cached queries
    /// use logarithmic tree walks; an ambiguous LRA gate uses the unchanged canonical query.
    pub fn enable_cached_summary_queries(&mut self) -> Result<(), Error> {
        if self.mode.contains(Mode::HISTOGRAM) || self.history != usize::MAX {
            return Err(Error::InvalidMode);
        }
        if self.audio_data_index != 0 || self.needed_frames != self.samples_in_100ms * 4 {
            return Err(Error::InvalidMode);
        }
        if !self.block_energy_history.is_empty() || !self.short_term_block_energy_history.is_empty()
        {
            return Err(Error::InvalidMode);
        }
        if self.summary_cache.is_none() {
            self.summary_cache = Some(SummaryCache::default());
        }
        Ok(())
    }

    pub fn loudness_global_cached(&self) -> Result<f64, Error> {
        if !self.mode.contains(Mode::I) {
            return Err(Error::InvalidMode);
        }
        self.summary_cache.as_ref().map_or_else(
            || self.loudness_global(),
            |cache| Ok(cache.integrated.integrated()),
        )
    }

    pub fn loudness_range_cached(&self) -> Result<f64, Error> {
        if !self.mode.contains(Mode::LRA) {
            return Err(Error::InvalidMode);
        }
        self.summary_cache.as_ref().map_or_else(
            || self.loudness_range(),
            |cache| {
                Ok(cache
                    .range
                    .range_with_canonical(|| self.short_term_block_energy_history.loudness_range()))
            },
        )
    }
}

#[cfg(test)]
#[path = "cached_summary_tests.rs"]
mod tests;
