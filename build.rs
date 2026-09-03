fn main() {
    let mut build = cc::Build::new();
    build
        .file("src/abi.c")
        .file("src/adaptive.c")
        .file("src/audit.c")
        .file("src/backend.c")
        .file("src/benchmark.c")
        .file("src/bitspace.c")
        .file("src/builtin_plugin.c")
        .file("src/embodied.c")
        .file("src/flowc.c")
        .file("src/flowy.c")
        .file("src/flowy_cli.c")
        .file("src/flowy_fvec.c")
        .file("src/jit.c")
        .file("src/orchestrator.c")
        .file("src/parser.c")
        .file("src/primitive.c")
        .file("src/registry.c")
        .file("src/reload.c")
        .file("src/search.c")
        .file("src/security.c")
        .file("src/smt.c")
        .file("src/swarm.c")
        .file("src/topology.c")
        .include("src")
        .define("FLOWC_NO_MAIN", "1")
        .flag("-std=c17")
        .flag("-O3")
        .flag("-pthread");

    build.compile("flow_core");

    println!("cargo:rerun-if-changed=src/");
}
