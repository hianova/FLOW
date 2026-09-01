fn main() {
    let mut build = cc::Build::new();
    build
        .file("src/abi.c")
        .file("src/adaptive.c")
        .file("src/backend.c")
        .file("src/benchmark.c")
        .file("src/bitspace.c")
        .file("src/builtin_plugin.c")
        .file("src/flowc.c")
        .file("src/parser.c")
        .file("src/registry.c")
        .file("src/reload.c")
        .file("src/search.c")
        .file("src/security.c")
        .file("src/semantic.c")
        .file("src/verifier.c")
        .include("src")
        .define("FLOWC_NO_MAIN", "1")
        .flag("-std=c17")
        .flag("-O3")
        .flag("-pthread");

    build.compile("flow_core");

    println!("cargo:rerun-if-changed=src/");
}
