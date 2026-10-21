"""MQLib's instance metrics (GraphMetrics) for an in-memory QUBO.

Same computation as ``MQLib -fQ <file> -m``: QUBOInstance -> MaxCutInstance (linear terms
become edges to an added master node) -> GraphMetrics.AllMetrics.
"""

import numpy as np
import scipy.sparse as sp

from . import _core

build_info = _core.build_info


def metric_names():
    return _core.metric_names()


def runtime_names():
    return _core.runtime_names()


def qubo_entries(Q, h, *, negate=True):
    """The (rows, cols, vals, diag) that ``random_qubo/extract_features.py`` writes to the
    MQLib text file: off-diagonal entries of ``triu(sign*Q, 1)`` in its COO order (0-based),
    and ``sign*h`` where nonzero. The diagonal of Q is not written."""
    if not sp.issparse(Q):
        Q = np.asarray(Q)
    if Q.ndim != 2 or Q.shape[0] != Q.shape[1]:
        raise ValueError("Q must be square")
    n = Q.shape[0]
    h = np.asarray(h)
    if h.shape != (n,):
        raise ValueError("h must have shape (n,)")
    sign = -1.0 if negate else 1.0
    upper = sp.triu(sign * Q, k=1).tocoo()
    hs = sign * h
    vals = np.ascontiguousarray(upper.data, dtype=np.float64)
    diag = np.where(hs != 0, hs, 0.0).astype(np.float64)
    if not (np.isfinite(vals).all() and np.isfinite(diag).all()):
        raise ValueError("Q and h must be finite")
    rows = np.ascontiguousarray(upper.row, dtype=np.int64)
    cols = np.ascontiguousarray(upper.col, dtype=np.int64)
    return rows, cols, vals, np.ascontiguousarray(diag), n


def qubo_metrics(Q, h, *, negate=True):
    """MQLib metrics of the QUBO min x'Qx + h'x (Q symmetric, both halves stored).

    ``negate=True`` passes the maximisation form (-Q, -h) that MQLib's ``-fQ`` expects, as
    ``extract_features.py --negate``. Returns ``(metrics, runtimes)``: dicts keyed by
    ``metric_names()`` and ``runtime_names()``; runtimes are MQLib's own per-metric timers
    in seconds (the instance construction is not timed).
    """
    rows, cols, vals, diag, n = qubo_entries(Q, h, negate=negate)
    if n < 2 or rows.size + np.count_nonzero(diag) == 0:
        raise ValueError("degenerate instance (0 terms or n<2)")
    values, times = _core.metrics(rows, cols, vals, diag, n)
    return dict(zip(metric_names(), values)), dict(zip(runtime_names(), times))


def graph_metrics(n, i, j, w):
    """MQLib metrics of a weighted Max-Cut graph on n nodes: edges (i[k], j[k], w[k]), 0-based, each unordered pair once, no
    self-loops (as ``MQLib -fM <file> -m``, without a file). Returns ``(metrics, runtimes)`` as qubo_metrics."""
    rows = np.ascontiguousarray(i, dtype=np.int64)
    cols = np.ascontiguousarray(j, dtype=np.int64)
    vals = np.ascontiguousarray(w, dtype=np.float64)
    if not np.isfinite(vals).all():
        raise ValueError("w must be finite")
    values, times = _core.graph_metrics(rows, cols, vals, int(n))
    return dict(zip(metric_names(), values)), dict(zip(runtime_names(), times))
