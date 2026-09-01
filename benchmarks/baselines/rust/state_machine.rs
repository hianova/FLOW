// FLOW benchmark baseline. Human decisions: 5.
#[derive(Clone, Copy)] enum State { Idle, Running, Done }
use std::env;
fn main() { let reps = env::var("FLOW_BENCH_REPS").ok().and_then(|v| v.parse().ok()).unwrap_or(1); for _ in 0..reps { let mut s = State::Idle; for e in [1,1,2,2] { s = match (s,e) { (State::Idle,1) => State::Running, (State::Running,2) => State::Done, _ => s }; } std::hint::black_box(s); } }
