# mqlib_metrics

A small Python wrapper around MQLib's instance metrics (`GraphMetrics`, the 58 metrics and 12 runtimes that `MQLib -m` prints) for a QUBO held in memory: no text file, no subprocess. It is not a wrapper of MQLib's heuristics.

## What it wraps

Only these MQLib sources are compiled, unmodified, from the parent tree:

- `src/problem/instance.cpp`, `src/problem/qubo_instance.cpp`, `src/problem/max_cut_instance.cpp`
- `src/metrics/max_cut_metrics.cpp`
- `src/util/random.cpp`

`max_cut_metrics.cpp` includes `problem/heuristic.h` but uses nothing from it, so no heuristic sources are needed. The binding is `src/bindings.cpp` (raw CPython API, `Py_buffer`).

## Equivalence to `MQLib -fQ <file> -m`

`main.cpp`'s metrics branch does `QUBOInstance(file)` → `MaxCutInstance(const QUBOInstance&)` → `GraphMetrics(mi).AllMetrics(&metrics, &runtimes)`. The wrapper does the same, except that the `QUBOInstance` comes from the in-memory constructor `QUBOInstance(offDiag, mainDiag, n)`:

- The off-diagonal tuples go through the same `Instance::AddLink` as the file parser and in the same order, so `nonzero_` and `all_nonzero_` are identical.
- The file parser stores a diagonal line `i i w` as `lin_[i] = w`; the wrapper passes the same values as `mainDiag`.

`qubo_metrics(Q, h, negate=True)` takes our solver convention (minimise x'Qx + h'x, Q symmetric with both halves stored) and builds exactly what `random_qubo/extract_features.py::_write_qubo_text(sign*Q, sign*h)` writes:

- the off-diagonal entries are `scipy.sparse.triu(sign*Q, k=1).tocoo()`, in that COO order (explicit zeros included);
- the diagonal is `sign*h` where it is nonzero, else 0.0;
- the diagonal of Q is ignored, as in the writer;
- `negate=True` (sign = -1) is the writer's `--negate`. MQLib's `-fQ` maximises, so this passes the maximisation form (-Q, -h).

`AllMetrics` calls `srand(0)` and draws from the process-global `rand()`, so the result is deterministic for a given input order. The call holds a mutex for that reason. It releases the GIL, and it resets the global C `rand()` state of the process.

Only `AllMetrics` is timed: the runtimes are MQLib's own per-metric timers, in seconds. Building the instance is not timed.

Compile flags match the Makefile where it matters for floating point: `-O2`, ISO C++11, no `NDEBUG`.

## API

```python
import mqlib_metrics as mm
metrics, runtimes = mm.qubo_metrics(Q, h)   # dicts: name -> value, name -> seconds
mm.metric_names(), mm.runtime_names()       # column order of `MQLib -mh`
mm.qubo_entries(Q, h)                       # (rows, cols, vals, diag, n) passed to the C++ core
mm.build_info()                             # source commit and compile flags
```

`_core.metrics(rows, cols, vals, diag, n)` takes contiguous int64 rows and cols (0-based, off-diagonal), float64 vals, a float64 diag of length n, and n.

## Build and install

From the research repo (`tripartite_matching/`):

```sh
uv pip install --python .venv/bin/python ../MQLib/python
```

This builds with scikit-build-core 0.11.6 in an isolated environment. The extension compiles `../src` and `../include` relative to this directory, so build from the directory itself (a wheel, as above), never from an sdist.

For a development build without installing:

```sh
cmake -S python -B <build dir> -DPython_EXECUTABLE=<python> -DCMAKE_BUILD_TYPE=Release
cmake --build <build dir>
cmake --install <build dir> --prefix <dir>   # then copy mqlib_metrics/__init__.py into <dir>/mqlib_metrics
```

## Source

Built and checked from commit `dd6a04b` ("Fix download"), which is `bbd9a6c` ("DFS fix", the connectivity DFS in `max_cut_metrics.cpp`) plus a download-script change. `build_info()["mqlib_commit"]` records the commit at build time, with `-dirty` if `src/` or `include/` had local changes.

The equivalence check is in `tripartite_matching/experiments/feature_timing/LEDGER.md` (2026-10-21, F2). On 30 QUBOs, every metric equals `bin/MQLib -fQ <file> -m` at its printed precision. It also equals, bit for bit, a reference driver that reads the same file.
