#ifndef WREPORT_PYTHON_VARTABLE_H
#define WREPORT_PYTHON_VARTABLE_H

#include <Python.h>

namespace wreport {
struct Vartable;
}

extern "C" {

typedef struct {
    PyObject_HEAD
    const wreport::Vartable* table;
} dpy_Vartable;

PyAPI_DATA(PyTypeObject) dpy_Vartable_Type;

#define dpy_Vartable_Check(ob) \
    (Py_TYPE(ob) == &dpy_Vartable_Type || \
     PyType_IsSubtype(Py_TYPE(ob), &dpy_Vartable_Type))

}

namespace wreport {
namespace python {

dpy_Vartable* vartable_create(const wreport::Vartable* table);

void register_vartable(PyObject* m);

}
}
#endif
