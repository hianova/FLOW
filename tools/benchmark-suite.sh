#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repetitions=${1:-1000}
build_dir="$root/build/benchmarks"
generated_dir="$root/generated/benchmarks"
mkdir -p "$build_dir" "$generated_dir"

specs="bounded_queue shared_cache parallel_map rank binary_parser state_machine"
for spec in $specs; do
    "$root/build/flowc" "$root/examples/$spec.flow" -o "$generated_dir/$spec.c" --search --iterations 100 --seed 42 >/dev/null
    cc -std=c17 -O3 -Wall -Wextra -Wpedantic -pthread -DFLOW_BENCHMARK -Dmain=flow_program_main -c "$generated_dir/$spec.c" -o "$build_dir/flow-$spec.o"
    cc -std=c17 -O3 -Wall -Wextra -Wpedantic -pthread "$root/benchmarks/repeat_main.c" "$build_dir/flow-$spec.o" -o "$build_dir/flow-$spec"
    cc -std=c17 -O3 -Wall -Wextra -Wpedantic -DFLOW_BENCHMARK -Dmain=flow_program_main -c "$root/benchmarks/baselines/c/$spec.c" -o "$build_dir/c-$spec.o"
    cc -std=c17 -O3 -Wall -Wextra -Wpedantic "$root/benchmarks/repeat_main.c" "$build_dir/c-$spec.o" -o "$build_dir/c-$spec"
done

measure_runtime() {
    BENCH_EXE=$1 FLOW_BENCH_REPS="$repetitions" /usr/bin/time -l sh -c '"$BENCH_EXE" >/dev/null' 2>&1
}

compile_time() {
    /usr/bin/time -p "$@" 2>&1 | awk '$1 == "real" { print $2 }'
}

printf 'case,flow_spec_loc,generated_c_loc,c_impl_loc,human_decisions,flow_compile_s,c_compile_s,flow_real_s,c_real_s,flow_rss_bytes,c_rss_bytes,runtime_checks,inferred_facts\n'
for spec in $specs; do
    flow_spec_loc=$(wc -l < "$root/examples/$spec.flow" | tr -d ' ')
    generated_c_loc=$(wc -l < "$generated_dir/$spec.c" | tr -d ' ')
    c_impl_loc=$(wc -l < "$root/benchmarks/baselines/c/$spec.c" | tr -d ' ')
    decisions=$(sed -n '1s/.*Human decisions: \([0-9][0-9]*\).*/\1/p' \
        "$root/benchmarks/baselines/c/$spec.c")
    test -n "$decisions"
    flow_compile=$(compile_time cc -std=c17 -O3 -Wall -Wextra -Wpedantic -pthread -DFLOW_BENCHMARK "$generated_dir/$spec.c" -o "$build_dir/flow-compile-$spec")
    c_compile=$(compile_time cc -std=c17 -O3 -Wall -Wextra -Wpedantic -DFLOW_BENCHMARK "$root/benchmarks/baselines/c/$spec.c" -o "$build_dir/c-compile-$spec")
    flow_stats=$(measure_runtime "$build_dir/flow-$spec")
    c_stats=$(measure_runtime "$build_dir/c-$spec")
    flow_time=$(printf '%s\n' "$flow_stats" | awk '$2 == "real" { print $1; exit }')
    c_time=$(printf '%s\n' "$c_stats" | awk '$2 == "real" { print $1; exit }')
    flow_rss=$(printf '%s\n' "$flow_stats" | awk '/maximum resident set size/ { print $1; exit }')
    c_rss=$(printf '%s\n' "$c_stats" | awk '/maximum resident set size/ { print $1; exit }')
    runtime_checks=$(grep -c 'Verification: status=runtime_check' "$generated_dir/$spec.c" || true)
    inferred_facts=$(sed -n 's#.*Semantic IR: ##p' "$generated_dir/$spec.c" | awk '{ for (i = 1; i <= NF; ++i) if ($i ~ /^inferred_facts=/) { sub(/^inferred_facts=/, "", $i); print $i; exit } }')
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$spec" "$flow_spec_loc" "$generated_c_loc" "$c_impl_loc" "$decisions" \
        "$flow_compile" "$c_compile" "$flow_time" "$c_time" \
        "$flow_rss" "$c_rss" "$runtime_checks" "$inferred_facts"
done
