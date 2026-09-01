// FLOW benchmark baseline. Human decisions: 5.
use std::{collections::VecDeque, env};
fn main() { let reps = env::var("FLOW_BENCH_REPS").ok().and_then(|v| v.parse().ok()).unwrap_or(1); for _ in 0..reps { let mut q = VecDeque::with_capacity(1024); for i in 0..5 { q.push_back(i); } while q.pop_front().is_some() {} } }
