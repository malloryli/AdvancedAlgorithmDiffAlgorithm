# mydiff

A command-line diff tool built on Myers' O(ND) difference algorithm (1986).
It prints output in GNU diff formats, and it can also apply unified patches.

## Build

Requires a C++17 compiler and `make`.

```sh
make          # builds build/mydiff
make test     # runs unit tests and GNU diff/patch compatibility tests
```

## Usage

```sh
build/mydiff FILE1 FILE2              # normal diff output
build/mydiff -u FILE1 FILE2           # unified diff
build/mydiff -y FILE1 FILE2           # side-by-side
build/mydiff --word-diff FILE1 FILE2  # inline word-level diff
build/mydiff patch FILE PATCHFILE     # apply a unified diff
```

Run `build/mydiff --help` for all options.

Exit status: 0 = identical, 1 = different, 2 = error.

## Example

```sh
build/mydiff -u examples/before.py examples/after.py
```

## Where things are

- `src/myers.cpp`: the Myers algorithm (the core of the project)
- `src/tokenize.cpp`: splits input into lines, words or characters
- `src/format.cpp`: output formats
- `src/slide.cpp`: tidies hunk boundaries for readability
- `src/patch.cpp`: applies patches
- `src/main.cpp`: command-line interface
- `tests/`: unit and compatibility tests
