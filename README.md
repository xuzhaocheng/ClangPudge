# clang-pudge
A clang tool to extract methods/functions boundaries from source files. Support c/c++/objc/objc++.

## Usage
```bash
$ clang-pudge -p <build_path> -output-file <output_json_file> <source0> ...
```

The output JSON format is as below:
```
{
  <file_path>: [
  {
    "name": <mangled_name>,
    "start": <fucntion_start_line_number>,
    "end": <function_end_line_number>
  },
  ...
  ]
}

```

## How to Build from Source
1. Clone `llvm-project` from https://github.com/llvm/llvm-project, or you can choose [apple fork version](https://github.com/apple/llvm-project) or other fork.
2. Create a directory named `clang-pudge` under `clang/clang-tools-extra`.
3. Add new line `add_subdirectory(clang-pudge)` to `clang-tools-extra/CMakeLists.txt`.
4. Follow [Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm) to build, or you can refer to this [guide](https://clang.llvm.org/docs/LibASTMatchersTutorial.html)


