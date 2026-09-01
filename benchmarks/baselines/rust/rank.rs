// FLOW benchmark baseline. Human decisions: 7.
use std::env;
fn main() { let reps = env::var("FLOW_BENCH_REPS").ok().and_then(|v| v.parse().ok()).unwrap_or(1); for _ in 0..reps { let mut a = [(1,91),(2,74),(3,99),(4,86),(5,95)]; a.sort_by(|x,y| y.1.cmp(&x.1)); std::hint::black_box(a); } }
