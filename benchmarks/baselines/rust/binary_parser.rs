// FLOW benchmark baseline. Human decisions: 6.
use std::env;
fn main() { let reps = env::var("FLOW_BENCH_REPS").ok().and_then(|v| v.parse().ok()).unwrap_or(1); for _ in 0..reps { let p = [0xF1u8, 0x02, 0x07, 0x2A]; assert!(p.len() >= 4 && p[0] == 0xF1 && p[1] as usize + 2 <= p.len()); std::hint::black_box((p[2], p[3])); } }
