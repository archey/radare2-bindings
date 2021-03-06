/* radare - LGPL - Copyright 2009-2016 - pancake */
/* python extension for radare2's r_lang */

#include <r_lib.h>
#include <r_lang.h>
#include <r_core.h>
#undef _GNU_SOURCE
#undef _XOPEN_SOURCE
#undef _POSIX_C_SOURCE
#undef PREFIX
#include <Python.h>
#include <structmember.h>
#if PY_MAJOR_VERSION>=3
#define PyString_FromString PyUnicode_FromString
#endif

static RCore *core = NULL;

static int run(RLang *lang, const char *code, int len) {
	core = (RCore *)lang->user;
	PyRun_SimpleString (code);
	return R_TRUE;
}

static int slurp_python(const char *file) {
	FILE *fd = r_sandbox_fopen (file, "r");
	if (fd) {
		PyRun_SimpleFile (fd, file);
		fclose (fd);
		return true;
	}
	return false;
}

static int run_file(struct r_lang_t *lang, const char *file) {
	return slurp_python (file);
}

/* init */
typedef struct {
	PyObject_HEAD
	PyObject *first; /* first name */
	PyObject *last;  /* last name */
	int number;
} Radare;

#if PY_MAJOR_VERSION<3
static char *py_nullstr = "";

static void Radare_dealloc(Radare* self) {
	Py_XDECREF (self->first);
	Py_XDECREF (self->last);
	//self->ob_type->tp_free((PyObject*)self);
}

static PyObject * Radare_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	Radare *self = (Radare *)type->tp_alloc (type, 0);
	if (self) {
		self->first = PyString_FromString ("");
		if (!self->first) {
			Py_DECREF (self);
			return NULL;
		}

		self->last = PyString_FromString ("");
		if (!self->last) {
			Py_DECREF (self);
			return NULL;
		}
		self->number = 0;
	}

	return (PyObject *)self;
}

static char *getS(PyObject *o, const char *name) {
	if (!o) return NULL;
	PyObject *res = PyDict_GetItemString (o, name);
	if (!res) return NULL;
	return PyString_AsString (res);
}

static st64 getI(PyObject *o, const char *name) {
	if (!o) return 0;
	PyObject *res = PyDict_GetItemString (o, name);
	if (!res) return 0;
	return (st64) PyNumber_AsSsize_t (res, NULL);
}

static void *getF(PyObject *o, const char *name) {
	if (!o) return NULL;
	return PyDict_GetItemString (o, name);
}

static void *py_assemble_cb = NULL;

static int py_assemble(RAsm *a, RAsmOp *op, const char *str) {
	int i, size = 0;
	int seize = -1;
	const char *opstr = str;
	if (py_assemble_cb) {
		PyObject *arglist = Py_BuildValue ("(z)", str);
		PyObject *result = PyEval_CallObject (py_assemble_cb, arglist);
		if (result && PyList_Check (result)) {
			seize = size = PyList_Size (result);
			for (i = 0; i < size ; i++) {
				PyObject *len = PyList_GetItem (result, i);
				op->buf[i] = PyNumber_AsSsize_t (len, NULL);
			}
		} else {
			eprintf ("Unknown type returned. List was expected.\n");
		}
	}
	op->size = size = seize;
	strncpy (op->buf_asm, opstr, sizeof (op->buf_asm));
	r_hex_bin2str (op->buf, op->size, op->buf_hex);
	return seize;
}

static void *py_disassemble_cb = NULL;

static int py_disassemble(RAsm *a, RAsmOp *op, const ut8 *buf, int len) {
	int size = 0;
	int seize = -1;
	const char *opstr = "invalid";
	if (py_disassemble_cb) {
		PyObject *arglist = Py_BuildValue ("(s#)", buf, len);
		PyObject *result = PyEval_CallObject (py_disassemble_cb, arglist);
		if (result && PyList_Check (result)) {
			PyObject *len = PyList_GetItem (result, 0);
			PyObject *str = PyList_GetItem (result, 1);
			seize = PyNumber_AsSsize_t (len, NULL);
			opstr = PyString_AsString (str);
		} else {
			eprintf ("Unknown type returned. List was expected.\n");
		}
	}
	op->size = size = seize;
	strncpy (op->buf_asm, opstr, sizeof (op->buf_asm));
	r_hex_bin2str (buf, op->size, op->buf_hex);
	return seize;
}

static PyObject *Radare_plugin(Radare* self, PyObject *args) {
	char *type = NULL;
	void *ptr = NULL;
	void *cb = NULL;

	if (!PyArg_ParseTuple (args, "sO", &type, &cb)) {
		return Py_False;
	}
	if (!PyCallable_Check (cb)) {
		PyErr_SetString (PyExc_TypeError, "second parameter must be callable");
		return Py_False;
	}
	if (strcmp (type, "asm")) {
		eprintf ("TODO: r2lang.plugin only supports 'asm' plugins atm\n");
		return Py_False;
	}

	PyObject *arglist = Py_BuildValue("(i)", 0);
	PyObject *o = PyEval_CallObject (cb, arglist);

	RAsmPlugin *ap = R_NEW0 (RAsmPlugin);
	ap->name = getS (o,"name");
	ap->arch = getS (o, "arch");
	ap->license = getS (o, "license");
	ap->desc = getS (o, "desc");
	ap->bits = getI (o, "bits");
	ptr = getF (o, "disassemble");
	if (ptr) {
		Py_INCREF(ptr);
		py_disassemble_cb = ptr;
		ap->disassemble = py_disassemble;
	}
	ptr = getF (o, "assemble");
	if (ptr) {
		Py_INCREF(ptr);
		py_assemble_cb = ptr;
		ap->assemble = py_assemble;
	}

	RLibStruct *lp = R_NEW0 (RLibStruct);
	lp->type = R_LIB_TYPE_ASM;
	lp->data = ap;
	r_lib_open_ptr (core->lib, "python.py", NULL, lp);
	return Py_True;
}

static PyObject *Radare_cmd(Radare* self, PyObject *args) {
	char *str, *cmd = NULL;
	if (!PyArg_ParseTuple (args, "s", &cmd))
		return NULL;
	str = r_core_cmd_str (core, cmd);
	return PyString_FromString (str? str: py_nullstr);
}

static int Radare_init(Radare *self, PyObject *args, PyObject *kwds) {
	PyObject *first = NULL, *last = NULL, *tmp;

	static char *kwlist[] = { "first", "last", "number", NULL };

	if (!PyArg_ParseTupleAndKeywords (args, kwds, "|OOi",
		kwlist, &first, &last, &self->number))
		return -1;

	if (first) {
		tmp = self->first;
		Py_INCREF (first);
		self->first = first;
		Py_XDECREF (tmp);
	}

	if (last) {
		tmp = self->last;
		Py_INCREF (last);
		self->last = last;
		Py_XDECREF (tmp);
	}

	return 0;
}

static PyMemberDef Radare_members[] = {
	{"first", T_OBJECT_EX, offsetof(Radare, first), 0, "first name"},
	{"last", T_OBJECT_EX, offsetof(Radare, last), 0, "last name"},
	{"number", T_INT, offsetof(Radare, number), 0, "noddy number"},
	{NULL}  /* Sentinel */
};

static PyMethodDef Radare_methods[] = {
	{"cmd", (PyCFunction)Radare_cmd, METH_VARARGS,
		"Executes a radare command and returns a string"
	},
	{"plugin", (PyCFunction)Radare_plugin, METH_VARARGS,
		"Register plugins in radare2"
	},
	{NULL}  /* Sentinel */
};

static PyTypeObject RadareType = {
	PyObject_HEAD_INIT (NULL)
	0,                         /*ob_size*/
	"radare.RadareInternal",   /*tp_name*/
	sizeof (Radare),           /*tp_basicsize*/
	0,                         /*tp_itemsize*/
	(destructor)Radare_dealloc,/*tp_dealloc*/
	0,                         /*tp_print*/
	0,                         /*tp_getattr*/
	0,                         /*tp_setattr*/
	0,                         /*tp_compare*/
	0,                         /*tp_repr*/
	0,                         /*tp_as_number*/
	0,                         /*tp_as_sequence*/
	0,                         /*tp_as_mapping*/
	0,                         /*tp_hash */
	0,                         /*tp_call*/
	0,                         /*tp_str*/
	0,                         /*tp_getattro*/
	0,                         /*tp_setattro*/
	0,                         /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
	"Radare objects",          /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	Radare_methods,            /* tp_methods */
	Radare_members,            /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	(initproc)Radare_init,     /* tp_init */
	0,                         /* tp_alloc */
	Radare_new,                /* tp_new */
};

static void init_radare_module(void) {
	if (PyType_Ready (&RadareType) < 0) {
		return;
	}
	Py_InitModule3 ("r2lang", Radare_methods, "radare python extension");
}
#else

/*
SEE 
static PyMethodDef EmbMethods[] = {
    {"numargs", emb_numargs, METH_VARARGS,
     "Return the number of arguments received by the process."},
    {NULL, NULL, 0, NULL}
};
*/

static PyModuleDef EmbModule = {
	PyModuleDef_HEAD_INIT, "radare", NULL, -1, NULL, //EmbMethods,
	NULL, NULL, NULL, NULL
};

static int init_radare_module(void) {
	// TODO import r2-swig api
	//eprintf ("TODO: python>3.x instantiate 'r' object\n");
	Gcore = lang->user;
	PyObject *m = PyModule_Create (&EmbModule);
	if (!m) {
		eprintf ("Cannot create python3 r2 module\n");
		return false;
	}
	return true;
}
#endif

/* -init- */

static int init(RLang *user);
static int setup(RLang *user);

static int prompt(void *user) {
	return !PyRun_SimpleString (
		"r2 = None\n"
		"try:\n"
		"	import r2lang\n"
		"	import r2pipe\n"
		"	r2 = r2pipe.open()\n"
		"	import IPython\n"
		"	IPython.embed()\n"
		"except:\n"
		"	raise Exception(\"Cannot find IPython\")\n"
	);
}

static int setup(RLang *lang) {
	RListIter *iter;
	RLangDef *def;
	char cmd[128];
	// Segfault if already initialized ?
	PyRun_SimpleString (
		"try:\n"
		"	from r2.r_core import RCore\n"
		"except:\n"
		"	pass\n");
	PyRun_SimpleString ("import r2pipe");
	core = lang->user;
	r_list_foreach (lang->defs, iter, def) {
		if (!def->type || !def->name) {
			continue;
		}
		if (!strcmp (def->type, "int"))
			snprintf (cmd, sizeof (cmd), "%s=%d", def->name, (int)(size_t)def->value);
		else if (!strcmp (def->type, "string"))
			snprintf (cmd, sizeof (cmd), "%s=\"%s\"", def->name, (char *)def->value);
		else snprintf (cmd, sizeof (cmd), "%s=%s.ncast(%p)",
			def->name, def->type, def->value);
		PyRun_SimpleString (cmd);
	}
	return R_TRUE;
}

static int init(RLang *lang) {
	core = lang->user;
	// DO NOT INITIALIZE MODULE IF ALREADY INITIALIZED
	if (Py_IsInitialized ()) {
		return 0;
	}
	Py_Initialize ();
	init_radare_module ();
	return R_TRUE;
}

static int fini(void *user) {
	return R_TRUE;
}

static const char *help =
	//" r = new RadareInternal()\n"
	"  print r.cmd(\"p8 10\");\n";

struct r_lang_plugin_t r_lang_plugin_python = {
	.name = "python",
	.ext = "py",
	.desc = "Python language extension",
	.init = &init,
	.setup = &setup,
	.fini = (void *)&fini,
	.help = &help,
	.prompt = (void *)&prompt,
	.run = &run,
	.run_file = &run_file,
	.set_argv = NULL,
};

#if !CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_LANG,
	.data = &r_lang_plugin_python,
};
#endif
