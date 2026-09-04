import glob

files = ["tests/ensemble-test.c", "tests/smt-test.c", "tests/mlir-llvm-test.c", "tests/topology-test.c"]

for path in files:
    with open(path, 'r') as f:
        content = f.read()
    
    if '#include "flow_test_kit.h"' not in content:
        content = '#include "flow_test_kit.h"\n' + content
        with open(path, 'w') as f:
            f.write(content)
