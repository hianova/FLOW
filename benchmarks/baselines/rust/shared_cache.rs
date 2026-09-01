// FLOW benchmark baseline. Human decisions: 7.
use std::{collections::HashMap, env};
fn main() { let reps = env::var("FLOW_BENCH_REPS").ok().and_then(|v| v.parse().ok()).unwrap_or(1); for _ in 0..reps { let mut c = HashMap::new(); for i in 0..5 { c.insert(i, 90 + i); } let _: i32 = (0..5).filter_map(|i| c.get(&i)).sum(); } }
