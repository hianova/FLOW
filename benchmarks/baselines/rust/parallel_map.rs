// FLOW benchmark baseline. Human decisions: 6.
use std::env;
fn main() { let reps = env::var("FLOW_BENCH_REPS").ok().and_then(|v| v.parse().ok()).unwrap_or(1); for _ in 0..reps { let input = [91, 74, 99, 86, 95]; let output: Vec<_> = input.into_iter().map(|v| v * 2).collect(); std::hint::black_box(output); } }
