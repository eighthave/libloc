/*
	libloc - A library to determine the location of someone on the Internet

	Copyright (C) 2017 IPFire Development Team <info@ipfire.org>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.
*/

#include <Python.h>

#include <loc/libloc.h>
#include <loc/writer.h>

#include "locationmodule.h"
#include "as.h"
#include "network.h"
#include "writer.h"

static PyObject* Writer_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
	WriterObject* self = (WriterObject*)type->tp_alloc(type, 0);

	return (PyObject*)self;
}

static void Writer_dealloc(WriterObject* self) {
	if (self->writer)
		loc_writer_unref(self->writer);

	Py_TYPE(self)->tp_free((PyObject* )self);
}

static int Writer_init(WriterObject* self, PyObject* args, PyObject* kwargs) {
	// Create the writer object
	int r = loc_writer_new(loc_ctx, &self->writer);
	if (r)
		return -1;

	return 0;
}

static PyObject* Writer_get_vendor(WriterObject* self) {
	const char* vendor = loc_writer_get_vendor(self->writer);

	return PyUnicode_FromString(vendor);
}

static int Writer_set_vendor(WriterObject* self, PyObject* value) {
	const char* vendor = PyUnicode_AsUTF8(value);

	int r = loc_writer_set_vendor(self->writer, vendor);
	if (r) {
		PyErr_Format(PyExc_ValueError, "Could not set vendor: %s", vendor);
		return r;
	}

	return 0;
}

static PyObject* Writer_get_description(WriterObject* self) {
	const char* description = loc_writer_get_description(self->writer);

	return PyUnicode_FromString(description);
}

static int Writer_set_description(WriterObject* self, PyObject* value) {
	const char* description = PyUnicode_AsUTF8(value);

	int r = loc_writer_set_description(self->writer, description);
	if (r) {
		PyErr_Format(PyExc_ValueError, "Could not set description: %s", description);
		return r;
	}

	return 0;
}

static PyObject* Writer_get_license(WriterObject* self) {
	const char* license = loc_writer_get_license(self->writer);

	return PyUnicode_FromString(license);
}

static int Writer_set_license(WriterObject* self, PyObject* value) {
	const char* license = PyUnicode_AsUTF8(value);

	int r = loc_writer_set_license(self->writer, license);
	if (r) {
		PyErr_Format(PyExc_ValueError, "Could not set license: %s", license);
		return r;
	}

	return 0;
}

static PyObject* Writer_add_as(WriterObject* self, PyObject* args) {
	struct loc_as* as;
	uint32_t number = 0;

	if (!PyArg_ParseTuple(args, "i", &number))
		return NULL;

	// Create AS object
	int r = loc_writer_add_as(self->writer, &as, number);
	if (r)
		return NULL;

	PyObject* obj = new_as(&ASType, as);
	loc_as_unref(as);

	return obj;
}

static PyObject* Writer_add_network(WriterObject* self, PyObject* args) {
	struct loc_network* network;
	const char* string = NULL;

	if (!PyArg_ParseTuple(args, "s", &string))
		return NULL;

	// Create network object
	int r = loc_writer_add_network(self->writer, &network, string);
	if (r) {
		switch (r) {
			case -EINVAL:
				PyErr_SetString(PyExc_ValueError, "Invalid network");
				break;

			case -EBUSY:
				PyErr_SetString(PyExc_IndexError, "A network already exists here");
				break;
		}

		return NULL;
	}

	PyObject* obj = new_network(&NetworkType, network);
	loc_network_unref(network);

	return obj;
}

static PyObject* Writer_write(WriterObject* self, PyObject* args) {
	const char* path = NULL;

	if (!PyArg_ParseTuple(args, "s", &path))
		return NULL;

	FILE* f = fopen(path, "w");
	if (!f) {
		PyErr_Format(PyExc_IOError, strerror(errno));
		return NULL;
	}

	int r = loc_writer_write(self->writer, f);
	fclose(f);

	// Raise any errors
	if (r) {
		PyErr_Format(PyExc_IOError, strerror(errno));
		return NULL;
	}

	Py_RETURN_NONE;
}

static struct PyMethodDef Writer_methods[] = {
	{
		"add_as",
		(PyCFunction)Writer_add_as,
		METH_VARARGS,
		NULL,
	},
	{
		"add_network",
		(PyCFunction)Writer_add_network,
		METH_VARARGS,
		NULL,
	},
	{
		"write",
		(PyCFunction)Writer_write,
		METH_VARARGS,
		NULL,
	},
	{ NULL },
};

static struct PyGetSetDef Writer_getsetters[] = {
	{
		"description",
		(getter)Writer_get_description,
		(setter)Writer_set_description,
		NULL,
		NULL,
	},
	{
		"license",
		(getter)Writer_get_license,
		(setter)Writer_set_license,
		NULL,
		NULL,
	},
	{
		"vendor",
		(getter)Writer_get_vendor,
		(setter)Writer_set_vendor,
		NULL,
		NULL,
	},
	{ NULL },
};

PyTypeObject WriterType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	tp_name:                "location.Writer",
	tp_basicsize:           sizeof(WriterObject),
	tp_flags:               Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
	tp_new:                 Writer_new,
	tp_dealloc:             (destructor)Writer_dealloc,
	tp_init:                (initproc)Writer_init,
	tp_doc:                 "Writer object",
	tp_methods:             Writer_methods,
	tp_getset:              Writer_getsetters,
};
