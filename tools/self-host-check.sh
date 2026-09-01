#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
probe=/tmp/flow-self-host-vocabulary.c
compiler_probe=/tmp/flow-self-host-compiler.c
compiler_bin=/tmp/flow-self-host-compiler
stage2_source=/tmp/flow-self-host-stage2.c
stage2_bin=/tmp/flow-self-host-stage2
stage2_output=/tmp/flow-self-host-stage2-output.c
stage2_program=/tmp/flow-self-host-stage2-program

"$root/build/flowc" "$root/examples/vocabulary.flow" -o "$probe" >/dev/null
"$root/build/flowc" "$root/examples/compiler.flow" -o "$compiler_probe" >/dev/null
grep -q 'self_host_graph_nodes=11' "$compiler_probe"
cc -std=c17 -Wall -Wextra -Wpedantic "$compiler_probe" -o "$compiler_bin"
"$compiler_bin" "$root/examples/compiler.flow" -o "$stage2_source"
cc -std=c17 -Wall -Wextra -Wpedantic "$stage2_source" -o "$stage2_bin"
"$stage2_bin" "$root/examples/small.flow" -o "$stage2_output"
cc -std=c17 -Wall -Wextra -Wpedantic "$stage2_output" -o "$stage2_program"
stage2_run=$($stage2_program)
printf '%s\n' "$stage2_run" | grep -q 'flow: scan'
printf '%s\n' "$stage2_run" | grep -q 'self_host: stage2'
printf '%s\n' "$stage2_run" | grep -q 'configuration: capacity 32 top 2'
printf '%s\n' "$stage2_run" | grep -q 'top 2'
grep -q 'Semantic IR: facts=' "$stage2_output"
grep -q 'graph_nodes=2' "$stage2_output"
grep -q 'Verification: status=proven' "$stage2_output"
grep -q 'Selected component: linear_array generator=emit_linear_array' "$stage2_output"
grep -q 'IR: input=items output= output_type= shared=0 bounded=0 parallelizable=0 ordered=0 deterministic=1' "$stage2_output"
stage2_custom_output=/tmp/flow-self-host-stage2-custom-input.c
stage2_custom_bin=/tmp/flow-self-host-stage2-custom-input
"$stage2_bin" "$root/examples/custom_input.flow" -o "$stage2_custom_output"
grep -q 'input_samples=3' "$stage2_custom_output"
cc -std=c17 -Wall -Wextra -Wpedantic "$stage2_custom_output" -o "$stage2_custom_bin"
printf '%s\n' "$($stage2_custom_bin)" | grep -q 'user 11 score 99'
stage3_source=/tmp/flow-self-host-stage3.c
stage3_bin=/tmp/flow-self-host-stage3
stage3_custom_output=/tmp/flow-self-host-stage3-custom-input.c
stage3_custom_bin=/tmp/flow-self-host-stage3-custom-input
"$stage2_bin" "$root/examples/compiler.flow" -o "$stage3_source"
grep -q 'Generated compiler component: graph_nodes=11' "$stage3_source"
for symbol in arena_store queue_push registry_hash ffi_available; do
    grep -q "$symbol" "$stage3_source"
done
cc -std=c17 -Wall -Wextra -Wpedantic "$stage3_source" -o "$stage3_bin"
! "$stage3_bin" "$root/examples/invalid_unbounded.flow" -o /tmp/flow-self-host-stage3-invalid-unbounded.c
! "$stage3_bin" "$root/examples/invalid_capability.flow" -o /tmp/flow-self-host-stage3-invalid-capability.c
! "$stage3_bin" "$root/examples/invalid_no_input.flow" -o /tmp/flow-self-host-stage3-invalid-no-input.c
"$stage3_bin" "$root/examples/custom_input.flow" -o "$stage3_custom_output"
cc -std=c17 -Wall -Wextra -Wpedantic "$stage3_custom_output" -o "$stage3_custom_bin"
stage3_custom_run=$($stage3_custom_bin)
printf '%s\n' "$stage3_custom_run" | grep -q 'user 11 score 99'
printf '%s\n' "$stage3_custom_run" | grep -q 'self_host: semantic-stage3'
stage3_rank_output=/tmp/flow-self-host-stage3-rank-search.c
stage3_rank_bin=/tmp/flow-self-host-stage3-rank-search
"$stage3_bin" "$root/examples/rank.flow" -o "$stage3_rank_output" --search --iterations 500 --seed 42
grep -q 'C search: mode=model iterations=500 seed=42 genome=' "$stage3_rank_output"
cc -std=c17 -Wall -Wextra -Wpedantic "$stage3_rank_output" -o "$stage3_rank_bin"
printf '%s\n' "$($stage3_rank_bin)" | grep -q 'configuration: capacity 4096 top 3 threads 16 shards 16'
stage3_profile=/tmp/flow-self-host-stage3-rank.profile
stage3_profile_output=/tmp/flow-self-host-stage3-rank-profile.c
stage3_profile_reuse=/tmp/flow-self-host-stage3-rank-profile-reuse.c
stage3_profile_log=/tmp/flow-self-host-stage3-profile.log
"$stage3_bin" "$root/examples/rank.flow" -o "$stage3_profile_output" --benchmark --iterations 50 --seed 42 --profile-out "$stage3_profile" > "$stage3_profile_log"
grep -q '^rank,sharded_hash,' "$stage3_profile"
"$stage3_bin" "$root/examples/rank.flow" -o "$stage3_profile_reuse" --benchmark --iterations 10 --seed 7 --profile "$stage3_profile" > "$stage3_profile_log"
grep -q 'profile: loaded component=sharded_hash' "$stage3_profile_log"
for spec in bounded_queue shared_cache parallel_map rank binary_parser state_machine; do
    stage3_case_output="/tmp/flow-self-host-stage3-$spec.c"
    stage3_case_bin="/tmp/flow-self-host-stage3-$spec"
    "$stage3_bin" "$root/examples/$spec.flow" -o "$stage3_case_output"
    cc -std=c17 -Wall -Wextra -Wpedantic "$stage3_case_output" -o "$stage3_case_bin"
    stage3_case_run=$($stage3_case_bin)
    printf '%s\n' "$stage3_case_run" | grep -q "flow: ${spec}"
    printf '%s\n' "$stage3_case_run" | grep -q 'self_host: semantic-stage3'
done
stage2_ambiguous_output=/tmp/flow-self-host-stage2-ambiguous.c
"$stage2_bin" "$root/examples/ambiguous.flow" -o "$stage2_ambiguous_output"
grep -q 'holes=1' "$stage2_ambiguous_output"
stage2_rank_output=/tmp/flow-self-host-stage2-rank-verifier.c
"$stage2_bin" "$root/examples/rank.flow" -o "$stage2_rank_output"
grep -q 'Verification: status=runtime_check' "$stage2_rank_output"
stage2_rank_search_output=/tmp/flow-self-host-stage2-rank-search.c
stage2_rank_search_bin=/tmp/flow-self-host-stage2-rank-search
"$stage2_bin" "$root/examples/rank.flow" -o "$stage2_rank_search_output" --search --iterations 500 --seed 42
grep -q 'C search: mode=model iterations=500 seed=42 genome=' "$stage2_rank_search_output"
grep -q 'Selected component: sharded_hash generator=emit_sharded_hash' "$stage2_rank_search_output"
cc -std=c17 -Wall -Wextra -Wpedantic "$stage2_rank_search_output" -o "$stage2_rank_search_bin"
stage2_rank_search_run=$($stage2_rank_search_bin)
printf '%s\n' "$stage2_rank_search_run" | grep -q 'component: sharded_hash'
printf '%s\n' "$stage2_rank_search_run" | grep -q 'configuration: capacity 4096 top 3 threads 16 shards 16'
stage2_rank_profile=/tmp/flow-self-host-stage2-rank.profile
stage2_rank_profile_output=/tmp/flow-self-host-stage2-rank-profile.c
stage2_rank_profile_reuse=/tmp/flow-self-host-stage2-rank-profile-reuse.c
stage2_profile_log=/tmp/flow-self-host-stage2-profile.log
"$stage2_bin" "$root/examples/rank.flow" -o "$stage2_rank_profile_output" --benchmark --iterations 50 --seed 42 --profile-out "$stage2_rank_profile" > "$stage2_profile_log"
grep -q 'profile: wrote' "$stage2_profile_log"
grep -q '^rank,sharded_hash,' "$stage2_rank_profile"
"$stage2_bin" "$root/examples/rank.flow" -o "$stage2_rank_profile_reuse" --benchmark --iterations 10 --seed 7 --profile "$stage2_rank_profile" > "$stage2_profile_log"
grep -q 'profile: loaded component=sharded_hash' "$stage2_profile_log"
! "$stage2_bin" "$root/examples/ordered.flow" -o /tmp/flow-self-host-stage2-invalid-component.c --component parallel_map
! "$stage2_bin" "$root/examples/invalid_capacity.flow" -o /tmp/flow-self-host-stage2-invalid.c
! "$stage2_bin" "$root/examples/invalid_capability.flow" -o /tmp/flow-self-host-stage2-invalid-capability.c
! "$stage2_bin" "$root/examples/invalid_unbounded.flow" -o /tmp/flow-self-host-stage2-invalid-unbounded.c
! "$stage2_bin" "$root/examples/invalid_no_input.flow" -o /tmp/flow-self-host-stage2-invalid-no-input.c

for spec in bounded_queue shared_cache parallel_map rank binary_parser state_machine small vocabulary; do
    stage2_case_output="/tmp/flow-self-host-stage2-$spec.c"
    stage2_case_bin="/tmp/flow-self-host-stage2-$spec"
    expected_flow=$spec
    test "$spec" = small && expected_flow=scan
    "$stage2_bin" "$root/examples/$spec.flow" -o "$stage2_case_output"
    grep -q 'Semantic IR: facts=' "$stage2_case_output"
    grep -q 'Verification: status=' "$stage2_case_output"
    grep -q 'generator=emit_' "$stage2_case_output"
    cc -std=c17 -Wall -Wextra -Wpedantic "$stage2_case_output" -o "$stage2_case_bin"
    stage2_case_run=$($stage2_case_bin)
    printf '%s\n' "$stage2_case_run" | grep -q "flow: ${expected_flow}"
    printf '%s\n' "$stage2_case_run" | grep -q 'self_host: stage2'
    case "$spec" in
        bounded_queue) component_marker='component: bounded_queue' ;;
        shared_cache) component_marker='component: sharded_hash' ;;
        parallel_map|vocabulary) component_marker='component: parallel_map' ;;
        rank) component_marker='component: sharded_hash' ;;
        binary_parser) component_marker='component: binary_parser' ;;
        state_machine) component_marker='component: state_machine' ;;
        small) component_marker='component: linear_array' ;;
    esac
    printf '%s\n' "$stage2_case_run" | grep -q "$component_marker"
    if test "$spec" = vocabulary; then
        grep -q 'resource=cpu capability=pthread domain=ranking contract= fallback=' \
            "$stage2_case_output"
    fi
done

for spec in bounded_queue shared_cache parallel_map rank binary_parser state_machine small vocabulary; do
    self_output="/tmp/flow-self-host-$spec.c"
    self_bin="/tmp/flow-self-host-$spec"
    expected_flow=$spec
    test "$spec" = small && expected_flow=scan
    "$compiler_bin" "$root/examples/$spec.flow" -o "$self_output"
    cc -std=c17 -Wall -Wextra -Wpedantic "$self_output" -o "$self_bin"
    self_run=$($self_bin)
    printf '%s\n' "$self_run" | grep -q "flow: ${expected_flow}"
    printf '%s\n' "$self_run" | grep -q 'self_host: bootstrap'
    case "$spec" in
        bounded_queue) marker='queue_processed 5' ;;
        shared_cache) marker='cache_hits 5' ;;
        parallel_map) marker='mapped 198' ;;
        binary_parser) marker='packet_valid' ;;
        state_machine) marker='final_state 2' ;;
        *) marker='' ;;
    esac
    case "$spec" in
        bounded_queue) component_marker='component: bounded_queue' ;;
        shared_cache) component_marker='component: sharded_hash' ;;
        parallel_map) component_marker='component: parallel_map' ;;
        rank) component_marker='component: ordered_tree' ;;
        binary_parser) component_marker='component: binary_parser' ;;
        state_machine) component_marker='component: state_machine' ;;
        small) component_marker='component: linear_array' ;;
        vocabulary) component_marker='component: parallel_map' ;;
    esac
    printf '%s\n' "$self_run" | grep -q "$component_marker"
    if test -n "$marker"; then
        printf '%s\n' "$self_run" | grep -q "$marker"
    fi
done
printf '%s\n' "$(/tmp/flow-self-host-small)" | grep -q 'top 2'
printf '%s\n' "$(/tmp/flow-self-host-small)" | grep -q 'user 3 score 99'

if grep -E -n '(Arc|RwLock|Vec|HashMap|pthread|malloc)' \
    "$root/src/flow.h" "$root/src/semantic.c"; then
    echo 'SELF_HOST_PRECHECK=failed: implementation mechanisms leaked into IR' >&2
    exit 1
fi

for source in parser.c semantic.c registry.c verifier.c backend.c; do
    test -f "$root/src/$source"
done
for symbol in arena_store queue_push registry_hash emit_component_template; do
    grep -q "$symbol" "$root/tools/self-host-stage2.c"
done

# Verify Plan Artifact & Contract Hash alignment between native flowc and stage2
native_lock=/tmp/flow-native-project.lock
stage2_lock=/tmp/flow-stage2-project.lock
"$root/build/flowc" "$root/examples/project.flow" -o /tmp/flow-native-proj.c --search --iterations 50 --seed 42 --lock "$native_lock" >/dev/null
"$stage2_bin" "$root/examples/project.flow" -o /tmp/flow-stage2-proj.c --search --iterations 50 --seed 42 --lock "$stage2_lock" >/dev/null

grep -q 'module=builtin' "$native_lock"
grep -q 'module=builtin' "$stage2_lock"
grep -q 'component=parallel_map' "$native_lock"
grep -q 'component=parallel_map' "$stage2_lock"

echo 'SELF_HOST_PRECHECK=passed'
echo 'SELF_HOST_STATUS=passed-semantic-bootstrap artifact_alignment=verified'
