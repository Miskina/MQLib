#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <climits>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "metrics/max_cut_metrics.h"
#include "problem/instance.h"
#include "problem/max_cut_instance.h"
#include "problem/qubo_instance.h"

namespace {

// AllMetrics seeds and draws from the process-global rand(), so calls are serialised.
std::mutex metrics_mutex;

class Buffer {
 public:
  Buffer() : acquired_(false) { std::memset(&view_, 0, sizeof(view_)); }
  ~Buffer() {
    if (acquired_) PyBuffer_Release(&view_);
  }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  bool acquire(PyObject* object, const char* name, bool integer) {
    if (PyObject_GetBuffer(object, &view_,
                           PyBUF_FORMAT | PyBUF_ND | PyBUF_C_CONTIGUOUS) != 0) {
      return false;
    }
    acquired_ = true;
    const char* format = view_.format ? view_.format : "B";
    if (format[0] == '@' || format[0] == '=' || format[0] == '<') ++format;
    const bool ok = view_.ndim == 1 && view_.itemsize == 8 &&
                    (integer ? (std::strcmp(format, "q") == 0 ||
                                std::strcmp(format, "l") == 0)
                             : std::strcmp(format, "d") == 0);
    if (!ok) {
      PyErr_Format(PyExc_TypeError, "%s must be a 1-d contiguous %s buffer",
                   name, integer ? "int64" : "float64");
      return false;
    }
    return true;
  }

  Py_ssize_t size() const { return view_.len / view_.itemsize; }
  const long long* ints() const { return static_cast<const long long*>(view_.buf); }
  const double* reals() const { return static_cast<const double*>(view_.buf); }

 private:
  Py_buffer view_;
  bool acquired_;
};

PyObject* names_tuple(const std::vector<std::string>& names) {
  PyObject* out = PyTuple_New(static_cast<Py_ssize_t>(names.size()));
  if (!out) return nullptr;
  for (size_t i = 0; i < names.size(); ++i) {
    PyObject* s = PyUnicode_FromString(names[i].c_str());
    if (!s) {
      Py_DECREF(out);
      return nullptr;
    }
    PyTuple_SET_ITEM(out, static_cast<Py_ssize_t>(i), s);
  }
  return out;
}

PyObject* doubles_tuple(const std::vector<double>& values) {
  PyObject* out = PyTuple_New(static_cast<Py_ssize_t>(values.size()));
  if (!out) return nullptr;
  for (size_t i = 0; i < values.size(); ++i) {
    PyObject* v = PyFloat_FromDouble(values[i]);
    if (!v) {
      Py_DECREF(out);
      return nullptr;
    }
    PyTuple_SET_ITEM(out, static_cast<Py_ssize_t>(i), v);
  }
  return out;
}

PyObject* metric_names(PyObject*, PyObject*) {
  std::vector<std::string> names;
  GraphMetrics::AllMetricNames(&names);
  return names_tuple(names);
}

PyObject* runtime_names(PyObject*, PyObject*) {
  std::vector<std::string> names;
  GraphMetrics::AllRuntimeTypes(&names);
  return names_tuple(names);
}

PyObject* build_info(PyObject*, PyObject*) {
  return Py_BuildValue("{s:s,s:s}", "mqlib_commit", MQLIB_COMMIT, "flags",
                       MQLIB_BUILD_FLAGS);
}

// metrics(rows, cols, vals, diag, n): 0-based off-diagonal entries in the order
// MQLib's QUBO file parser would read them, the main diagonal (linear terms) and n.
// Same path as `MQLib -fQ <file> -m`.
PyObject* metrics(PyObject*, PyObject* args) {
  PyObject *rows_obj, *cols_obj, *vals_obj, *diag_obj;
  long long n;
  if (!PyArg_ParseTuple(args, "OOOOL", &rows_obj, &cols_obj, &vals_obj,
                        &diag_obj, &n)) {
    return nullptr;
  }
  Buffer rows, cols, vals, diag;
  if (!rows.acquire(rows_obj, "rows", true) ||
      !cols.acquire(cols_obj, "cols", true) ||
      !vals.acquire(vals_obj, "vals", false) ||
      !diag.acquire(diag_obj, "diag", false)) {
    return nullptr;
  }
  if (n < 1 || n >= INT_MAX) {
    PyErr_SetString(PyExc_ValueError, "n out of range");
    return nullptr;
  }
  const Py_ssize_t nnz = rows.size();
  if (cols.size() != nnz || vals.size() != nnz) {
    PyErr_SetString(PyExc_ValueError, "rows, cols and vals differ in length");
    return nullptr;
  }
  if (diag.size() != n) {
    PyErr_SetString(PyExc_ValueError, "diag must have length n");
    return nullptr;
  }
  if (nnz + n >= INT_MAX) {
    PyErr_SetString(PyExc_ValueError, "too many entries");
    return nullptr;
  }
  for (Py_ssize_t k = 0; k < nnz; ++k) {
    const long long r = rows.ints()[k], c = cols.ints()[k];
    if (r < 0 || r >= n || c < 0 || c >= n || r == c) {
      PyErr_Format(PyExc_ValueError,
                   "entry %zd: (%lld, %lld) is not an off-diagonal index", k, r, c);
      return nullptr;
    }
  }

  std::vector<double> out_metrics, out_runtimes;
  std::string error;
  bool memory_error = false;
  Py_BEGIN_ALLOW_THREADS
  try {
    std::vector<Instance::InstanceTuple> off_diag;
    off_diag.reserve(static_cast<size_t>(nnz));
    for (Py_ssize_t k = 0; k < nnz; ++k) {
      off_diag.push_back(Instance::InstanceTuple(
          std::make_pair(static_cast<int>(rows.ints()[k]) + 1,
                         static_cast<int>(cols.ints()[k]) + 1),
          vals.reals()[k]));
    }
    std::vector<double> main_diag(diag.reals(), diag.reals() + n);
    QUBOInstance qi(off_diag, main_diag, static_cast<int>(n));
    MaxCutInstance mi(qi);
    std::lock_guard<std::mutex> lock(metrics_mutex);
    GraphMetrics gm(mi);
    gm.AllMetrics(&out_metrics, &out_runtimes);
  } catch (const std::bad_alloc&) {
    memory_error = true;
  } catch (const std::exception& e) {
    error = e.what();
  } catch (...) {
    error = "unknown C++ exception";
  }
  Py_END_ALLOW_THREADS
  if (memory_error) return PyErr_NoMemory();
  if (!error.empty()) {
    PyErr_SetString(PyExc_RuntimeError, error.c_str());
    return nullptr;
  }

  PyObject* m = doubles_tuple(out_metrics);
  if (!m) return nullptr;
  PyObject* t = doubles_tuple(out_runtimes);
  if (!t) {
    Py_DECREF(m);
    return nullptr;
  }
  return Py_BuildValue("(NN)", m, t);
}

// graph_metrics(rows, cols, vals, n): a weighted Max-Cut graph given as 0-based edges (each unordered pair
// once, no self-loops) on n nodes. No QUBO conversion and so no master node.
PyObject* graph_metrics(PyObject*, PyObject* args) {
  PyObject *rows_obj, *cols_obj, *vals_obj;
  long long n;
  if (!PyArg_ParseTuple(args, "OOOL", &rows_obj, &cols_obj, &vals_obj, &n)) {
    return nullptr;
  }
  Buffer rows, cols, vals;
  if (!rows.acquire(rows_obj, "rows", true) ||
      !cols.acquire(cols_obj, "cols", true) ||
      !vals.acquire(vals_obj, "vals", false)) {
    return nullptr;
  }
  if (n < 2 || n >= INT_MAX) {
    PyErr_SetString(PyExc_ValueError, "n out of range");
    return nullptr;
  }
  const Py_ssize_t nnz = rows.size();
  if (cols.size() != nnz || vals.size() != nnz) {
    PyErr_SetString(PyExc_ValueError, "rows, cols and vals differ in length");
    return nullptr;
  }
  if (nnz < 1 || nnz >= INT_MAX) {
    PyErr_SetString(PyExc_ValueError, "need 1 <= edges < INT_MAX");
    return nullptr;
  }
  for (Py_ssize_t k = 0; k < nnz; ++k) {
    const long long r = rows.ints()[k], c = cols.ints()[k];
    if (r < 0 || r >= n || c < 0 || c >= n || r == c) {
      PyErr_Format(PyExc_ValueError, "edge %zd: (%lld, %lld) is not a pair of distinct nodes", k, r, c);
      return nullptr;
    }
  }

  std::vector<double> out_metrics, out_runtimes;
  std::string error;
  bool memory_error = false;
  Py_BEGIN_ALLOW_THREADS
  try {
    std::vector<Instance::InstanceTuple> edges;
    edges.reserve(static_cast<size_t>(nnz));
    for (Py_ssize_t k = 0; k < nnz; ++k) {
      edges.push_back(Instance::InstanceTuple(
          std::make_pair(static_cast<int>(rows.ints()[k]) + 1,
                         static_cast<int>(cols.ints()[k]) + 1),
          vals.reals()[k]));
    }
    MaxCutInstance mi(edges, static_cast<int>(n));
    std::lock_guard<std::mutex> lock(metrics_mutex);
    GraphMetrics gm(mi);
    gm.AllMetrics(&out_metrics, &out_runtimes);
  } catch (const std::bad_alloc&) {
    memory_error = true;
  } catch (const std::exception& e) {
    error = e.what();
  } catch (...) {
    error = "unknown C++ exception";
  }
  Py_END_ALLOW_THREADS
  if (memory_error) return PyErr_NoMemory();
  if (!error.empty()) {
    PyErr_SetString(PyExc_RuntimeError, error.c_str());
    return nullptr;
  }

  PyObject* m = doubles_tuple(out_metrics);
  if (!m) return nullptr;
  PyObject* t = doubles_tuple(out_runtimes);
  if (!t) {
    Py_DECREF(m);
    return nullptr;
  }
  return Py_BuildValue("(NN)", m, t);
}

PyMethodDef methods[] = {
    {"metrics", metrics, METH_VARARGS,
     "metrics(rows, cols, vals, diag, n) -> (metrics, runtimes)"},
    {"graph_metrics", graph_metrics, METH_VARARGS,
     "graph_metrics(rows, cols, vals, n) -> (metrics, runtimes)"},
    {"metric_names", metric_names, METH_NOARGS, "GraphMetrics::AllMetricNames"},
    {"runtime_names", runtime_names, METH_NOARGS,
     "GraphMetrics::AllRuntimeTypes"},
    {"build_info", build_info, METH_NOARGS, "Source commit and compile flags."},
    {nullptr, nullptr, 0, nullptr}};

PyModuleDef module = {PyModuleDef_HEAD_INIT, "_core",
                      "MQLib GraphMetrics on an in-memory QUBOInstance.", -1,
                      methods, nullptr, nullptr, nullptr, nullptr};

}  // namespace

PyMODINIT_FUNC PyInit__core() { return PyModule_Create(&module); }
