import glob

files = ["tests/ensemble-test.c", "tests/smt-test.c", "tests/mlir-llvm-test.c", "tests/topology-test.c"]

for path in files:
    with open(path, 'r') as f:
        content = f.read()
    
    # We want to find `});\n` followed by whatever up to `return 0;\n}`
    # and put it inside `});`
    
    if "});" in content:
        parts = content.split("});")
        if len(parts) == 2:
            before = parts[0]
            after = parts[1]
            
            # remove the trailing `}` from after
            after = after.replace("\n}", "")
            
            # put after inside before
            new_content = before + after + "\n});\n}"
            
            with open(path, 'w') as f:
                f.write(new_content)
