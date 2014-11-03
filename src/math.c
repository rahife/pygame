
/*
  pygame - Python Game Library

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/* Adjust gcc 4.4 optimization for floating point on x86-32 PCs running Linux.
 * This addresses bug 52:
 * http://pygame.motherhamster.org/bugzilla/show_bug.cgi?id=52
 */
#if defined(__GNUC__) && defined(__linux__) && defined(__i386__) && \
  __SIZEOF_POINTER__ == 4 &&  __GNUC__ == 4 && __GNUC_MINOR__ == 4
#pragma GCC optimize ("float-store")
#endif

#define PYGAMEAPI_MATH_INTERNAL
#define NO_PYGAME_C_API
#include "doc/math_doc.h"
#include "pygame.h"
#include "structmember.h"
#include "pgcompat.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

/* on some windows platforms math.h doesn't define M_PI */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VECTOR_EPSILON (1e-6)
#define VECTOR_MAX_SIZE (4)
#define STRING_BUF_SIZE (100)
#define SWIZZLE_ERR_NO_ERR         0
#define SWIZZLE_ERR_DOUBLE_IDX     1
#define SWIZZLE_ERR_EXTRACTION_ERR 2

#define OP_ADD          1
/* #define OP_IADD         2 */
#define OP_SUB          3
/* #define OP_ISUB         4 */
#define OP_MUL          5
/* #define OP_IMUL         6 */
#define OP_DIV          7
/* #define OP_IDIV         8 */
#define OP_FLOOR_DIV    9
/* #define OP_IFLOOR_DIV  10 */
#define OP_MOD         11
//#define OP_INPLACE     16   // not needed for immutable objects
#define OP_ARG_REVERSE 32
#define OP_ARG_UNKNOWN 64
#define OP_ARG_VECTOR 128
#define OP_ARG_NUMBER 256

static PyTypeObject PyVector2_Type;
static PyTypeObject PyVector3_Type;
static PyTypeObject PyVectorElementwiseProxy_Type;
static PyTypeObject PyVectorIter_Type;

#define PyVector2_Check(x) (Py_TYPE(x) == &PyVector2_Type)
#define PyVector3_Check(x) (Py_TYPE(x) == &PyVector3_Type)
#define PyVector_Check(x) (PyVector2_Check(x) || PyVector3_Check(x))
#define vector_elementwiseproxy_Check(x) \
    (Py_TYPE(x) == &PyVectorElementwiseProxy_Type)

#define DEG2RAD(angle) ((angle) * M_PI / 180.)
#define RAD2DEG(angle) ((angle) * 180. / M_PI)

typedef struct
{
    PyObject_HEAD
    double *coords;     /* Coordinates */
    unsigned int dim;   /* Dimension of the vector */
    double epsilon;     /* Small value for comparisons */
} PyVector;

typedef struct {
    PyObject_HEAD
    long it_index;
    PyVector *vec;
} vectoriter;

typedef struct {
    PyObject_HEAD
    PyVector *vec;
} vector_elementwiseproxy;


/* further forward declerations */
/* generic helper functions */
static int RealNumber_Check(PyObject *obj);
static double PySequence_GetItem_AsDouble(PyObject *seq, Py_ssize_t index);
static int PySequence_AsVectorCoords(PyObject *seq, double *coords, const size_t size);
static int PyVectorCompatible_Check(PyObject *obj, int dim);
static double _scalar_product(const double *coords1, const double *coords2, int size);
static int get_double_from_unicode_slice(PyObject *unicode_obj,
                                         Py_ssize_t idx1, Py_ssize_t idx2,
                                         double *val);
static int _vector_find_string_helper(PyObject *str_obj, const char *substr,
                                      Py_ssize_t start, Py_ssize_t end);
static int _vector_coords_from_string(PyObject *str, char **delimiter,
                                      double *coords, Py_ssize_t dim);

/* generic vector functions */
static PyObject *PyVector_NEW(int dim);
static void vector_dealloc(PyVector* self);
static PyObject *vector_generic_math(PyObject *o1, PyObject *o2, int op);
static PyObject *vector_add(PyObject *o1, PyObject *o2);
static PyObject *vector_sub(PyObject *o1, PyObject *o2);
static PyObject *vector_mul(PyObject *o1, PyObject *o2);
static PyObject *vector_div(PyVector *self, PyObject *other);
static PyObject *vector_floor_div(PyVector *self, PyObject *other);
static PyObject *vector_neg(PyVector *self);
static PyObject *vector_pos(PyVector *self);
static int vector_nonzero(PyVector *self);
static Py_ssize_t vector_len(PyVector *self);
static PyObject *vector_GetItem(PyVector *self, Py_ssize_t index);
static PyObject *vector_GetSlice(PyVector *self, Py_ssize_t ilow, Py_ssize_t ihigh);
static PyObject *vector_getx (PyVector *self, void *closure);
static PyObject *vector_gety (PyVector *self, void *closure);
static PyObject *vector_getz (PyVector *self, void *closure);
#ifdef PYGAME_MATH_VECTOR_HAVE_W
static PyObject *vector_getw (PyVector *self, void *closure);
#endif
static PyObject *vector_richcompare(PyObject *o1, PyObject *o2, int op);
static PyObject *vector_length(PyVector *self);
static PyObject *vector_length_squared(PyVector *self);
static PyObject *vector_normalize(PyVector *self);
static PyObject *vector_dot(PyVector *self, PyObject *other);
static PyObject *vector_scale_to_length(PyVector *self, PyObject *length);
static PyObject *vector_slerp(PyVector *self, PyObject *args);
static PyObject *vector_lerp(PyVector *self, PyObject *args);
static int _vector_reflect_helper(double *dst_coords, const double *src_coords,
                                  PyObject *normal, int dim, double epsilon);
static PyObject *vector_reflect(PyVector *self, PyObject *normal);
static double _vector_distance_helper(PyVector *self, PyObject *other);
static PyObject *vector_distance_to(PyVector *self, PyObject *other);
static PyObject *vector_distance_squared_to(PyVector *self, PyObject *other);
static PyObject *vector_getAttr_swizzle(PyVector *self, PyObject *attr_name);
static PyObject *vector_elementwise(PyVector *self);
static int _vector_check_snprintf_success(int return_code);
static PyObject *vector_repr(PyVector *self);
static PyObject *vector_str(PyVector *self);
/*
static Py_ssize_t vector_readbuffer(PyVector *self, Py_ssize_t segment, void **ptrptr);
static Py_ssize_t vector_writebuffer(PyVector *self, Py_ssize_t segment, void **ptrptr);
static Py_ssize_t vector_segcount(PyVector *self, Py_ssize_t *lenp);
*/

/* vector2 specific functions */
static PyObject *vector2_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
static int vector2_init(PyVector *self, PyObject *args, PyObject *kwds);
static int _vector2_rotate_helper(double *dst_coords, const double *src_coords,
                                  double angle, double epsilon);
static PyObject *vector2_rotate(PyVector *self, PyObject *args);
static PyObject *vector2_cross(PyVector *self, PyObject *other);
static PyObject *vector2_angle_to(PyVector *self, PyObject *other);
static PyObject *vector2_as_polar(PyVector *self);
static PyObject *vector2_from_polar(PyObject *cls, PyObject *args);

/* vector3 specific functions */
static PyObject *vector3_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
static int vector3_init(PyVector *self, PyObject *args, PyObject *kwds);
static int _vector3_rotate_helper(double *dst_coords, const double *src_coords,
                                  const double *axis_coords,
                                  double angle, double epsilon);
static PyObject *vector3_rotate(PyVector *self, PyObject *args);
static PyObject *vector3_cross(PyVector *self, PyObject *other);
static PyObject *vector3_angle_to(PyVector *self, PyObject *other);
static PyObject *vector3_as_spherical(PyVector *self);
static PyObject *vector3_from_spherical(PyObject *cls, PyObject *args);

/* vector iterator functions */
static void vectoriter_dealloc(vectoriter *it);
static PyObject *vectoriter_next(vectoriter *it);
static PyObject *vectoriter_len(vectoriter *it);
static PyObject *vector_iter(PyObject *vec);

/* elementwiseproxy */
static void vector_elementwiseproxy_dealloc(vector_elementwiseproxy *it);
static PyObject *vector_elementwiseproxy_richcompare(PyObject *o1, PyObject *o2, int op);
static PyObject *vector_elementwiseproxy_generic_math(PyObject *o1,
                                                      PyObject *o2, int op);
static PyObject *vector_elementwiseproxy_add(PyObject *o1, PyObject *o2);
static PyObject *vector_elementwiseproxy_sub(PyObject *o1, PyObject *o2);
static PyObject *vector_elementwiseproxy_mul(PyObject *o1, PyObject *o2);
static PyObject *vector_elementwiseproxy_div(PyObject *o1, PyObject *o2);
static PyObject *vector_elementwiseproxy_floor_div(PyObject *o1, PyObject *o2);
static PyObject *vector_elementwiseproxy_pow(PyObject *baseObj, PyObject *expoObj, PyObject *mod);
static PyObject *vector_elementwiseproxy_mod(PyObject *o1, PyObject *o2);
static PyObject *vector_elementwiseproxy_abs(vector_elementwiseproxy *self);
static PyObject *vector_elementwiseproxy_neg(vector_elementwiseproxy *self);
static PyObject *vector_elementwiseproxy_pos(vector_elementwiseproxy *self);
static int vector_elementwiseproxy_nonzero(vector_elementwiseproxy *self);
static PyObject *vector_elementwise(PyVector *vec);


static int swizzling_enabled = 0;


/********************************
 * Global helper functions
 ********************************/

static int
RealNumber_Check(PyObject *obj)
{
    if (obj && PyNumber_Check(obj) && !PyComplex_Check(obj))
        return 1;
    return 0;
}

static double
PySequence_GetItem_AsDouble(PyObject *seq, Py_ssize_t index)
{
    PyObject *item;
    double value;

    item = PySequence_GetItem(seq, index);
    if (item == NULL) {
        PyErr_SetString(PyExc_TypeError, "a sequence is expected");
        return -1;
    }
    value = PyFloat_AsDouble(item);
    Py_DECREF(item);
    if (PyErr_Occurred())
        return -1;
    return value;
}

static int
PySequence_AsVectorCoords(PyObject *seq, double *const coords, const size_t size)
{
    int i;

    if (PyVector_Check(seq)) {
        memcpy(coords, ((PyVector *)seq)->coords, sizeof(double) * size);
        return 1;
    }
    if (!PySequence_Check(seq) || PySequence_Length(seq) != size) {
        PyErr_SetString(PyExc_ValueError, "Sequence has the wrong length.");
        return 0;
    }

    for (i = 0; i < size; ++i) {
        coords[i] = PySequence_GetItem_AsDouble(seq, i);
        if (PyErr_Occurred()) {
            return 0;
        }
    }
    return 1;
}

static int
PyVectorCompatible_Check(PyObject *obj, int dim)
{
    int i;
    PyObject *tmp;

    switch(dim) {
    case 2:
        if (PyVector2_Check(obj)) {
            return 1;
        }
        break;
    case 3:
        if (PyVector3_Check(obj)) {
            return 1;
        }
        break;
/*
    case 4:
        if (PyVector4_Check(obj)) {
            return 1;
        }
        break;
*/
    default:
        PyErr_SetString(PyExc_SystemError,
                        "Wrong internal call to PyVectorCompatible_Check.");
        return 0;
    }

    if (!PySequence_Check(obj) || (PySequence_Length(obj) != dim)) {
        return 0;
    }

    for (i = 0; i < dim; ++i) {
        tmp = PySequence_GetItem(obj, i);
        if (tmp == NULL || !RealNumber_Check(tmp)) {
            Py_XDECREF(tmp);
            return 0;
        }
        Py_DECREF(tmp);
    }
    return 1;
}

static double
_scalar_product(const double *coords1, const double *coords2, int size)
{
    int i;
    double product = 0;
    for (i = 0; i < size; ++i)
        product += coords1[i] * coords2[i];
    return product;
}


static int
get_double_from_unicode_slice(PyObject *unicode_obj,
                              Py_ssize_t idx1, Py_ssize_t idx2, double *val)
{
    PyObject *float_obj;
    PyObject *slice = PySequence_GetSlice (unicode_obj, idx1, idx2);
    if (slice == NULL) {
        PyErr_SetString (PyExc_SystemError,
                         "internal error while converting str slice to float");
        return -1;
    }
#if PY3
    float_obj = PyFloat_FromString (slice);
#else
    float_obj = PyFloat_FromString (slice, NULL);
#endif
    Py_DECREF (slice);
    if (float_obj == NULL)
        return 0;
    *val = PyFloat_AsDouble(float_obj);
    Py_DECREF (float_obj);
    return 1;
}

static int
_vector_find_string_helper(PyObject *str_obj, const char *substr,
                           Py_ssize_t start, Py_ssize_t end)
{
    /* This is implemented using Python's Unicode C-API rather than
     * plain C to handle both char* and wchar_t* especially because
     * wchar.h was only added to the standard in 95 and we want to be
     * ISO C 90 compatible.
     */
    PyObject *substr_obj;
#if PY_VERSION_HEX < 0x02060000
    PyObject *tmp;
#endif
    Py_ssize_t pos;
#if PY_VERSION_HEX >= 0x02060000
    substr_obj = PyUnicode_FromString (substr);
#else
    tmp = PyString_FromString (substr);
    if (tmp == NULL) {
        return -2;
    }
    substr_obj = PyUnicode_FromObject (tmp);
    Py_DECREF (tmp);
#endif
    if (substr_obj == NULL) {
        return -2;
    }
    pos = PyUnicode_Find (str_obj, substr_obj, start, end, 1);
    Py_DECREF (substr_obj);
    return pos;
}

/* Extracts coords from a str/unicode Object <str>.
 * <delimiter> should be an array of <dim>+1 C-Strings of the strings before,
 *   between and after the coords.
 * The extracted numbers will be stored in <coords>.
 * return
 *    0 on success.
 *   -1 if conversion was unsuccessful
 *   -2 if an internal error occured and an exception was set
 */
static int
_vector_coords_from_string(PyObject *str, char **delimiter,
                           double *coords, Py_ssize_t dim)
{
    int error_code;
    Py_ssize_t i, start_pos, end_pos, length;
    PyObject *vector_string;
    vector_string = PyUnicode_FromObject (str);
    if (vector_string == NULL) {
        return -2;
    }
    length = PySequence_Length (vector_string);
    /* find the starting point of the first coordinate in the string */
    start_pos = _vector_find_string_helper(vector_string, delimiter[0],
                                           0, length);
    if (start_pos < 0) {
        return start_pos;
    }
    start_pos += strlen(delimiter[0]);
    for (i = 0; i < dim; i++) {
        /* find the end point of the current coordinate in the string */
        end_pos = _vector_find_string_helper(vector_string,
                                             delimiter[i+1],
                                             start_pos, length);
        if (end_pos < 0)
            return end_pos;
        /* try to convert the current coordinate */
        error_code = get_double_from_unicode_slice(vector_string,
                                                   start_pos, end_pos,
                                                   &coords[i]);
        if (error_code < 0) {
            return -2;
        }
        else if (error_code == 0) {
            return -1;
        }
        /* move starting point to the next coordinate */
        start_pos = end_pos + strlen(delimiter[i+1]);
    }
    return 0;
}


static PyMemberDef vector_members[] = {
    {"epsilon", T_DOUBLE, offsetof(PyVector, epsilon), 0,
     "small value used in comparisons"},
    {NULL}  /* Sentinel */
};


static PyObject*
PyVector_NEW(int dim)
{
    PyVector *vec;
    switch (dim) {
    case 2:
        vec = PyObject_New(PyVector, &PyVector2_Type);
        break;
    case 3:
        vec = PyObject_New(PyVector, &PyVector3_Type);
        break;
/*
    case 4:
        vec = PyObject_New(PyVector, &PyVector4_Type);
        break;
*/
    default:
        PyErr_SetString(PyExc_SystemError,
                        "Wrong internal call to PyVector_NEW.\n");
        return NULL;
    }

    if (vec != NULL) {
        vec->dim = dim;
        vec->epsilon = VECTOR_EPSILON;
        vec->coords = PyMem_New(double, dim);
        if (vec->coords == NULL) {
            Py_DECREF(vec);
            return PyErr_NoMemory();
        }
    }

    return (PyObject *)vec;
}

static void
vector_dealloc(PyVector* self)
{
    PyMem_Del(self->coords);
    Py_TYPE(self)->tp_free((PyObject*)self);
}






/**********************************************
 * Generic vector PyNumber emulation routines
 **********************************************/

static PyObject *
vector_generic_math(PyObject *o1, PyObject *o2, int op)
{
    int i, dim;
    double *vec_coords;
    double other_coords[VECTOR_MAX_SIZE];
    double tmp;
    PyObject *other;
    PyVector *vec, *ret = NULL;
    if (PyVector_Check(o1)) {
        vec = (PyVector *)o1;
        other = o2;
    }
    else {
        vec = (PyVector *)o2;
        other = o1;
        op |= OP_ARG_REVERSE;
    }
    dim = vec->dim;
    vec_coords = vec->coords;

    if (other == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }

    if (PyVectorCompatible_Check(other, dim)) {
        op |= OP_ARG_VECTOR;
        if (!PySequence_AsVectorCoords(other, other_coords, dim))
            return NULL;
    }
    else if (RealNumber_Check(other))
        op |= OP_ARG_NUMBER;
    else
        op |= OP_ARG_UNKNOWN;

    if (op != (OP_MUL | OP_ARG_VECTOR) &&
        op != (OP_MUL | OP_ARG_VECTOR | OP_ARG_REVERSE)) {
        ret = (PyVector*)PyVector_NEW(dim);
        if (ret == NULL)
            return NULL;
    }

    switch (op) {
    case OP_ADD | OP_ARG_VECTOR:
    case OP_ADD | OP_ARG_VECTOR | OP_ARG_REVERSE:
        for (i = 0; i < dim; i++)
            ret->coords[i] = vec_coords[i] + other_coords[i];
        break;
    case OP_SUB | OP_ARG_VECTOR:
        for (i = 0; i < dim; i++)
            ret->coords[i] = vec_coords[i] - other_coords[i];
        break;
    case OP_SUB | OP_ARG_VECTOR | OP_ARG_REVERSE:
        for (i = 0; i < dim; i++)
            ret->coords[i] = other_coords[i] - vec_coords[i];
        break;
    case OP_MUL | OP_ARG_VECTOR:
    case OP_MUL | OP_ARG_VECTOR | OP_ARG_REVERSE:
        tmp = 0.;
        for (i = 0; i < dim; i++)
            tmp += vec_coords[i] * other_coords[i];
        ret = (PyVector*)PyFloat_FromDouble(tmp);
        break;
    case OP_MUL | OP_ARG_NUMBER:
    case OP_MUL | OP_ARG_NUMBER | OP_ARG_REVERSE:
        tmp = PyFloat_AsDouble(other);
        for (i = 0; i < dim; i++)
            ret->coords[i] = vec_coords[i] * tmp;
        break;
    case OP_DIV | OP_ARG_NUMBER:
        tmp = PyFloat_AsDouble(other);
        if (tmp == 0.) {
            PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
            Py_DECREF(ret);
            return NULL;
        }
        tmp = 1. / tmp;
        for (i = 0; i < dim; i++)
            ret->coords[i] = vec_coords[i] * tmp;
        break;
    case OP_FLOOR_DIV | OP_ARG_NUMBER:
        tmp = PyFloat_AsDouble(other);
        if (tmp == 0.) {
            PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
            Py_DECREF(ret);
            return NULL;
        }
        tmp = 1. / tmp;
        for (i = 0; i < dim; i++)
            ret->coords[i] = floor(vec_coords[i] * tmp);
        break;
    default:
        Py_XDECREF(ret);
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }
    return (PyObject*)ret;
}


static PyObject *
vector_add(PyObject *o1, PyObject *o2)
{
    return vector_generic_math(o1, o2, OP_ADD);
}
static PyObject *
vector_sub(PyObject *o1, PyObject *o2)
{
    return vector_generic_math(o1, o2, OP_SUB);
}
static PyObject *
vector_mul(PyObject *o1, PyObject *o2)
{
    return vector_generic_math(o1, o2, OP_MUL);
}
static PyObject *
vector_div(PyVector *o1, PyObject *o2)
{
    return vector_generic_math((PyObject*)o1, o2, OP_DIV);
}
static PyObject *
vector_floor_div(PyVector *o1, PyObject *o2)
{
    return vector_generic_math((PyObject*)o1, o2, OP_FLOOR_DIV);
}

static PyObject *
vector_neg(PyVector *self)
{
    int i;
    PyVector *ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret != NULL) {
        for (i = 0; i < self->dim; i++) {
            ret->coords[i] = -self->coords[i];
        }
    }
    return (PyObject*)ret;
}

static PyObject *
vector_pos(PyVector *self)
{
    PyVector *ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret != NULL) {
        memcpy(ret->coords, self->coords, sizeof(ret->coords[0]) * ret->dim);
    }
    return (PyObject*)ret;
}

static int
vector_nonzero(PyVector *self)
{
    int i;
    for (i = 0; i < self->dim; i++) {
        if (self->coords[i] != 0) {
            return 1;
        }
    }
    return 0;
}

static PyNumberMethods vector_as_number = {
    (binaryfunc)vector_add,         /* nb_add;       __add__ */
    (binaryfunc)vector_sub,         /* nb_subtract;  __sub__ */
    (binaryfunc)vector_mul,         /* nb_multiply;  __mul__ */
#if !PY3
    (binaryfunc)vector_div,         /* nb_divide;    __div__ */
#endif
    (binaryfunc)0,                  /* nb_remainder; __mod__ */
    (binaryfunc)0,                  /* nb_divmod;    __divmod__ */
    (ternaryfunc)0,                 /* nb_power;     __pow__ */
    (unaryfunc)vector_neg,          /* nb_negative;  __neg__ */
    (unaryfunc)vector_pos,          /* nb_positive;  __pos__ */
    (unaryfunc)0,                   /* nb_absolute;  __abs__ */
    (inquiry)vector_nonzero,        /* nb_nonzero;   __nonzero__ */
    (unaryfunc)0,                   /* nb_invert;    __invert__ */
    (binaryfunc)0,                  /* nb_lshift;    __lshift__ */
    (binaryfunc)0,                  /* nb_rshift;    __rshift__ */
    (binaryfunc)0,                  /* nb_and;       __and__ */
    (binaryfunc)0,                  /* nb_xor;       __xor__ */
    (binaryfunc)0,                  /* nb_or;        __or__ */
#if !PY3
    (coercion)0,                    /* nb_coerce;    __coerce__ */
#endif
    (unaryfunc)0,                   /* nb_int;       __int__ */
    (unaryfunc)0,                   /* nb_long;      __long__ */
    (unaryfunc)0,                   /* nb_float;     __float__ */
#if !PY3
    (unaryfunc)0,                   /* nb_oct;       __oct__ */
    (unaryfunc)0,                   /* nb_hex;       __hex__ */
#endif
    /* Added in release 2.0 */
    (binaryfunc)0,                  /* nb_inplace_add;       __iadd__ */
    (binaryfunc)0,                  /* nb_inplace_subtract;  __isub__ */
    (binaryfunc)0,                  /* nb_inplace_multiply;  __imul__ */
#if !PY3
    (binaryfunc)0,                  /* nb_inplace_divide;    __idiv__ */
#endif
    (binaryfunc)0,                  /* nb_inplace_remainder; __imod__ */
    (ternaryfunc)0,                 /* nb_inplace_power;     __pow__ */
    (binaryfunc)0,                  /* nb_inplace_lshift;    __ilshift__ */
    (binaryfunc)0,                  /* nb_inplace_rshift;    __irshift__ */
    (binaryfunc)0,                  /* nb_inplace_and;       __iand__ */
    (binaryfunc)0,                  /* nb_inplace_xor;       __ixor__ */
    (binaryfunc)0,                  /* nb_inplace_or;        __ior__ */

    /* Added in release 2.2 */
    (binaryfunc)vector_floor_div,   /* nb_floor_divide;         __floor__ */
    (binaryfunc)vector_div,         /* nb_true_divide;          __truediv__ */
    (binaryfunc)0,                  /* nb_inplace_floor_divide; __ifloor__ */
    (binaryfunc)0,                  /* nb_inplace_true_divide;  __itruediv__ */
};



/*************************************************
 * Generic vector PySequence emulation routines
 *************************************************/

static Py_ssize_t
vector_len(PyVector *self)
{
    return (Py_ssize_t)self->dim;
}

static PyObject *
vector_GetItem(PyVector *self, Py_ssize_t index)
{
    if (index < 0 || index >= self->dim) {
        PyErr_SetString(PyExc_IndexError, "subscript out of range.");
        return NULL;
    }
    return PyFloat_FromDouble(self->coords[index]);
}

static PyObject *
vector_GetSlice(PyVector *self, Py_ssize_t ilow, Py_ssize_t ihigh)
{
    /* some code was taken from the CPython source listobject.c */
    PyListObject *slice;
    Py_ssize_t i, len;
    PyObject **dest;

    /* make sure boundaries are sane */
    if (ilow < 0)
        ilow = 0;
    else if (ilow > self->dim)
        ilow = self->dim;
    if (ihigh < ilow)
        ihigh = ilow;
    else if (ihigh > self->dim)
        ihigh = self->dim;

    len = ihigh - ilow;
    slice = (PyListObject *) PyList_New(len);
    if (slice == NULL)
        return NULL;

    dest = slice->ob_item;
    for (i = 0; i < len; i++) {
        dest[i] = PyFloat_FromDouble(self->coords[ilow + i]);
    }
    return (PyObject *)slice;
}


static PySequenceMethods vector_as_sequence = {
    (lenfunc)vector_len,             /* sq_length;    __len__ */
    (binaryfunc)0,                   /* sq_concat;    __add__ */
    (ssizeargfunc)0,                 /* sq_repeat;    __mul__ */
    (ssizeargfunc)vector_GetItem,    /* sq_item;      __getitem__ */
    (ssizessizeargfunc)vector_GetSlice, /* sq_slice;     __getslice__ */
    (ssizeobjargproc)0,              /* sq_ass_item;  __setitem__ */
    (ssizessizeobjargproc)0,         /* sq_ass_slice; __setslice__ */
};


/***************************************************************************
 * Generic vector PyMapping emulation routines to support extended slicing
 ***************************************************************************/

/* Great parts of this function are adapted from python's list_subscript */
static PyObject*
vector_subscript (PyVector *self, PyObject *key)
{
    Py_ssize_t i;
#if PY_VERSION_HEX >= 0x02050000
    if (PyIndex_Check (key)) {
        i = PyNumber_AsSsize_t (key, PyExc_IndexError);
#else
    if (PyInt_Check (key) || PyLong_Check (key)) {
        if (PyInt_Check (key))
            i = PyInt_AsLong (key);
        else
            i = PyLong_AsLong (key);
#endif
        if (i == -1 && PyErr_Occurred())
            return NULL;
        if (i < 0)
            i += self->dim;
        return vector_GetItem (self, i);
    }
    else if (PySlice_Check (key)) {
        Py_ssize_t start, stop, step, slicelength, cur;
        PyObject *result;
        PyObject *it;
        PyObject **dest;

        if (PySlice_GetIndicesEx ((PySliceObject*)key, self->dim,
                 &start, &stop, &step, &slicelength) < 0) {
            return NULL;
        }

        if (slicelength <= 0) {
            return PyList_New(0);
        }
        else if (step == 1) {
            return vector_GetSlice (self, start, stop);
        }
        else {
            result = PyList_New (slicelength);
            if (!result)
                return NULL;

            dest = ((PyListObject *)result)->ob_item;
            for (cur = start, i = 0; i < slicelength; cur += step, i++) {
                it = PyFloat_FromDouble (self->coords[cur]);
                if (it == NULL) {
                    Py_DECREF (result);
                    return NULL;
                }
                dest[i] = it;
            }
            return result;
        }
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "vector indices must be integers, not %.200s",
                     key->ob_type->tp_name);
        return NULL;
    }
}

static PyMappingMethods vector_as_mapping = {
    (lenfunc)vector_len,                 /* mp_length */
    (binaryfunc)vector_subscript,        /* mp_subscript */
    (objobjargproc)0                     /* mp_ass_subscript */
};



static PyObject*
vector_getx (PyVector *self, void *closure)
{
    return PyFloat_FromDouble(self->coords[0]);
}

static PyObject*
vector_gety (PyVector *self, void *closure)
{
    return PyFloat_FromDouble(self->coords[1]);
}

static PyObject*
vector_getz (PyVector *self, void *closure)
{
    return PyFloat_FromDouble(self->coords[2]);
}

#ifdef PYGAME_MATH_VECTOR_HAVE_W
static PyObject*
vector_getw (PyVector *self, void *closure)
{
    return PyFloat_FromDouble(self->coords[3]);
}
#endif



static PyObject *
vector_richcompare(PyObject *o1, PyObject *o2, int op)
{
    int i;
    double diff;
    double other_coords[VECTOR_MAX_SIZE];
    PyVector *vec;
    PyObject *other;

    if (PyVector_Check(o1)) {
        vec = (PyVector*)o1;
        other = o2;
    }
    else {
        vec = (PyVector*)o2;
        other = o1;
    }
    if (!PyVectorCompatible_Check(other, vec->dim)) {
        if (op == Py_EQ)
            Py_RETURN_FALSE;
        else if (op == Py_NE)
            Py_RETURN_TRUE;
        else {
            Py_INCREF(Py_NotImplemented);
            return Py_NotImplemented;
        }
    }

    if (!PySequence_AsVectorCoords(other, other_coords, vec->dim)) {
        return NULL;
    }

    switch(op) {
    case Py_EQ:
        for (i = 0; i < vec->dim; i++) {
            diff = vec->coords[i] - other_coords[i];
            /* test diff != diff to catch NaN */
            if ((diff != diff) || (fabs(diff) >= vec->epsilon)) {
                Py_RETURN_FALSE;
            }
        }
        Py_RETURN_TRUE;
    case Py_NE:
        for (i = 0; i < vec->dim; i++) {
            diff = vec->coords[i] - other_coords[i];
            if ((diff != diff) || (fabs(diff) >= vec->epsilon)) {
                Py_RETURN_TRUE;
            }
        }
        Py_RETURN_FALSE;
    default:
        PyErr_SetString(PyExc_TypeError,
                        "This operation is not supported by vectors");
        return NULL;
    }
}








static PyObject *
vector_length(PyVector *self)
{
    double length_squared = _scalar_product(self->coords, self->coords,
                                            self->dim);
    return PyFloat_FromDouble(sqrt(length_squared));
}

static PyObject *
vector_length_squared(PyVector *self)
{
    double length_squared = _scalar_product(self->coords, self->coords,
                                            self->dim);
    return PyFloat_FromDouble(length_squared);
}

static PyObject *
vector_normalize(PyVector *self)
{
    PyVector *ret;
    int i;
    double length;

    length = sqrt(_scalar_product(self->coords, self->coords, self->dim));

    if (length < self->epsilon) {
        PyErr_SetString(PyExc_ValueError,
                        "Can't normalize Vector of length Zero");
        return NULL;
    }

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }

    for (i = 0; i < self->dim; ++i)
        ret->coords[i] = self->coords[i] / length;

    return (PyObject*)ret;
}

static PyObject *
vector_is_normalized(PyVector *self)
{
    double length_squared = _scalar_product(self->coords, self->coords,
                                            self->dim);
    if (fabs(length_squared - 1) < self->epsilon)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyObject *
vector_dot(PyVector *self, PyObject *other)
{
    double other_coords[VECTOR_MAX_SIZE];
    if (!PySequence_AsVectorCoords(other, other_coords, self->dim)) {
        PyErr_SetString(PyExc_TypeError,
                        "Cannot perform dot product with this type.");
        return NULL;
    }
    return PyFloat_FromDouble(_scalar_product(self->coords, other_coords,
                                              self->dim));
}

static PyObject *
vector_scale_to_length(PyVector *self, PyObject *length)
{
    int i;
    double new_length, old_length;
    double fraction;
    PyVector *ret;

    new_length = PyFloat_AsDouble(length);
    if (PyErr_Occurred()) {
        return NULL;
    }

    old_length = sqrt(_scalar_product(self->coords, self->coords, self->dim));

    if (old_length < self->epsilon) {
        PyErr_SetString(PyExc_ValueError, "Cannot scale a vector with zero length");
        return NULL;
    }

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }

    fraction = new_length / old_length;
    for (i = 0; i < self->dim; ++i)
        ret->coords[i] = self->coords[i] * fraction;

    return (PyObject*)ret;
}

static PyObject *
vector_slerp(PyVector *self, PyObject *args)
{
    int i;
    PyObject *other;
    PyVector *ret;
    double other_coords[VECTOR_MAX_SIZE];
    double tmp, angle, t, length1, length2, f0, f1, f2;

    if (!PyArg_ParseTuple(args, "Od:slerp", &other, &t)) {
        return NULL;
    }
    if (!PySequence_AsVectorCoords(other, other_coords, self->dim)) {
        PyErr_SetString(PyExc_TypeError, "Argument 1 must be a vector.");
        return NULL;
    }
    if (fabs(t) > 1) {
        PyErr_SetString(PyExc_ValueError, "Argument 2 must be in range [-1, 1].");
        return NULL;
    }

    length1 = sqrt(_scalar_product(self->coords, self->coords, self->dim));
    length2 = sqrt(_scalar_product(other_coords, other_coords, self->dim));
    if ((length1 < self->epsilon) || (length2 < self->epsilon)) {
        PyErr_SetString(PyExc_ValueError,
                        "can't use slerp with Zero-Vector");
        return NULL;
    }
    tmp = (_scalar_product(self->coords, other_coords, self->dim) /
           (length1 * length2));
    /* make sure tmp is in the range [-1:1] so acos won't return NaN */
    tmp = (tmp < -1 ? -1 : (tmp > 1 ? 1: tmp));
    angle = acos(tmp);

    /* if t < 0 we take the long arch of the greate circle to the destiny */
    if (t < 0) {
        angle -= 2 * M_PI;
        t = -t;
    }
    if (self->coords[0] * other_coords[1] < self->coords[1] * other_coords[0])
        angle *= -1;

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }
    /* special case angle==0 and angle==360 */
    if ((fabs(angle) < self->epsilon) ||
        (fabs(fabs(angle) - 2 * M_PI) < self->epsilon)) {
        /* approximate with lerp, because slerp diverges with 1/sin(angle) */
        for (i = 0; i < self->dim; ++i)
            ret->coords[i] = self->coords[i] * (1 - t) + other_coords[i] * t;
    }
    /* special case angle==180 and angle==-180 */
    else if (fabs(fabs(angle) - M_PI) < self->epsilon) {
        PyErr_SetString(PyExc_ValueError,
                        "SLERP with 180 degrees is undefined.");
        Py_DECREF(ret);
        return NULL;
    }
    else {
        f0 = ((length2 - length1) * t + length1) / sin(angle);
        f1 = sin(angle * (1-t)) / length1;
        f2 = sin(angle * t) / length2;
        for (i = 0; i < self->dim; ++i) {
            ret->coords[i] = (self->coords[i] * f1 + other_coords[i] * f2) * f0;
        }
    }
    return (PyObject*)ret;
}

static PyObject *
vector_lerp(PyVector *self, PyObject *args)
{
    int i;
    PyObject *other;
    PyVector *ret;
    double t;
    double other_coords[VECTOR_MAX_SIZE];

    if (!PyArg_ParseTuple(args, "Od:Vector.lerp", &other, &t)) {
        return NULL;
    }
    if (!PySequence_AsVectorCoords(other, other_coords, self->dim)) {
        PyErr_SetString(PyExc_TypeError, "Expected Vector as argument 1");
        return NULL;
    }
    if (t < 0 || t > 1) {
        PyErr_SetString(PyExc_ValueError, "Argument 2 must be in range [0, 1]");
        return NULL;
    }

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }
    for (i = 0; i < self->dim; ++i)
        ret->coords[i] = self->coords[i] * (1 - t) + other_coords[i] * t;
    return (PyObject *)ret;
}

static int
_vector_reflect_helper(double *dst_coords, const double *src_coords,
                       PyObject *normal, int dim, double epsilon)
{
    int i;
    double dot_product, norm_length;
    double norm_coords[VECTOR_MAX_SIZE];

    /* normalize the normal */
    if (!PySequence_AsVectorCoords(normal, norm_coords, dim))
        return 0;

    norm_length = _scalar_product(norm_coords, norm_coords, dim);

    if (norm_length < epsilon) {
        PyErr_SetString(PyExc_ValueError, "Normal must not be of length zero.");
        return 0;
    }
    if (norm_length != 1) {
        norm_length = sqrt(norm_length);
        for (i = 0; i < dim; ++i)
            norm_coords[i] /= norm_length;
    }

    /* calculate the dot_product for the projection */
    dot_product = _scalar_product(src_coords, norm_coords, dim);

    for (i = 0; i < dim; ++i)
        dst_coords[i] = src_coords[i] - 2 * norm_coords[i] * dot_product;
    return 1;
}

static PyObject *
vector_reflect(PyVector *self, PyObject *normal)
{
    PyVector *ret = (PyVector *)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }

    if (!_vector_reflect_helper(ret->coords, self->coords,
                                normal, self->dim, self->epsilon)) {
        return NULL;
    }
    return (PyObject *)ret;
}

static double
_vector_distance_helper(PyVector *self, PyObject *other)
{
    int i;
    double distance_squared, tmp;

    distance_squared = 0;
    for (i = 0; i < self->dim; ++i) {
        tmp = PySequence_GetItem_AsDouble(other, i) - self->coords[i];
        distance_squared += tmp * tmp;
    }
    /* PySequence_GetItem_AsDouble can fail in which case it will set an Err */
    if (PyErr_Occurred())
        return -1;

    return distance_squared;
}

static PyObject *
vector_distance_to(PyVector *self, PyObject *other)
{
    double distance_squared = _vector_distance_helper(self, other);
    if (distance_squared < 0 && PyErr_Occurred())
        return NULL;
    return PyFloat_FromDouble(sqrt(distance_squared));
}

static PyObject *
vector_distance_squared_to(PyVector *self, PyObject *other)
{
    double distance_squared = _vector_distance_helper(self, other);
    if (distance_squared < 0 && PyErr_Occurred())
        return NULL;
    return PyFloat_FromDouble(distance_squared);
}


static int
_vector_check_snprintf_success(int return_code)
{
    if (return_code < 0) {
        PyErr_SetString(PyExc_SystemError, "internal snprintf call went wrong! Please report this to pygame-users@seul.org");
        return 0;
    }
    if (return_code >= STRING_BUF_SIZE) {
        PyErr_SetString(PyExc_SystemError, "Internal buffer to small for snprintf! Please report this to pygame-users@seul.org");
        return 0;
    }
    return 1;
}

static PyObject *
vector_repr(PyVector *self)
{
    int i, tmp;
    int bufferIdx;
    char buffer[2][STRING_BUF_SIZE];

    bufferIdx = 1;
    tmp = PyOS_snprintf(buffer[0], STRING_BUF_SIZE, "<Vector%d(", self->dim);
    if (!_vector_check_snprintf_success(tmp))
        return NULL;
    for (i = 0; i < self->dim - 1; ++i) {
        tmp = PyOS_snprintf(buffer[bufferIdx % 2], STRING_BUF_SIZE, "%s%g, ",
                            buffer[(bufferIdx + 1) % 2], self->coords[i]);
        bufferIdx++;
        if (!_vector_check_snprintf_success(tmp))
            return NULL;
    }
    tmp = PyOS_snprintf(buffer[bufferIdx % 2], STRING_BUF_SIZE, "%s%g)>",
                        buffer[(bufferIdx + 1) % 2], self->coords[i]);
    if (!_vector_check_snprintf_success(tmp))
        return NULL;
    return Text_FromUTF8(buffer[bufferIdx % 2]);
}

static PyObject *
vector_str(PyVector *self)
{
    int i, tmp;
    int bufferIdx;
    char buffer[2][STRING_BUF_SIZE];

    bufferIdx = 1;
    tmp = PyOS_snprintf(buffer[0], STRING_BUF_SIZE, "[");
    if (!_vector_check_snprintf_success(tmp))
        return NULL;
    for (i = 0; i < self->dim - 1; ++i) {
        tmp = PyOS_snprintf(buffer[bufferIdx % 2], STRING_BUF_SIZE, "%s%g, ",
                            buffer[(bufferIdx + 1) % 2], self->coords[i]);
        bufferIdx++;
        if (!_vector_check_snprintf_success(tmp))
            return NULL;
    }
    tmp = PyOS_snprintf(buffer[bufferIdx % 2], STRING_BUF_SIZE, "%s%g]",
                        buffer[(bufferIdx + 1) % 2], self->coords[i]);
    if (!_vector_check_snprintf_success(tmp))
        return NULL;
    return Text_FromUTF8(buffer[bufferIdx % 2]);
}


/* This method first tries normal attribute access. If successful we're
 * done. If not we try swizzling. Here we have 3 different outcomes:
 *  1) swizzling works. we return the result as a tuple
 *  2) swizzling fails because it wasn't a valid swizzle. we return the
 *     original AttributeError
 *  3) swizzling fails due to some internal error. we return this error.
 */
static PyObject*
vector_getAttr_swizzle(PyVector *self, PyObject *attr_name)
{
    double *coords;
    Py_ssize_t i, idx, len;
    PyObject *err_type, *err_value, *err_traceback;
    PyObject *attr_unicode = NULL;
    Py_UNICODE *attr = NULL;
    PyObject *res = PyObject_GenericGetAttr((PyObject*)self, attr_name);
    /* if normal lookup failed try to swizzle */
    if (swizzling_enabled &&
        PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_AttributeError)) {
        /* fetch the error so it can be restored in case swizzling fails */
        PyErr_Fetch(&err_type, &err_value, &err_traceback);
        len = PySequence_Length(attr_name);
        if (len < 0)
            goto swizzle_failed;
        coords = self->coords;
        attr_unicode = PyUnicode_FromObject(attr_name);
        if (attr_unicode == NULL)
            goto swizzle_failed;
        attr = PyUnicode_AsUnicode(attr_unicode);
        if (attr == NULL)
            goto internal_error;
        res = (PyObject*)PyTuple_New(len);
        if (res == NULL)
            goto internal_error;
        for (i = 0; i < len; i++) {
            switch (attr[i]) {
            case 'x':
            case 'y':
            case 'z':
                idx = attr[i] - 'x';
                break;
            case 'w':
                idx = 3;
                break;
            default:
                goto swizzle_failed;
            }
            if (idx < self->dim) {
                if (PyTuple_SetItem(res, i, PyFloat_FromDouble(coords[idx])) != 0)
                    goto internal_error;
            }
            else {
                goto swizzle_failed;
            }
        }
        /* swizzling succeeded! clear the error and return result */
        PyErr_Clear();
        Py_DECREF(attr_unicode);
    }
    return res;

    /* swizzling failed! restore the exception from PyObject_GenericGetAttr,
     * clean up and return NULL */
swizzle_failed:
    PyErr_Restore(err_type, err_value, err_traceback);
internal_error:
    Py_XDECREF(res);
    Py_XDECREF(attr_unicode);
    return NULL;
}


#if 0
static Py_ssize_t
vector_readbuffer(PyVector *self, Py_ssize_t segment, void **ptrptr)
{
    if (segment != 0) {
        PyErr_SetString(PyExc_SystemError,
                        "accessing non-existent vector segment");
        return -1;
    }
    *ptrptr = self->coords;
    return self->dim;
}

static Py_ssize_t
vector_writebuffer(PyVector *self, Py_ssize_t segment, void **ptrptr)
{
    if (segment != 0) {
        PyErr_SetString(PyExc_SystemError,
                        "accessing non-existent vector segment");
        return -1;
    }
    *ptrptr = self->coords;
    return self->dim;
}

static Py_ssize_t
vector_segcount(PyVector *self, Py_ssize_t *lenp)
{
    if (lenp) {
        *lenp = self->dim * sizeof(self->coords[0]);
    }
    return 1;
}

static int
vector_getbuffer(PyVector *self, Py_buffer *view, int flags)
{
    int ret;
    void *ptr;
    if (view == NULL) {
        self->ob_exports++;
        return 0;
    }
    ptr = self->coords;
    ret = PyBuffer_FillInfo(view, (PyObject*)self, ptr, Py_SIZE(self), 0, flags);
    if (ret >= 0) {
        obj->ob_exports++;
    }
    return ret;
}

static void
vector_releasebuffer(PyVector *self, Py_buffer *view)
{
    self->ob_exports--;
}


static PyBufferProcs vector_as_buffer = {
    (readbufferproc)vector_readbuffer,
    (writebufferproc)vector_writebuffer,
    (segcountproc)vector_segcount,
    (charbufferproc)0,
    (getbufferproc)vector_getbuffer,
    (releasebufferproc)vector_releasebuffer,
};
#endif

/*********************************************************************
 * vector2 specific functions
 *********************************************************************/


static PyObject *
vector2_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyVector *vec = (PyVector *)type->tp_alloc(type, 0);

    if (vec != NULL) {
        vec->dim = 2;
        vec->epsilon = VECTOR_EPSILON;
        vec->coords = PyMem_New(double, vec->dim);
        if (vec->coords == NULL) {
            Py_TYPE(vec)->tp_free((PyObject*)vec);
            return NULL;
        }
    }

    return (PyObject *)vec;
}


static int
vector2_init(PyVector *self, PyObject *args, PyObject *kwds)
{
    PyObject *xOrSequence=NULL, *y=NULL;
    static char *kwlist[] = {"x", "y", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "|OO:Vector2", kwlist,
                                      &xOrSequence, &y))
        return -1;

    if (xOrSequence) {
        if (RealNumber_Check(xOrSequence)) {
            self->coords[0] = PyFloat_AsDouble(xOrSequence);
        }
        else if (PyVectorCompatible_Check (xOrSequence, self->dim)) {
            if (!PySequence_AsVectorCoords(xOrSequence, self->coords, 2))
                return -1;
            else
                return 0;
        }
#if PY3
        else if (PyUnicode_Check (xOrSequence)) {
#else
        else if (PyUnicode_Check (xOrSequence) ||
                 PyString_Check (xOrSequence)) {
#endif
            char *delimiter[3] = { "<Vector2(", ", ", ")>" };
            int error_code;
            error_code = _vector_coords_from_string(xOrSequence, delimiter,
                                                    self->coords, self->dim);
            if (error_code == -2) {
                return -1;
            }
            else if (error_code == -1) {
                goto error;
            }
            return 0;
        }
        else {
            goto error;
        }
    }
    else {
        self->coords[0] = 0.;
    }

    if (y) {
        if (RealNumber_Check(y)) {
            self->coords[1] = PyFloat_AsDouble(y);
        }
        else {
            goto error;
        }
    }
    else {
        self->coords[1] = 0.;
    }
    /* success initialization */
    return 0;
error:
    PyErr_SetString(PyExc_ValueError,
                    "Vector2 must be initialized with 2 real numbers or a sequence of 2 real numbers");
    return -1;
}

static int
_vector2_rotate_helper(double *dst_coords, const double *src_coords,
                       double angle, double epsilon)
{
    /* make sure angle is in range [0, 360) */
    angle = fmod(angle, 360.);
    if (angle < 0)
        angle += 360.;

    /* special-case rotation by 0, 90, 180 and 270 degrees */
    if (fmod(angle + epsilon, 90.) < 2 * epsilon) {
        switch ((int)((angle + epsilon) / 90)) {
        case 0: /* 0 degrees */
        case 4: /* 360 degree (see issue 214) */
            dst_coords[0] = src_coords[0];
            dst_coords[1] = src_coords[1];
            break;
        case 1: /* 90 degrees */
            dst_coords[0] = -src_coords[1];
            dst_coords[1] = src_coords[0];
            break;
        case 2: /* 180 degrees */
            dst_coords[0] = -src_coords[0];
            dst_coords[1] = -src_coords[1];
            break;
        case 3: /* 270 degrees */
            dst_coords[0] = src_coords[1];
            dst_coords[1] = -src_coords[0];
            break;
        default:
            /* this should NEVER happen and means a bug in the code */
            PyErr_SetString(PyExc_RuntimeError, "Please report this bug in vector2_rotate_helper to the developers at pygame-users@seul.org");
            return 0;
        }
    }
    else {
        double sinValue, cosValue;

        angle = DEG2RAD(angle);
        sinValue = sin(angle);
        cosValue = cos(angle);

        dst_coords[0] = cosValue * src_coords[0] - sinValue * src_coords[1];
        dst_coords[1] = sinValue * src_coords[0] + cosValue * src_coords[1];
    }
    return 1;
}

static PyObject *
vector2_rotate(PyVector *self, PyObject *args)
{
    double angle;
    PyVector *ret;

    if (!PyArg_ParseTuple(args, "d:rotate", &angle)) {
        return NULL;
    }

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL || !_vector2_rotate_helper(ret->coords, self->coords,
                                               angle, self->epsilon)) {
        Py_XDECREF(ret);
        return NULL;
    }
    return (PyObject*)ret;
}


static PyObject *
vector2_cross(PyVector *self, PyObject *other)
{
    double other_coords[2];

    if (!PyVectorCompatible_Check(other, self->dim)) {
        PyErr_SetString(PyExc_TypeError, "cannot calculate cross Product");
        return NULL;
    }

    if (!PySequence_AsVectorCoords(other, other_coords, 2)) {
        return NULL;
    }

    return PyFloat_FromDouble((self->coords[0] * other_coords[1]) -
                              (self->coords[1] * other_coords[0]));
}

static PyObject *
vector2_angle_to(PyVector *self, PyObject *other)
{
    double angle;
    double other_coords[2];

    if (!PyVectorCompatible_Check(other, self->dim)) {
        PyErr_SetString(PyExc_TypeError, "expected an vector.");
        return NULL;
    }

    if (!PySequence_AsVectorCoords(other, other_coords, 2)) {
        return NULL;
    }

    angle = (atan2(other_coords[1], other_coords[0]) -
             atan2(self->coords[1], self->coords[0]));
    return PyFloat_FromDouble(RAD2DEG(angle));
}

static PyObject *
vector2_as_polar(PyVector *self)
{
    double r, phi;
    r = sqrt(_scalar_product(self->coords, self->coords, self->dim));
    phi = RAD2DEG(atan2(self->coords[1], self->coords[0]));
    return Py_BuildValue("(dd)", r, phi);
}

/* class method */
static PyObject *
vector2_from_polar(PyObject *cls, PyObject *args)
{
    double r, phi;
    if (!PyArg_ParseTuple(args, "(dd):Vector2.from_polar", &r, &phi)) {
        return NULL;
    }
    phi = DEG2RAD(phi);
    return PyObject_CallFunction(cls, "dd", r * cos(phi), r * sin(phi));
}


static PyMethodDef vector2_methods[] = {
    {"length", (PyCFunction)vector_length, METH_NOARGS,
     DOC_VECTOR2LENGTH
    },
    {"length_squared", (PyCFunction)vector_length_squared, METH_NOARGS,
     DOC_VECTOR2LENGTHSQUARED
    },
    {"rotate", (PyCFunction)vector2_rotate, METH_VARARGS,
     DOC_VECTOR2ROTATE
    },
    {"slerp", (PyCFunction)vector_slerp, METH_VARARGS,
     DOC_VECTOR2SLERP
    },
    {"lerp", (PyCFunction)vector_lerp, METH_VARARGS,
     DOC_VECTOR2LERP
    },
    {"normalize", (PyCFunction)vector_normalize, METH_NOARGS,
     DOC_VECTOR2NORMALIZE
    },
    {"is_normalized", (PyCFunction)vector_is_normalized, METH_NOARGS,
     DOC_VECTOR2ISNORMALIZED
    },
    {"cross", (PyCFunction)vector2_cross, METH_O,
     DOC_VECTOR2CROSS
    },
    {"dot", (PyCFunction)vector_dot, METH_O,
     DOC_VECTOR2DOT
    },
    {"angle_to", (PyCFunction)vector2_angle_to, METH_O,
     DOC_VECTOR2ANGLETO
    },
    {"scale_to_length", (PyCFunction)vector_scale_to_length, METH_O,
     DOC_VECTOR2SCALETOLENGTH
    },
    {"reflect", (PyCFunction)vector_reflect, METH_O,
     DOC_VECTOR2REFLECT
    },
    {"distance_to", (PyCFunction)vector_distance_to, METH_O,
     DOC_VECTOR2DISTANCETO
    },
    {"distance_squared_to", (PyCFunction)vector_distance_squared_to, METH_O,
     DOC_VECTOR2DISTANCESQUAREDTO
    },
    {"elementwise", (PyCFunction)vector_elementwise, METH_NOARGS,
     DOC_VECTOR2ELEMENTWISE
    },
    {"as_polar", (PyCFunction)vector2_as_polar, METH_NOARGS,
     DOC_VECTOR2ASPOLAR
    },
    {"from_polar", (PyCFunction)vector2_from_polar, METH_VARARGS | METH_CLASS,
     DOC_VECTOR2FROMPOLAR
    },

    {NULL}  /* Sentinel */
};


static PyGetSetDef vector2_getsets[] = {
    { "x", (getter)vector_getx, NULL, DOC_VECTORX, NULL },
    { "y", (getter)vector_gety, NULL, DOC_VECTORY, NULL },
    { NULL, 0, NULL, NULL, NULL }  /* Sentinel */
};


/********************************
 * PyVector2 type definition
 ********************************/

static PyTypeObject PyVector2_Type = {
    TYPE_HEAD(NULL, 0)
    "pygame.math.Vector2",     /* tp_name */
    sizeof(PyVector),          /* tp_basicsize */
    0,                         /* tp_itemsize */
    /* Methods to implement standard operations */
    (destructor)vector_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    (reprfunc)vector_repr,     /* tp_repr */
    /* Method suites for standard classes */
    &vector_as_number,         /* tp_as_number */
    &vector_as_sequence,       /* tp_as_sequence */
    &vector_as_mapping,        /* tp_as_mapping */
    /* More standard operations (here for binary compatibility) */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    (reprfunc)vector_str,      /* tp_str */
    (getattrofunc)vector_getAttr_swizzle, /* tp_getattro */
    (setattrofunc)0,           /* tp_setattro */
    /* Functions to access object as input/output buffer */
    0,                         /* tp_as_buffer */
    /* Flags to define presence of optional/expanded features */
#if PY3
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
#else
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE |
    Py_TPFLAGS_CHECKTYPES, /* tp_flags */
#endif
    /* Documentation string */
    DOC_PYGAMEMATHVECTOR2,     /* tp_doc */

    /* Assigned meaning in release 2.0 */
    /* call function for all accessible objects */
    0,                         /* tp_traverse */
    /* delete references to contained objects */
    0,                         /* tp_clear */

    /* Assigned meaning in release 2.1 */
    /* rich comparisons */
    (richcmpfunc)vector_richcompare, /* tp_richcompare */
    /* weak reference enabler */
    0,                         /* tp_weaklistoffset */

    /* Added in release 2.2 */
    /* Iterators */
    vector_iter,               /* tp_iter */
    0,                         /* tp_iternext */
    /* Attribute descriptor and subclassing stuff */
    vector2_methods,           /* tp_methods */
    vector_members,            /* tp_members */
    vector2_getsets,           /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)vector2_init,    /* tp_init */
    0,                         /* tp_alloc */
    (newfunc)vector2_new,      /* tp_new */
    0,                         /* tp_free */
    0,                         /* tp_is_gc */
    0,                         /* tp_bases */
    0,                         /* tp_mro */
    0,                         /* tp_cache */
    0,                         /* tp_subclasses */
    0,                         /* tp_weaklist */
};










/*************************************************************
 *  PyVector3 specific functions
 *************************************************************/



static PyObject *
vector3_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyVector *vec = (PyVector *)type->tp_alloc(type, 0);

    if (vec != NULL) {
        vec->dim = 3;
        vec->epsilon = VECTOR_EPSILON;
        vec->coords = PyMem_New(double, vec->dim);
        if (vec->coords == NULL) {
            Py_TYPE(vec)->tp_free((PyObject*)vec);
            return NULL;
        }
    }

    return (PyObject *)vec;
}

static int
vector3_init(PyVector *self, PyObject *args, PyObject *kwds)
{
    PyObject *xOrSequence=NULL, *y=NULL, *z=NULL;
    static char *kwlist[] = {"x", "y", "z", NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "|OOO:Vector3", kwlist,
                                      &xOrSequence, &y, &z))
        return -1;

    if (xOrSequence) {
        if (RealNumber_Check(xOrSequence)) {
            self->coords[0] = PyFloat_AsDouble(xOrSequence);
        }
        else if (PyVectorCompatible_Check(xOrSequence, self->dim)) {
            if (!PySequence_AsVectorCoords(xOrSequence, self->coords, 3))
                return -1;
            else
                return 0;
        }
#if PY3
        else if (PyUnicode_Check (xOrSequence)) {
#else
        else if (PyUnicode_Check (xOrSequence) ||
                 PyString_Check (xOrSequence)) {
#endif
            char *delimiter[4] = { "<Vector3(", ", ", ", ", ")>" };
            int error_code;
            error_code = _vector_coords_from_string(xOrSequence, delimiter,
                                                    self->coords, self->dim);
            if (error_code == -2) {
                return -1;
            }
            else if (error_code == -1) {
                goto error;
            }
            return 0;
        }
        else {
            goto error;
        }
    }
    else {
        self->coords[0] = 0.;
    }

    if (y) {
        if (RealNumber_Check(y)) {
            self->coords[1] = PyFloat_AsDouble(y);
        }
        else {
            goto error;
        }
    }
    else {
        self->coords[1] = 0.;
    }

    if (z) {
        if (RealNumber_Check(z)) {
            self->coords[2] = PyFloat_AsDouble(z);
        }
        else {
            goto error;
        }
    }
    else {
        self->coords[2] = 0.;
    }
    /* success initialization */
    return 0;
error:
    PyErr_SetString(PyExc_ValueError,
                    "Vector3 must be initialized with 3 real numbers or a sequence of 3 real numbers");
    return -1;
}


static int
_vector3_rotate_helper(double *dst_coords, const double *src_coords,
                       const double *axis_coords,
                       double angle, double epsilon)
{
    double sinValue, cosValue, cosComplement;
    double normalizationFactor;
    double axisLength2 = 0;
    double axis[3];
    int i;

    /* make sure angle is in range [0, 360) */
    angle = fmod(angle, 360.);
    if (angle < 0)
        angle += 360.;

    for (i = 0; i < 3; ++i) {
        axisLength2 += axis_coords[i] * axis_coords[i];
        axis[i] = axis_coords[i];
    }

    /* Rotation axis may not be Zero */
    if (axisLength2 < epsilon) {
        PyErr_SetString(PyExc_ValueError, "Rotation Axis is to close to Zero");
        return 0;
    }

    /* normalize the axis */
    if (fabs(axisLength2 - 1) > epsilon) {
        normalizationFactor = 1. / sqrt(axisLength2);
        for (i = 0; i < 3; ++i)
            axis[i] *= normalizationFactor;
    }

    /* special-case rotation by 0, 90, 180 and 270 degrees */
    if (fmod(angle + epsilon, 90.) < 2 * epsilon) {
        switch ((int)((angle + epsilon) / 90)) {
        case 0: /* 0 degrees */
        case 4: /* 360 degrees (see issue 214) */
            memcpy(dst_coords, src_coords, 3 * sizeof(src_coords[0]));
            break;
        case 1: /* 90 degrees */
            dst_coords[0] = (src_coords[0] * (axis[0] * axis[0]) +
                             src_coords[1] * (axis[0] * axis[1] - axis[2]) +
                             src_coords[2] * (axis[0] * axis[2] + axis[1]));
            dst_coords[1] = (src_coords[0] * (axis[0] * axis[1] + axis[2]) +
                             src_coords[1] * (axis[1] * axis[1]) +
                             src_coords[2] * (axis[1] * axis[2] - axis[0]));
            dst_coords[2] = (src_coords[0] * (axis[0] * axis[2] - axis[1]) +
                             src_coords[1] * (axis[1] * axis[2] + axis[0]) +
                             src_coords[2] * (axis[2] * axis[2]));
            break;
        case 2: /* 180 degrees */
            dst_coords[0] = (src_coords[0] * (-1 + axis[0] * axis[0] * 2) +
                             src_coords[1] * (axis[0] * axis[1] * 2) +
                             src_coords[2] * (axis[0] * axis[2] * 2));
            dst_coords[1] = (src_coords[0] * (axis[0] * axis[1] * 2) +
                             src_coords[1] * (-1 + axis[1] * axis[1] * 2) +
                             src_coords[2] * (axis[1] * axis[2] * 2));
            dst_coords[2] = (src_coords[0] * (axis[0] * axis[2] * 2) +
                             src_coords[1] * (axis[1] * axis[2] * 2) +
                             src_coords[2] * (-1 + axis[2] * axis[2] * 2));
            break;
        case 3: /* 270 degrees */
            dst_coords[0] = (src_coords[0] * (axis[0] * axis[0]) +
                             src_coords[1] * (axis[0] * axis[1] + axis[2]) +
                             src_coords[2] * (axis[0] * axis[2] - axis[1]));
            dst_coords[1] = (src_coords[0] * (axis[0] * axis[1] - axis[2]) +
                             src_coords[1] * (axis[1] * axis[1]) +
                             src_coords[2] * (axis[1] * axis[2] + axis[0]));
            dst_coords[2] = (src_coords[0] * (axis[0] * axis[2] + axis[1]) +
                             src_coords[1] * (axis[1] * axis[2] - axis[0]) +
                             src_coords[2] * (axis[2] * axis[2]));
            break;
        default:
            /* this should NEVER happen and means a bug in the code */
            PyErr_SetString(PyExc_RuntimeError, "Please report this bug in vector3_rotate_helper to the developers at pygame-users@seul.org");
            return 0;
        }
    }
    else {
        angle = DEG2RAD(angle);
        sinValue = sin(angle);
        cosValue = cos(angle);
        cosComplement = 1 - cosValue;

        dst_coords[0] = (src_coords[0] * (cosValue + axis[0] * axis[0] * cosComplement) +
                         src_coords[1] * (axis[0] * axis[1] * cosComplement - axis[2] * sinValue) +
                         src_coords[2] * (axis[0] * axis[2] * cosComplement + axis[1] * sinValue));
        dst_coords[1] = (src_coords[0] * (axis[0] * axis[1] * cosComplement + axis[2] * sinValue) +
                         src_coords[1] * (cosValue + axis[1] * axis[1] * cosComplement) +
                         src_coords[2] * (axis[1] * axis[2] * cosComplement - axis[0] * sinValue));
        dst_coords[2] = (src_coords[0] * (axis[0] * axis[2] * cosComplement - axis[1] * sinValue) +
                         src_coords[1] * (axis[1] * axis[2] * cosComplement + axis[0] * sinValue) +
                         src_coords[2] * (cosValue + axis[2] * axis[2] * cosComplement));
    }
    return 1;
}

static PyObject *
vector3_rotate(PyVector *self, PyObject *args)
{
    PyVector *ret;
    PyObject *axis;
    double axis_coords[3];
    double angle;

    if (!PyArg_ParseTuple(args, "dO:rotate", &angle, &axis)) {
        return NULL;
    }
    if (!PyVectorCompatible_Check(axis, self->dim)) {
        PyErr_SetString(PyExc_TypeError, "axis must be a 3D Vector");
        return NULL;
    }
    if (!PySequence_AsVectorCoords(axis, axis_coords, 3)) {
        return NULL;
    }

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL || !_vector3_rotate_helper(ret->coords, self->coords,
                                               axis_coords, angle,
                                               self->epsilon)) {
        Py_XDECREF(ret);
        return NULL;
    }
    return (PyObject*)ret;
}


static PyObject *
vector3_rotate_x(PyVector *self, PyObject *angleObject)
{
    PyVector *ret;
    double sinValue, cosValue;
    double angle;

    angle = DEG2RAD(PyFloat_AsDouble(angleObject));
    if (PyErr_Occurred()) {
        return NULL;
    }
    sinValue = sin(angle);
    cosValue = cos(angle);

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }
    ret->coords[0] = self->coords[0];
    ret->coords[1] = self->coords[1] * cosValue - self->coords[2] * sinValue;
    ret->coords[2] = self->coords[1] * sinValue + self->coords[2] * cosValue;
    return (PyObject*)ret;
}

static PyObject *
vector3_rotate_y(PyVector *self, PyObject *angleObject)
{
    PyVector *ret;
    double sinValue, cosValue;
    double angle;

    angle = DEG2RAD(PyFloat_AsDouble(angleObject));
    if (PyErr_Occurred()) {
        return NULL;
    }
    sinValue = sin(angle);
    cosValue = cos(angle);

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }
    ret->coords[0] = self->coords[0] * cosValue + self->coords[2] * sinValue;
    ret->coords[1] = self->coords[1];
    ret->coords[2] = -self->coords[0] * sinValue + self->coords[2] * cosValue;

    return (PyObject*)ret;
}

static PyObject *
vector3_rotate_z(PyVector *self, PyObject *angleObject)
{
    PyVector *ret;
    double sinValue, cosValue;
    double angle;

    angle = DEG2RAD(PyFloat_AsDouble(angleObject));
    if (PyErr_Occurred()) {
        return NULL;
    }
    sinValue = sin(angle);
    cosValue = cos(angle);

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        return NULL;
    }
    ret->coords[0] = self->coords[0] * cosValue - self->coords[1] * sinValue;
    ret->coords[1] = self->coords[0] * sinValue + self->coords[1] * cosValue;
    ret->coords[2] = self->coords[2];

    return (PyObject*)ret;
}


static PyObject *
vector3_cross(PyVector *self, PyObject *other)
{
    PyVector *ret;
    double *ret_coords;
    double *self_coords;
    double *other_coords;

    if (!PyVectorCompatible_Check(other, self->dim)) {
        PyErr_SetString(PyExc_TypeError, "cannot calculate cross Product");
        return NULL;
    }

    self_coords = self->coords;
    if (PyVector_Check(other)) {
        other_coords = ((PyVector *)other)->coords;
    }
    else {
        other_coords = PyMem_New(double, self->dim);
        if (!PySequence_AsVectorCoords(other, other_coords, 3)) {
            PyMem_Del(other_coords);
            return NULL;
        }
    }

    ret = (PyVector*)PyVector_NEW(self->dim);
    if (ret == NULL) {
        if (!PyVector_Check(other))
            PyMem_Del(other_coords);
        return NULL;
    }
    ret_coords = ret->coords;
    ret_coords[0] = ((self_coords[1] * other_coords[2]) -
                     (self_coords[2] * other_coords[1]));
    ret_coords[1] = ((self_coords[2] * other_coords[0]) -
                     (self_coords[0] * other_coords[2]));
    ret_coords[2] = ((self_coords[0] * other_coords[1]) -
                     (self_coords[1] * other_coords[0]));

    if (!PyVector_Check(other))
        PyMem_Del(other_coords);

    return (PyObject*)ret;
}

static PyObject *
vector3_angle_to(PyVector *self, PyObject *other)
{
    double angle, tmp, squared_length1, squared_length2;
    double other_coords[VECTOR_MAX_SIZE];

    if (!PyVectorCompatible_Check(other, self->dim)) {
        PyErr_SetString(PyExc_TypeError, "expected an vector.");
        return NULL;
    }

    if (!PySequence_AsVectorCoords(other, other_coords, self->dim)) {
        return NULL;
    }
    squared_length1 = _scalar_product(self->coords, self->coords, self->dim);
    squared_length2 = _scalar_product(other_coords, other_coords, self->dim);
    tmp = sqrt(squared_length1 * squared_length2);
    if (tmp == 0) {
        PyErr_SetString(PyExc_ValueError, "angle to zero vector is undefined.");
        return NULL;
    }
    angle = acos(_scalar_product(self->coords, other_coords, self->dim) / tmp);
    return PyFloat_FromDouble(RAD2DEG(angle));
}

static PyObject *
vector3_as_spherical(PyVector *self)
{
    double r, theta, phi;
    r = sqrt(_scalar_product(self->coords, self->coords, self->dim));
    if (r == 0.) {
        return Py_BuildValue("(ddd)", 0., 0., 0.);
    }
    theta = RAD2DEG(acos(self->coords[2] / r));
    phi = RAD2DEG(atan2(self->coords[1], self->coords[0]));
    return Py_BuildValue("(ddd)", r, theta, phi);
}

static PyObject *
vector3_from_spherical(PyObject *cls, PyObject *args)
{
    double r, theta, phi;

    if (!PyArg_ParseTuple(args, "(ddd):Vector3.from_spherical",
                          &r, &theta, &phi)) {
        return NULL;
    }
    theta = DEG2RAD(theta);
    phi = DEG2RAD(phi);
    return PyObject_CallFunction(cls, "ddd", r * sin(theta) * cos(phi),
                                 r * sin(theta) * sin(phi), r * cos(theta));

/*    new_args = Py_BuildValue("(ddd)", r * sin(theta) * cos(phi),
                             r * sin(theta) * sin(phi), r * cos(theta));
    if (!new_args) {
        return NULL;
    }
    ret = PyInstance_New(cls, new_args, NULL);
//    ret = PyType_GenericNew(cls, new_args, NULL);
    Py_XDECREF(new_args);
    return ret;*/
}


static PyMethodDef vector3_methods[] = {
    {"length", (PyCFunction)vector_length, METH_NOARGS,
     DOC_VECTOR3LENGTH
    },
    {"length_squared", (PyCFunction)vector_length_squared, METH_NOARGS,
     DOC_VECTOR3LENGTHSQUARED
    },
    {"rotate", (PyCFunction)vector3_rotate, METH_VARARGS,
     DOC_VECTOR3ROTATE
    },
    {"rotate_x", (PyCFunction)vector3_rotate_x, METH_O,
     DOC_VECTOR3ROTATEX
    },
    {"rotate_y", (PyCFunction)vector3_rotate_y, METH_O,
     DOC_VECTOR3ROTATEY
    },
    {"rotate_z", (PyCFunction)vector3_rotate_z, METH_O,
     DOC_VECTOR3ROTATEZ
    },
    {"slerp", (PyCFunction)vector_slerp, METH_VARARGS,
     DOC_VECTOR3SLERP
    },
    {"lerp", (PyCFunction)vector_lerp, METH_VARARGS,
     DOC_VECTOR3LERP
    },
    {"normalize", (PyCFunction)vector_normalize, METH_NOARGS,
     DOC_VECTOR3NORMALIZE
    },
    {"is_normalized", (PyCFunction)vector_is_normalized, METH_NOARGS,
     DOC_VECTOR3ISNORMALIZED
    },
    {"cross", (PyCFunction)vector3_cross, METH_O,
     DOC_VECTOR3CROSS
    },
    {"dot", (PyCFunction)vector_dot, METH_O,
     DOC_VECTOR3DOT
    },
    {"angle_to", (PyCFunction)vector3_angle_to, METH_O,
     DOC_VECTOR3ANGLETO
    },
    {"scale_to_length", (PyCFunction)vector_scale_to_length, METH_O,
     DOC_VECTOR3SCALETOLENGTH
    },
    {"reflect", (PyCFunction)vector_reflect, METH_O,
     DOC_VECTOR3REFLECT
    },
    {"distance_to", (PyCFunction)vector_distance_to, METH_O,
     DOC_VECTOR3DISTANCETO
    },
    {"distance_squared_to", (PyCFunction)vector_distance_squared_to, METH_O,
     DOC_VECTOR3DISTANCESQUAREDTO
    },
    {"elementwise", (PyCFunction)vector_elementwise, METH_NOARGS,
     DOC_VECTOR3ELEMENTWISE
    },
    {"as_spherical", (PyCFunction)vector3_as_spherical, METH_NOARGS,
     DOC_VECTOR3ASSPHERICAL
    },
    {"from_spherical", (PyCFunction)vector3_from_spherical, METH_VARARGS | METH_CLASS,
     DOC_VECTOR3FROMSPHERICAL
    },

    {NULL}  /* Sentinel */
};


static PyGetSetDef vector3_getsets[] = {
    { "x", (getter)vector_getx, NULL, DOC_VECTORX, NULL },
    { "y", (getter)vector_gety, NULL, DOC_VECTORY, NULL },
    { "z", (getter)vector_getz, NULL, DOC_VECTORZ, NULL },
    { NULL, 0, NULL, NULL, NULL }  /* Sentinel */
};


/********************************
 * PyVector3 type definition
 ********************************/

static PyTypeObject PyVector3_Type = {
    TYPE_HEAD (NULL, 0)
    "pygame.math.Vector3",     /* tp_name */
    sizeof(PyVector),          /* tp_basicsize */
    0,                         /* tp_itemsize */
    /* Methods to implement standard operations */
    (destructor)vector_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    (reprfunc)vector_repr,     /* tp_repr */
    /* Method suites for standard classes */
    &vector_as_number,         /* tp_as_number */
    &vector_as_sequence,       /* tp_as_sequence */
    &vector_as_mapping,        /* tp_as_mapping */
    /* More standard operations (here for binary compatibility) */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    (reprfunc)vector_str,      /* tp_str */
    (getattrofunc)vector_getAttr_swizzle, /* tp_getattro */
    (setattrofunc)0,           /* tp_setattro */
    /* Functions to access object as input/output buffer */
    0,                         /* tp_as_buffer */
    /* Flags to define presence of optional/expanded features */
#if PY3
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
#else
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE |
    Py_TPFLAGS_CHECKTYPES, /* tp_flags */
#endif
    /* Documentation string */
    DOC_PYGAMEMATHVECTOR3,         /* tp_doc */

    /* Assigned meaning in release 2.0 */
    /* call function for all accessible objects */
    0,                         /* tp_traverse */
    /* delete references to contained objects */
    0,                         /* tp_clear */

    /* Assigned meaning in release 2.1 */
    /* rich comparisons */
    (richcmpfunc)vector_richcompare, /* tp_richcompare */
    /* weak reference enabler */
    0,                         /* tp_weaklistoffset */

    /* Added in release 2.2 */
    /* Iterators */
    vector_iter,               /* tp_iter */
    0,                         /* tp_iternext */
    /* Attribute descriptor and subclassing stuff */
    vector3_methods,           /* tp_methods */
    vector_members,            /* tp_members */
    vector3_getsets,           /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)vector3_init,    /* tp_init */
    0,                         /* tp_alloc */
    (newfunc)vector3_new,      /* tp_new */
    0,                         /* tp_free */
    0,                         /* tp_is_gc */
    0,                         /* tp_bases */
    0,                         /* tp_mro */
    0,                         /* tp_cache */
    0,                         /* tp_subclasses */
    0,                         /* tp_weaklist */
};












/********************************************
 * PyVectorIterator type definition
 ********************************************/

static void
vectoriter_dealloc(vectoriter *it)
{
    Py_XDECREF(it->vec);
    PyObject_Del(it);
}

static PyObject *
vectoriter_next(vectoriter *it)
{
    assert(it != NULL);
    if (it->vec == NULL)
        return NULL;
    assert(PyVector_Check(it->vec));

    if (it->it_index < it->vec->dim) {
        double item = it->vec->coords[it->it_index];
        ++(it->it_index);
        return PyFloat_FromDouble(item);
    }

    Py_DECREF(it->vec);
    it->vec = NULL;
    return NULL;
}

static PyObject *
vectoriter_len(vectoriter *it)
{
    Py_ssize_t len = 0;
    if (it && it->vec) {
        len = it->vec->dim - it->it_index;
    }
#if PY_VERSION_HEX >= 0x02050000
    return PyInt_FromSsize_t(len);
#else
    return PyInt_FromLong((long unsigned int)len);
#endif
}

static PyMethodDef vectoriter_methods[] = {
    {"__length_hint__", (PyCFunction)vectoriter_len, METH_NOARGS,
    },
    {NULL, NULL} /* sentinel */
};

static PyTypeObject PyVectorIter_Type = {
    TYPE_HEAD (NULL, 0)
    "pygame.math.VectorIterator", /* tp_name */
    sizeof(vectoriter),        /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)vectoriter_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    PyObject_GenericGetAttr,   /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    0,                         /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    PyObject_SelfIter,         /* tp_iter */
    (iternextfunc)vectoriter_next, /* tp_iternext */
    vectoriter_methods,        /* tp_methods */
    0,                         /* tp_members */
};

static PyObject *
vector_iter(PyObject *vec)
{
    vectoriter *it;
    if (!PyVector_Check(vec)) {
        PyErr_BadInternalCall();
        return NULL;
    }

    it = PyObject_New(vectoriter, &PyVectorIter_Type);
    if (it == NULL)
        return NULL;
    it->it_index = 0;
    Py_INCREF(vec);
    it->vec = (PyVector *)vec;
    return (PyObject *)it;
}














/*****************************************
 * elementwiseproxy
 *****************************************/



static void
vector_elementwiseproxy_dealloc(vector_elementwiseproxy *it)
{
    Py_XDECREF(it->vec);
    PyObject_Del(it);
}

static PyObject *
vector_elementwiseproxy_richcompare(PyObject *o1, PyObject *o2, int op)
{
    int i, dim, ret;
    double diff, value;
    double *other_coords;
    PyVector *vec;
    PyObject *other;

    if (vector_elementwiseproxy_Check(o1)) {
        vec = ((vector_elementwiseproxy*)o1)->vec;
        other = o2;
    }
    else {
        vec = ((vector_elementwiseproxy*)o2)->vec;
        other = o1;
        /* flip op */
        if (op == Py_LT)
            op = Py_GE;
        else if (op == Py_LE)
            op = Py_GT;
        else if (op == Py_GT)
            op = Py_LE;
        else if (op == Py_GE)
            op = Py_LT;
    }
    if (vector_elementwiseproxy_Check(other))
        other = (PyObject*)((vector_elementwiseproxy*)other)->vec;
    dim = vec->dim;

    ret = 1;
    if (PyVectorCompatible_Check(other, dim)) {
        other_coords = PyMem_New(double, dim);
        if (other_coords == NULL) {
            return NULL;
        }
        if (!PySequence_AsVectorCoords(other, other_coords, dim)) {
            PyMem_Del(other_coords);
            return NULL;
        }
        /* use diff == diff to check for NaN */
        /* TODO: how should NaN be handled with LT/LE/GT/GE? */
        switch (op) {
        case Py_EQ:
            for (i = 0; i < dim; i++) {
                diff = vec->coords[i] - other_coords[i];
                if ((diff != diff) || (fabs(diff) >= vec->epsilon)) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_NE:
            for (i = 0; i < dim; i++) {
                diff = vec->coords[i] - other_coords[i];
                if ((diff == diff) && (fabs(diff) < vec->epsilon)) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_LT:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] >= other_coords[i]) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_LE:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] > other_coords[i]) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_GT:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] <= other_coords[i]) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_GE:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] < other_coords[i]) {
                    ret = 0;
                    break;
                }
            }
            break;
        default:
            PyMem_Del(other_coords);
            PyErr_BadInternalCall();
            return NULL;
        }
        PyMem_Del(other_coords);
    }
    else if (RealNumber_Check(other)) {
        /* the following PyFloat_AsDouble call should never fail because
           then RealNumber_Check should have returned false */
        value = PyFloat_AsDouble(other);
        switch (op) {
        case Py_EQ:
            for (i = 0; i < dim; i++) {
                diff = vec->coords[i] - value;
                if (diff != diff || fabs(diff) >= vec->epsilon) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_NE:
            for (i = 0; i < dim; i++) {
                diff = vec->coords[i] - value;
                if (diff == diff && fabs(diff) < vec->epsilon) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_LT:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] >= value) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_LE:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] > value) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_GT:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] <= value) {
                    ret = 0;
                    break;
                }
            }
            break;
        case Py_GE:
            for (i = 0; i < dim; i++) {
                if (vec->coords[i] < value) {
                    ret = 0;
                    break;
                }
            }
            break;
        default:
            PyErr_BadInternalCall();
            return NULL;
        }
    }
    else {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    return PyBool_FromLong(ret);
}




/*******************************************************
 * vector_elementwiseproxy PyNumber emulation routines
 *******************************************************/

static PyObject *
vector_elementwiseproxy_generic_math(PyObject *o1, PyObject *o2, int op)
{
    int i, dim;
    double mod, other_value = 0.0;
    double other_coords[VECTOR_MAX_SIZE];
    PyObject *other;
    PyVector *vec, *ret;
    if (vector_elementwiseproxy_Check(o1)) {
        vec = ((vector_elementwiseproxy*)o1)->vec;
        other = o2;
    }
    else {
        other = o1;
        vec = ((vector_elementwiseproxy*)o2)->vec;
        op |= OP_ARG_REVERSE;
    }

    dim = vec->dim;

    if (vector_elementwiseproxy_Check(other))
        other = (PyObject*)((vector_elementwiseproxy*)other)->vec;

    if (PyVectorCompatible_Check(other, dim)) {
        op |= OP_ARG_VECTOR;
        if (!PySequence_AsVectorCoords(other, other_coords, dim))
            return NULL;
    }
    else if (RealNumber_Check(other)) {
        op |= OP_ARG_NUMBER;
        other_value = PyFloat_AsDouble(other);
    }
    else
        op |= OP_ARG_UNKNOWN;

    ret = (PyVector*)PyVector_NEW(dim);
    if (ret == NULL) {
        return NULL;
    }

    /* only handle special elementwise cases.
     * all others cases will be handled by the default clause */
    switch (op) {
    case OP_ADD | OP_ARG_NUMBER:
    case OP_ADD | OP_ARG_NUMBER | OP_ARG_REVERSE:
        for (i = 0; i < dim; i++)
            ret->coords[i] = vec->coords[i] + other_value;
        break;
    case OP_SUB | OP_ARG_NUMBER:
        for (i = 0; i < dim; i++)
            ret->coords[i] = vec->coords[i] - other_value;
        break;
    case OP_SUB | OP_ARG_NUMBER | OP_ARG_REVERSE:
        for (i = 0; i < dim; i++)
            ret->coords[i] = other_value - vec->coords[i];
        break;
    case OP_MUL | OP_ARG_VECTOR:
    case OP_MUL | OP_ARG_VECTOR | OP_ARG_REVERSE:
        for (i = 0; i < vec->dim; i++)
            ret->coords[i] = vec->coords[i] * other_coords[i];
        break;
    case OP_DIV | OP_ARG_VECTOR:
        for (i = 0; i < vec->dim; i++) {
            if (other_coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            ret->coords[i] = vec->coords[i] / other_coords[i];
        }
        break;
    case OP_DIV | OP_ARG_VECTOR | OP_ARG_REVERSE:
        for (i = 0; i < vec->dim; i++) {
            if (vec->coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            ret->coords[i] = other_coords[i] / vec->coords[i];
        }
        break;
    case OP_DIV | OP_ARG_NUMBER | OP_ARG_REVERSE:
        for (i = 0; i < vec->dim; i++) {
            if (vec->coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            ret->coords[i] = other_value / vec->coords[i];
        }
        break;
    case OP_FLOOR_DIV | OP_ARG_VECTOR:
        for (i = 0; i < vec->dim; i++) {
            if (other_coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            ret->coords[i] = floor(vec->coords[i] / other_coords[i]);
        }
        break;
    case OP_FLOOR_DIV | OP_ARG_VECTOR | OP_ARG_REVERSE:
        for (i = 0; i < vec->dim; i++) {
            if (vec->coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            ret->coords[i] = floor(other_coords[i] / vec->coords[i]);
        }
        break;
    case OP_FLOOR_DIV | OP_ARG_NUMBER | OP_ARG_REVERSE:
        for (i = 0; i < vec->dim; i++) {
            if (vec->coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            ret->coords[i] = floor(other_value / vec->coords[i]);
        }
        break;
    case OP_MOD | OP_ARG_VECTOR:
        for (i = 0; i < vec->dim; i++) {
            if (other_coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            mod = fmod(vec->coords[i], other_coords[i]);
            /* note: checking mod*value < 0 is incorrect -- underflows to
               0 if value < sqrt(smallest nonzero double) */
            if (mod && ((other_coords[i] < 0) != (mod < 0))) {
                mod += other_coords[i];
            }
            ret->coords[i] = mod;
        }
        break;
    case OP_MOD | OP_ARG_VECTOR | OP_ARG_REVERSE:
        for (i = 0; i < vec->dim; i++) {
            if (vec->coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            mod = fmod(other_coords[i], vec->coords[i]);
            /* note: see above */
            if (mod && ((vec->coords[i] < 0) != (mod < 0))) {
                mod += vec->coords[i];
            }
            ret->coords[i] = mod;
        }
        break;
    case OP_MOD | OP_ARG_NUMBER:
        if (other_value == 0) {
            PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
            Py_DECREF(ret);
            return NULL;
        }
        for (i = 0; i < vec->dim; i++) {
            mod = fmod(vec->coords[i], other_value);
            /* note: see above */
            if (mod && ((other_value < 0) != (mod < 0))) {
                mod += other_value;
            }
            ret->coords[i] = mod;
        }
        break;
    case OP_MOD | OP_ARG_NUMBER | OP_ARG_REVERSE:
        for (i = 0; i < vec->dim; i++) {
            if (vec->coords[i] == 0) {
                PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
                Py_DECREF(ret);
                return NULL;
            }
            mod = fmod(other_value, vec->coords[i]);
            /* note: see above */
            if (mod && ((vec->coords[i] < 0) != (mod < 0))) {
                mod += vec->coords[i];
            }
            ret->coords[i] = mod;
        }
        break;
    default:
        Py_DECREF(ret);
        return vector_generic_math((PyObject*)vec, other, op);
    }
    return (PyObject*)ret;
}

static PyObject *
vector_elementwiseproxy_add(PyObject *o1, PyObject *o2)
{
    return vector_elementwiseproxy_generic_math(o1, o2, OP_ADD);
}
static PyObject *
vector_elementwiseproxy_sub(PyObject *o1, PyObject *o2)
{
    return vector_elementwiseproxy_generic_math(o1, o2, OP_SUB);
}
static PyObject *
vector_elementwiseproxy_mul(PyObject *o1, PyObject *o2)
{
    return vector_elementwiseproxy_generic_math(o1, o2, OP_MUL);
}
static PyObject *
vector_elementwiseproxy_div(PyObject *o1, PyObject *o2)
{
    return vector_elementwiseproxy_generic_math(o1, o2, OP_DIV);
}
static PyObject *
vector_elementwiseproxy_floor_div(PyObject *o1, PyObject *o2)
{
    return vector_elementwiseproxy_generic_math(o1, o2, OP_FLOOR_DIV);
}
static PyObject *
vector_elementwiseproxy_mod(PyObject *o1, PyObject *o2)
{
    return vector_elementwiseproxy_generic_math(o1, o2, OP_MOD);
}

static PyObject *
vector_elementwiseproxy_pow(PyObject *baseObj, PyObject *expoObj, PyObject *mod)
{
    int i, dim;
    double *tmp;
    PyObject *bases[VECTOR_MAX_SIZE];
    PyObject *expos[VECTOR_MAX_SIZE];
    PyObject *ret, *result;
    if (mod != Py_None) {
        PyErr_SetString(PyExc_TypeError, "pow() 3rd argument not "
                        "supported for vectors");
        return NULL;
    }

    if (vector_elementwiseproxy_Check(baseObj)) {
        dim = ((vector_elementwiseproxy*)baseObj)->vec->dim;
        tmp = ((vector_elementwiseproxy*)baseObj)->vec->coords;
        for (i = 0; i < dim; ++i)
            bases[i] = PyFloat_FromDouble(tmp[i]);
        if (vector_elementwiseproxy_Check(expoObj)) {
            tmp = ((vector_elementwiseproxy*)expoObj)->vec->coords;
            for (i = 0; i < dim; ++i)
                expos[i] = PyFloat_FromDouble(tmp[i]);
        }
        else if (PyVectorCompatible_Check(expoObj, dim)) {
            for (i = 0; i < dim; ++i)
                expos[i] = PySequence_ITEM(expoObj, i);
        }
        else if (RealNumber_Check(expoObj)) {
            /* INCREF so that we can unify the clean up code */
            for (i = 0; i < dim; i++) {
                expos[i] = expoObj;
                Py_INCREF(expoObj);
            }
        }
        else {
            Py_INCREF(Py_NotImplemented);
            ret = Py_NotImplemented;
            goto clean_up;
        }
    }
    else {
        dim = ((vector_elementwiseproxy*)expoObj)->vec->dim;
        tmp = ((vector_elementwiseproxy*)expoObj)->vec->coords;
        for (i = 0; i < dim; ++i)
            expos[i] = PyFloat_FromDouble(tmp[i]);
        if (PyVectorCompatible_Check(baseObj, dim)) {
            for (i = 0; i < dim; ++i)
                bases[i] = PySequence_ITEM(baseObj, i);
        }
        else if (RealNumber_Check(baseObj)) {
            /* INCREF so that we can unify the clean up code */
            for (i = 0; i < dim; i++) {
                bases[i] = baseObj;
                Py_INCREF(baseObj);
            }
        }
        else {
            Py_INCREF(Py_NotImplemented);
            ret = Py_NotImplemented;
            goto clean_up;
        }
    }
    if (PyErr_Occurred()) {
        ret = NULL;
        goto clean_up;
    }

    ret = PyVector_NEW(dim);
    if (ret == NULL)
        goto clean_up;
    /* there are many special cases so we let python do the work for us */
    for (i = 0; i < dim; i++) {
        result = PyNumber_Power(bases[i], expos[i], Py_None);
        if (result == NULL || !RealNumber_Check(result)) {
            /* raising a negative number to a fractional power returns a
             * complex in python-3.x. we do not allow this. */
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_ValueError, "negative number "
                                "cannot be raised to a fractional power");
            }
            Py_XDECREF(result);
            Py_DECREF(ret);
            ret = NULL;
            goto clean_up;
        }
        ((PyVector*)ret)->coords[i] = PyFloat_AsDouble(result);
        Py_DECREF(result);
    }
clean_up:
    for (i = 0; i < dim; ++i) {
        Py_XDECREF(bases[i]);
        Py_XDECREF(expos[i]);
    }
    return ret;
}


static PyObject *
vector_elementwiseproxy_abs(vector_elementwiseproxy *self)
{
    int i;
    PyVector *ret = (PyVector*)PyVector_NEW(self->vec->dim);
    if (ret != NULL) {
        for (i = 0; i < self->vec->dim; i++) {
            ret->coords[i] = fabs(self->vec->coords[i]);
        }
    }
    return (PyObject*)ret;
}

static PyObject *
vector_elementwiseproxy_neg(vector_elementwiseproxy *self)
{
    return vector_neg(self->vec);
}

static PyObject *
vector_elementwiseproxy_pos(vector_elementwiseproxy *self)
{
    return vector_pos(self->vec);
}

static int
vector_elementwiseproxy_nonzero(vector_elementwiseproxy *self)
{
    return vector_nonzero(self->vec);
}

static PyNumberMethods vector_elementwiseproxy_as_number = {
    (binaryfunc)vector_elementwiseproxy_add,      /* nb_add;       __add__ */
    (binaryfunc)vector_elementwiseproxy_sub,      /* nb_subtract;  __sub__ */
    (binaryfunc)vector_elementwiseproxy_mul,      /* nb_multiply;  __mul__ */
#if !PY3
    (binaryfunc)vector_elementwiseproxy_div,      /* nb_divide;    __div__ */
#endif
    (binaryfunc)vector_elementwiseproxy_mod,      /* nb_remainder; __mod__ */
    (binaryfunc)0,                                /* nb_divmod;    __divmod__ */
    (ternaryfunc)vector_elementwiseproxy_pow,     /* nb_power;     __pow__ */
    (unaryfunc)vector_elementwiseproxy_neg, /* nb_negative;  __neg__ */
    (unaryfunc)vector_elementwiseproxy_pos, /* nb_positive;  __pos__ */
    (unaryfunc)vector_elementwiseproxy_abs, /* nb_absolute;  __abs__ */
    (inquiry)vector_elementwiseproxy_nonzero, /* nb_nonzero;   __nonzero__ */
    (unaryfunc)0,                   /* nb_invert;    __invert__ */
    (binaryfunc)0,                  /* nb_lshift;    __lshift__ */
    (binaryfunc)0,                  /* nb_rshift;    __rshift__ */
    (binaryfunc)0,                  /* nb_and;       __and__ */
    (binaryfunc)0,                  /* nb_xor;       __xor__ */
    (binaryfunc)0,                  /* nb_or;        __or__ */
#if !PY3
    (coercion)0,                    /* nb_coerce;    __coerce__ */
#endif
    (unaryfunc)0,                   /* nb_int;       __int__ */
    (unaryfunc)0,                   /* nb_long;      __long__ */
    (unaryfunc)0,                   /* nb_float;     __float__ */
#if !PY3
    (unaryfunc)0,                   /* nb_oct;       __oct__ */
    (unaryfunc)0,                   /* nb_hex;       __hex__ */
#endif
    /* Added in release 2.0 */
    (binaryfunc)0,                  /* nb_inplace_add;       __iadd__ */
    (binaryfunc)0,                  /* nb_inplace_subtract;  __isub__ */
    (binaryfunc)0,                  /* nb_inplace_multiply;  __imul__ */
#if !PY3
    (binaryfunc)0,                  /* nb_inplace_divide;    __idiv__ */
#endif
    (binaryfunc)0,                  /* nb_inplace_remainder; __imod__ */
    (ternaryfunc)0,                 /* nb_inplace_power;     __pow__ */
    (binaryfunc)0,                  /* nb_inplace_lshift;    __ilshift__ */
    (binaryfunc)0,                  /* nb_inplace_rshift;    __irshift__ */
    (binaryfunc)0,                  /* nb_inplace_and;       __iand__ */
    (binaryfunc)0,                  /* nb_inplace_xor;       __ixor__ */
    (binaryfunc)0,                  /* nb_inplace_or;        __ior__ */

    /* Added in release 2.2 */
    (binaryfunc)vector_elementwiseproxy_floor_div, /* nb_floor_divide;         __floor__ */
    (binaryfunc)vector_elementwiseproxy_div, /* nb_true_divide;          __truediv__ */
    (binaryfunc)0,                  /* nb_inplace_floor_divide; __ifloor__ */
    (binaryfunc)0,                  /* nb_inplace_true_divide;  __itruediv__ */
};




static PyTypeObject PyVectorElementwiseProxy_Type = {
    TYPE_HEAD (NULL, 0)
    "pygame.math.VectorElementwiseProxy", /* tp_name */
    sizeof(vector_elementwiseproxy), /* tp_basicsize */
    0,                         /* tp_itemsize */
    /* Methods to implement standard operations */
    (destructor)vector_elementwiseproxy_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    (reprfunc)0,               /* tp_repr */
    /* Method suites for standard classes */
    &vector_elementwiseproxy_as_number, /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    /* More standard operations (here for binary compatibility) */
    0,                         /* tp_hash */
    0,                         /* tp_call */
    (reprfunc)0,               /* tp_str */
    (getattrofunc)0,           /* tp_getattro */
    (setattrofunc)0,           /* tp_setattro */
    /* Functions to access object as input/output buffer */
    0,                         /* tp_as_buffer */
    /* Flags to define presence of optional/expanded features */
#if PY3
    Py_TPFLAGS_DEFAULT,
#else
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES, /* tp_flags */
#endif
    /* Documentation string */
    0,                         /* tp_doc */

    /* Assigned meaning in release 2.0 */
    /* call function for all accessible objects */
    0,                         /* tp_traverse */
    /* delete references to contained objects */
    0,                         /* tp_clear */

    /* Assigned meaning in release 2.1 */
    /* rich comparisons */
    (richcmpfunc)vector_elementwiseproxy_richcompare, /* tp_richcompare */
    /* weak reference enabler */
    0,                         /* tp_weaklistoffset */

    /* Added in release 2.2 */
    /* Iterators */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    /* Attribute descriptor and subclassing stuff */
    0,                         /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)0,               /* tp_init */
    0,                         /* tp_alloc */
    (newfunc)0,                /* tp_new */
    0,                         /* tp_free */
    0,                         /* tp_is_gc */
    0,                         /* tp_bases */
    0,                         /* tp_mro */
    0,                         /* tp_cache */
    0,                         /* tp_subclasses */
    0,                         /* tp_weaklist */
};



static PyObject *
vector_elementwise(PyVector *vec)
{
    vector_elementwiseproxy *proxy;
    if (!PyVector_Check(vec)) {
        PyErr_BadInternalCall();
        return NULL;
    }

    proxy = PyObject_New(vector_elementwiseproxy,
                         &PyVectorElementwiseProxy_Type);
    if (proxy == NULL)
        return NULL;
    Py_INCREF(vec);
    proxy->vec = (PyVector *)vec;
    return (PyObject *)proxy;
}







static PyObject *
math_enable_swizzling(PyVector *self)
{
    swizzling_enabled = 1;
    Py_RETURN_NONE;
}

static PyObject *
math_disable_swizzling(PyVector *self)
{
    swizzling_enabled = 0;
    Py_RETURN_NONE;
}

static PyMethodDef _math_methods[] =
{
    {"enable_swizzling", (PyCFunction)math_enable_swizzling, METH_NOARGS,
     "enables swizzling."
    },
    {"disable_swizzling", (PyCFunction)math_disable_swizzling, METH_NOARGS,
     "disables swizzling."
    },
    {NULL, NULL, 0, NULL}
};


/****************************
 * Module init function
 ****************************/

MODINIT_DEFINE (math)
{
    PyObject *module, *apiobj;
    static void *c_api[PYGAMEAPI_MATH_NUMSLOTS];

#if PY3
    static struct PyModuleDef _module = {
        PyModuleDef_HEAD_INIT,
        "math",
        DOC_PYGAMEMATH,
        -1,
        _math_methods,
        NULL, NULL, NULL, NULL
    };
#endif

    /* initialize the extension types */
    if ((PyType_Ready(&PyVector2_Type) < 0) ||
        (PyType_Ready(&PyVector3_Type) < 0) ||
        (PyType_Ready(&PyVectorIter_Type) < 0) ||
        (PyType_Ready(&PyVectorElementwiseProxy_Type) < 0) /*||
        (PyType_Ready(&PyVector4_Type) < 0)*/) {
        MODINIT_ERROR;
    }

    /* initialize the module */
#if PY3
    module = PyModule_Create (&_module);
#else
    module = Py_InitModule3 (MODPREFIX "math", _math_methods, DOC_PYGAMEMATH);
#endif

    if (module == NULL) {
        MODINIT_ERROR;
    }

    /* add extension types to module */
    Py_INCREF(&PyVector2_Type);
    Py_INCREF(&PyVector3_Type);
    Py_INCREF(&PyVectorIter_Type);
    Py_INCREF(&PyVectorElementwiseProxy_Type);
    /*
    Py_INCREF(&PyVector4_Type);
    */
    if ((PyModule_AddObject(module, "Vector2", (PyObject *)&PyVector2_Type) != 0) ||
        (PyModule_AddObject(module, "Vector3", (PyObject *)&PyVector3_Type) != 0) ||
        (PyModule_AddObject(module, "VectorElementwiseProxy", (PyObject *)&PyVectorElementwiseProxy_Type) != 0) ||
        (PyModule_AddObject(module, "VectorIterator", (PyObject *)&PyVectorIter_Type) != 0) /*||
        (PyModule_AddObject(module, "Vector4", (PyObject *)&PyVector4_Type) != 0)*/) {
        Py_DECREF(&PyVector2_Type);
        Py_DECREF(&PyVector3_Type);
        Py_DECREF(&PyVectorElementwiseProxy_Type);
        Py_DECREF(&PyVectorIter_Type);
        /*
        Py_DECREF(&PyVector4_Type);
        */
        DECREF_MOD (module);
        MODINIT_ERROR;
    }

    /* export the C api */
    c_api[0] = &PyVector2_Type;
    c_api[1] = &PyVector3_Type;
    /*
    c_api[2] = &PyVector4_Type;
    c_api[3] = PyVector_NEW;
    c_api[4] = PyVectorCompatible_Check;
    */
    apiobj = encapsulate_api(c_api, "math");
    if (apiobj == NULL) {
        DECREF_MOD (module);
        MODINIT_ERROR;
    }

    if (PyModule_AddObject(module, PYGAMEAPI_LOCAL_ENTRY, apiobj) != 0) {
        Py_DECREF (apiobj);
        DECREF_MOD (module);
        MODINIT_ERROR;
    }

    MODINIT_RETURN (module);
}
