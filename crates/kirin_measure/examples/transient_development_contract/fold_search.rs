use super::*;
use sha2::{Digest, Sha256};

pub(super) fn search_restart(
    items: &[Item],
    category_totals: &[u16],
    restart: u8,
) -> (Score, Vec<u8>, Vec<(String, u8)>) {
    let mut rng = DeterministicRng::new(restart);
    let assignment = initial_assignment(items, &mut rng);
    let mut state = SearchState::new(items, assignment, category_totals);
    improve_randomly(&mut state, &mut rng);
    improve_best_swaps(&mut state);
    canonicalize(items, &mut state.assignment);
    let state = SearchState::new(items, state.assignment, category_totals);
    (
        state.score(),
        state.assignment.clone(),
        signature(items, &state.assignment),
    )
}

pub(super) fn state_for<'a>(
    items: &'a [Item],
    assignment: Vec<u8>,
    category_totals: &'a [u16],
) -> SearchState<'a> {
    SearchState::new(items, assignment, category_totals)
}

fn initial_assignment(items: &[Item], rng: &mut DeterministicRng) -> Vec<u8> {
    let capacity = items.len() / usize::from(FOLD_COUNT);
    let mut beat = indices_by_type(items, true);
    let mut fill = indices_by_type(items, false);
    rng.shuffle(&mut beat);
    rng.shuffle(&mut fill);
    let beat_floor = beat.len() / usize::from(FOLD_COUNT);
    let mut targets = [beat_floor; FOLD_COUNT as usize];
    let mut fold_order = (0..FOLD_COUNT).collect::<Vec<_>>();
    rng.shuffle(&mut fold_order);
    for fold in fold_order
        .into_iter()
        .take(beat.len() % usize::from(FOLD_COUNT))
    {
        targets[usize::from(fold)] += 1;
    }
    let mut assignment = vec![u8::MAX; items.len()];
    let (mut beat_offset, mut fill_offset) = (0, 0);
    for fold in 0..FOLD_COUNT {
        let beat_count = targets[usize::from(fold)];
        let fill_count = capacity - beat_count;
        for index in &beat[beat_offset..beat_offset + beat_count] {
            assignment[*index] = fold;
        }
        for index in &fill[fill_offset..fill_offset + fill_count] {
            assignment[*index] = fold;
        }
        beat_offset += beat_count;
        fill_offset += fill_count;
    }
    assignment
}

fn indices_by_type(items: &[Item], beat: bool) -> Vec<usize> {
    items
        .iter()
        .enumerate()
        .filter_map(|(index, item)| (item.beat == beat).then_some(index))
        .collect()
}

impl<'a> SearchState<'a> {
    fn new(items: &'a [Item], assignment: Vec<u8>, category_totals: &'a [u16]) -> Self {
        let totals =
            std::array::from_fn(|metric| items.iter().map(|item| item.metrics[metric]).sum());
        let category_cost = category_totals
            .iter()
            .map(|total| u128::from(*total).pow(2) * u128::from(FOLD_COUNT))
            .sum();
        let mut state = Self {
            items,
            assignment,
            folds: (0..FOLD_COUNT)
                .map(|_| FoldState::new(category_totals.len()))
                .collect(),
            category_totals,
            category_cost,
            totals,
        };
        for (index, fold) in state.assignment.clone().into_iter().enumerate() {
            state.add(index, fold);
        }
        state
    }

    fn add(&mut self, index: usize, fold: u8) {
        let item = &self.items[index];
        let fold_index = usize::from(fold);
        self.folds[fold_index].ids += 1;
        self.folds[fold_index].beat_ids += u64::from(item.beat);
        self.folds[fold_index].validation_ids += u64::from(item.validation);
        self.folds[fold_index].forced_opened_validation_ids +=
            u64::from(item.forced_opened_validation);
        for metric in 0..4 {
            self.folds[fold_index].metrics[metric] += item.metrics[metric];
            *self.folds[fold_index].metric_values[metric]
                .entry(item.metrics[metric])
                .or_default() += 1;
        }
        self.folds[fold_index].positive_ids[0] += u64::from(item.metrics[2] > 0);
        self.folds[fold_index].positive_ids[1] += u64::from(item.metrics[3] > 0);
        for category in item.categories {
            self.update_category(fold_index, category, 1);
        }
    }

    fn remove(&mut self, index: usize, fold: u8) {
        let item = &self.items[index];
        let fold_index = usize::from(fold);
        self.folds[fold_index].ids -= 1;
        self.folds[fold_index].beat_ids -= u64::from(item.beat);
        self.folds[fold_index].validation_ids -= u64::from(item.validation);
        self.folds[fold_index].forced_opened_validation_ids -=
            u64::from(item.forced_opened_validation);
        for metric in 0..4 {
            self.folds[fold_index].metrics[metric] -= item.metrics[metric];
            let count = self.folds[fold_index].metric_values[metric]
                .get_mut(&item.metrics[metric])
                .expect("metric value exists");
            *count -= 1;
            if *count == 0 {
                self.folds[fold_index].metric_values[metric].remove(&item.metrics[metric]);
            }
        }
        self.folds[fold_index].positive_ids[0] -= u64::from(item.metrics[2] > 0);
        self.folds[fold_index].positive_ids[1] -= u64::from(item.metrics[3] > 0);
        for category in item.categories {
            self.update_category(fold_index, category, -1);
        }
    }

    fn update_category(&mut self, fold: usize, category: usize, delta: i8) {
        let total = u64::from(self.category_totals[category]);
        let old = u64::from(self.folds[fold].categories[category]);
        let old_cost = u128::from((old * u64::from(FOLD_COUNT)).abs_diff(total)).pow(2);
        let new = if delta > 0 { old + 1 } else { old - 1 };
        let new_cost = u128::from((new * u64::from(FOLD_COUNT)).abs_diff(total)).pow(2);
        self.category_cost = self.category_cost + new_cost - old_cost;
        self.folds[fold].categories[category] = new as u16;
    }

    fn swap(&mut self, left: usize, right: usize) {
        let left_fold = self.assignment[left];
        let right_fold = self.assignment[right];
        self.remove(left, left_fold);
        self.remove(right, right_fold);
        self.add(left, right_fold);
        self.add(right, left_fold);
        self.assignment.swap(left, right);
    }

    fn count_constraints_hold(&self) -> bool {
        spread(self.folds.iter().map(|fold| fold.ids)) == 0
            && spread(self.folds.iter().map(|fold| fold.beat_ids)) <= 1
    }

    fn score(&self) -> Score {
        let (hard_excess, failed_gates) = hard_score(&self.folds);
        let numeric = self
            .folds
            .iter()
            .flat_map(|fold| (0..4).map(|metric| (fold.metrics[metric], self.totals[metric])))
            .map(|(value, total)| normalized_square(value * u64::from(FOLD_COUNT), total))
            .sum::<u128>();
        Score {
            hard_excess,
            failed_gates,
            diagnostic_isolation_cost: isolation_cost(&self.folds),
            balance_cost: numeric + self.category_cost * 10_000,
        }
    }
}

fn isolation_cost(folds: &[FoldState]) -> u128 {
    let forced = spread(folds.iter().map(|fold| fold.forced_opened_validation_ids));
    let validation = spread(folds.iter().map(|fold| fold.validation_ids));
    u128::from(forced) * 1_000 + u128::from(validation)
}

fn improve_randomly(state: &mut SearchState<'_>, rng: &mut DeterministicRng) {
    let mut score = state.score();
    for _ in 0..RANDOM_SWAP_ATTEMPTS {
        let left = rng.index(state.items.len());
        let right = rng.index(state.items.len());
        if left == right || state.assignment[left] == state.assignment[right] {
            continue;
        }
        state.swap(left, right);
        let candidate = state.count_constraints_hold().then(|| state.score());
        if candidate.is_some_and(|value| value < score) {
            score = candidate.expect("candidate exists");
        } else {
            state.swap(left, right);
        }
    }
}

fn improve_best_swaps(state: &mut SearchState<'_>) {
    let mut current = state.score();
    for _ in 0..BEST_SWAP_PASSES {
        let mut best = None;
        for left in 0..state.items.len() - 1 {
            for right in left + 1..state.items.len() {
                if state.assignment[left] == state.assignment[right] {
                    continue;
                }
                state.swap(left, right);
                let candidate = state.count_constraints_hold().then(|| state.score());
                state.swap(left, right);
                if candidate.is_some_and(|value| value < current)
                    && best.is_none_or(|(_, _, value)| candidate.expect("candidate") < value)
                {
                    best = Some((left, right, candidate.expect("candidate")));
                }
            }
        }
        let Some((left, right, score)) = best else {
            break;
        };
        state.swap(left, right);
        current = score;
    }
}

fn hard_score(folds: &[FoldState]) -> (u128, u32) {
    let (mut excess, mut failures) = (0_u128, 0_u32);
    add_spread_gate(
        &mut excess,
        &mut failures,
        folds.iter().map(|fold| fold.ids),
        0,
    );
    add_spread_gate(
        &mut excess,
        &mut failures,
        folds.iter().map(|fold| fold.beat_ids),
        1,
    );
    add_spread_gate(
        &mut excess,
        &mut failures,
        folds.iter().map(|fold| fold.ids - fold.beat_ids),
        1,
    );
    for (metric, limit) in RATIO_LIMITS.into_iter().enumerate() {
        let minimum = folds
            .iter()
            .map(|fold| fold.metrics[metric])
            .min()
            .unwrap_or(0);
        let maximum = folds
            .iter()
            .map(|fold| fold.metrics[metric])
            .max()
            .unwrap_or(0);
        add_ratio_gate(&mut excess, &mut failures, minimum, maximum, limit);
    }
    for fold in folds {
        add_minimum_gate(
            &mut excess,
            &mut failures,
            fold.metrics[2],
            MIN_KICK_EVENTS_PER_FOLD,
        );
        add_minimum_gate(
            &mut excess,
            &mut failures,
            fold.metrics[3],
            MIN_HAT_EVENTS_PER_FOLD,
        );
        for positive in fold.positive_ids {
            add_minimum_gate(
                &mut excess,
                &mut failures,
                positive,
                MIN_POSITIVE_IDS_PER_FOLD,
            );
        }
        for metric in 0..4 {
            add_ratio_gate(
                &mut excess,
                &mut failures,
                fold.metrics[metric],
                fold.maximum(metric),
                RatioLimit::new(1, 4),
            );
        }
    }
    (excess, failures)
}

fn add_spread_gate(
    excess: &mut u128,
    failures: &mut u32,
    values: impl Iterator<Item = u64>,
    limit: u64,
) {
    let amount = spread(values).saturating_sub(limit);
    if amount > 0 {
        *failures += 1;
        *excess += u128::from(amount * 1_000_000).pow(2);
    }
}

fn add_ratio_gate(
    excess: &mut u128,
    failures: &mut u32,
    minimum: u64,
    maximum: u64,
    limit: RatioLimit,
) {
    let left = u128::from(maximum) * u128::from(limit.denominator);
    let right = u128::from(minimum) * u128::from(limit.numerator);
    if minimum == 0 || left > right {
        *failures += 1;
        let ppm = ((left - right) * 1_000_000)
            .checked_div(right)
            .unwrap_or(10_000_000);
        *excess += ppm.pow(2);
    }
}

fn add_minimum_gate(excess: &mut u128, failures: &mut u32, actual: u64, required: u64) {
    if actual < required {
        *failures += 1;
        let ppm = u128::from(required - actual) * 1_000_000 / u128::from(required);
        *excess += ppm.pow(2);
    }
}

fn canonicalize(items: &[Item], assignment: &mut [u8]) {
    let mut members = (0..FOLD_COUNT)
        .map(|fold| {
            let ids = items
                .iter()
                .zip(assignment.iter())
                .filter(|(_, value)| **value == fold)
                .map(|(item, _)| item.id.as_str())
                .collect::<Vec<_>>();
            (ids, fold)
        })
        .collect::<Vec<_>>();
    members.sort();
    let mut remap = [0; FOLD_COUNT as usize];
    for (new, (_, old)) in members.into_iter().enumerate() {
        remap[usize::from(old)] = new as u8;
    }
    assignment
        .iter_mut()
        .for_each(|fold| *fold = remap[usize::from(*fold)]);
}

fn signature(items: &[Item], assignment: &[u8]) -> Vec<(String, u8)> {
    let mut value = items
        .iter()
        .zip(assignment)
        .map(|(item, fold)| (item.id.clone(), *fold))
        .collect::<Vec<_>>();
    value.sort();
    value
}

struct DeterministicRng(u64);

impl DeterministicRng {
    fn new(restart: u8) -> Self {
        let mut hash = Sha256::new();
        hash.update(FOLD_ASSIGNMENT_VERSION);
        hash.update(FOLD_ASSIGNMENT_SEED);
        hash.update([restart]);
        let seed = u64::from_be_bytes(hash.finalize()[..8].try_into().expect("eight bytes"));
        Self(seed.max(1))
    }

    fn next(&mut self) -> u64 {
        self.0 ^= self.0 >> 12;
        self.0 ^= self.0 << 25;
        self.0 ^= self.0 >> 27;
        self.0 = self.0.wrapping_mul(0x2545_f491_4f6c_dd1d);
        self.0
    }

    fn index(&mut self, length: usize) -> usize {
        ((u128::from(self.next()) * length as u128) >> 64) as usize
    }

    fn shuffle<T>(&mut self, values: &mut [T]) {
        for end in (1..values.len()).rev() {
            let index = self.index(end + 1);
            values.swap(index, end);
        }
    }
}
