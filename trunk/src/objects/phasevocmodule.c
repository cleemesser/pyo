/*************************************************************************
 * Copyright 2010 Olivier Belanger                                        *                  
 *                                                                        * 
 * This file is part of pyo, a python module to help digital signal       *
 * processing script creation.                                            *  
 *                                                                        * 
 * pyo is free software: you can redistribute it and/or modify            *
 * it under the terms of the GNU General Public License as published by   *
 * the Free Software Foundation, either version 3 of the License, or      *
 * (at your option) any later version.                                    * 
 *                                                                        *
 * pyo is distributed in the hope that it will be useful,                 *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of         *    
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 * GNU General Public License for more details.                           *
 *                                                                        *
 * You should have received a copy of the GNU General Public License      *
 * along with pyo.  If not, see <http://www.gnu.org/licenses/>.           *
 *************************************************************************/

#include <Python.h>
#include "structmember.h"
#include <math.h>
#include "pyomodule.h"
#include "streammodule.h"
#include "pvstreammodule.h"
#include "servermodule.h"
#include "dummymodule.h"
#include "fft.h"
#include "wind.h"

static int 
isPowerOfTwo(int x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

typedef struct {
    pyo_audio_HEAD
    PyObject *input;
    Stream *input_stream;
    PVStream *pv_stream;
    int size;
    int olaps;
    int hsize;
    int hopsize;
    int wintype;
    int incount;
    int inputLatency;
    int overcount;
    MYFLT factor;
    MYFLT scale;
    MYFLT *input_buffer;
    MYFLT *inframe;
    MYFLT *outframe; 
    MYFLT *real; 
    MYFLT *imag; 
    MYFLT *lastPhase;
    MYFLT **twiddle;
    MYFLT *window;
    MYFLT **magn;
    MYFLT **freq;
    int *count;
} PVAnal;


static void
PVAnal_realloc_memories(PVAnal *self) {
    int i, j, n8;
    self->hsize = self->size / 2;
    self->hopsize = self->size / self->olaps;
    self->factor = self->sr / (self->hopsize * TWOPI);
    self->scale = TWOPI * self->hopsize / self->size;
    self->inputLatency = self->size - self->hopsize;
    self->incount = self->inputLatency;
    self->overcount = 0;
    n8 = self->size >> 3;
    self->input_buffer = (MYFLT *)realloc(self->input_buffer, self->size * sizeof(MYFLT));
    self->inframe = (MYFLT *)realloc(self->inframe, self->size * sizeof(MYFLT));
    self->outframe = (MYFLT *)realloc(self->outframe, self->size * sizeof(MYFLT));    
    for (i=0; i<self->size; i++)
        self->input_buffer[i] = self->inframe[i] = self->outframe[i] = 0.0;
    self->lastPhase = (MYFLT *)realloc(self->lastPhase, self->hsize * sizeof(MYFLT)); 
    self->real = (MYFLT *)realloc(self->real, self->hsize * sizeof(MYFLT)); 
    self->imag = (MYFLT *)realloc(self->imag, self->hsize * sizeof(MYFLT)); 
    self->magn = (MYFLT **)realloc(self->magn, self->olaps * sizeof(MYFLT *)); 
    self->freq = (MYFLT **)realloc(self->freq, self->olaps * sizeof(MYFLT *));
    for (i=0; i<self->olaps; i++) {
        self->magn[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        self->freq[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        for (j=0; j<self->hsize; j++)
            self->magn[i][j] = self->freq[i][j] = 0.0;
    } 
    for (i=0; i<self->hsize; i++)
        self->lastPhase[i] = self->real[i] = self->imag[i] = 0.0;
    self->twiddle = (MYFLT **)realloc(self->twiddle, 4 * sizeof(MYFLT *));
    for(i=0; i<4; i++)
        self->twiddle[i] = (MYFLT *)malloc(n8 * sizeof(MYFLT));
    fft_compute_split_twiddle(self->twiddle, self->size);
    self->window = (MYFLT *)realloc(self->window, self->size * sizeof(MYFLT));
    gen_window(self->window, self->size, self->wintype);
    for (i=0; i<self->bufsize; i++)
        self->count[i] = self->incount;
    PVStream_setFFTsize(self->pv_stream, self->size);
    PVStream_setOlaps(self->pv_stream, self->olaps);
    PVStream_setMagn(self->pv_stream, self->magn);
    PVStream_setFreq(self->pv_stream, self->freq);
    PVStream_setCount(self->pv_stream, self->count);
}

static void
PVAnal_process(PVAnal *self) {
    int i, k, mod;
    MYFLT real, imag, mag, phase, tmp;
    MYFLT *in = Stream_getData((Stream *)self->input_stream);
    
    for (i=0; i<self->bufsize; i++) {
        self->input_buffer[self->incount] = in[i];
        self->count[i] = self->incount;
        self->incount++;
        if (self->incount >= self->size) {
            self->incount = self->inputLatency;
            mod = self->hopsize * self->overcount;
            for (k=0; k<self->size; k++) {
                self->inframe[(k+mod)%self->size] = self->input_buffer[k] * self->window[k];
            }
            realfft_split(self->inframe, self->outframe, self->size, self->twiddle);
            self->real[0] = self->outframe[0];
            self->imag[0] = 0.0;
            for (k=1; k<self->hsize; k++) {
                self->real[k] = self->outframe[k];
                self->imag[k] = self->outframe[self->size - k];
            }

            for (k=0; k<self->hsize; k++) {
                real = self->real[k];
                imag = self->imag[k];
                mag = MYSQRT(real*real + imag*imag);
                phase = MYATAN2(imag, real);
                tmp = phase - self->lastPhase[k];
                self->lastPhase[k] = phase;
                while (tmp > PI) tmp -= TWOPI;
                while (tmp < -PI) tmp += TWOPI;
                self->magn[self->overcount][k] = mag;
                self->freq[self->overcount][k] = (tmp + k * self->scale) * self->factor;
            }
            for (k=0; k<self->inputLatency; k++) {
                self->input_buffer[k] = self->input_buffer[k + self->hopsize];
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVAnal_setProcMode(PVAnal *self)
{        
    self->proc_func_ptr = PVAnal_process;  
}

static void
PVAnal_compute_next_data_frame(PVAnal *self)
{
    (*self->proc_func_ptr)(self); 
}

static int
PVAnal_traverse(PVAnal *self, visitproc visit, void *arg)
{
    pyo_VISIT
    Py_VISIT(self->input);
    Py_VISIT(self->input_stream);
    Py_VISIT(self->pv_stream);
    return 0;
}

static int 
PVAnal_clear(PVAnal *self)
{
    pyo_CLEAR
    Py_CLEAR(self->input);
    Py_CLEAR(self->input_stream);
    Py_CLEAR(self->pv_stream);
    return 0;
}

static void
PVAnal_dealloc(PVAnal* self)
{
    int i;
    pyo_DEALLOC
    free(self->input_buffer);
    free(self->inframe);
    free(self->outframe); 
    free(self->real); 
    free(self->imag); 
    free(self->lastPhase);
    for(i=0; i<4; i++) {
        free(self->twiddle[i]);
    }
    free(self->twiddle);
    free(self->window);
    for(i=0; i<self->olaps; i++) {
        free(self->magn[i]);
        free(self->freq[i]);
    }
    free(self->magn);
    free(self->freq);
    free(self->count);
    PVAnal_clear(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
PVAnal_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    int i, k;
    PyObject *inputtmp, *input_streamtmp;
    PVAnal *self;
    self = (PVAnal *)type->tp_alloc(type, 0);

    self->size = 1024;
    self->olaps = 4;
    self->wintype = 2;
    INIT_OBJECT_COMMON
    Stream_setFunctionPtr(self->stream, PVAnal_compute_next_data_frame);
    self->mode_func_ptr = PVAnal_setProcMode;

    static char *kwlist[] = {"input", "size", "olaps", "wintype", NULL};
    
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|iii", kwlist, &inputtmp, &self->size, &self->olaps, &self->wintype))
        Py_RETURN_NONE;

    INIT_INPUT_STREAM
 
    PyObject_CallMethod(self->server, "addStream", "O", self->stream);

    MAKE_NEW_PV_STREAM(self->pv_stream, &PVStreamType, NULL);

    if (!isPowerOfTwo(self->size)) {
        k = 1;
        while (k < self->size)
            k *= 2;
        self->size = k;
        printf("FFT size must be a power-of-2, using the next power-of-2 greater than size : %d\n", self->size);
    }

    self->count = (int *)realloc(self->count, self->bufsize * sizeof(int));

    PVAnal_realloc_memories(self);

    (*self->mode_func_ptr)(self);

    return (PyObject *)self;
}

static PyObject * PVAnal_getServer(PVAnal* self) { GET_SERVER };
static PyObject * PVAnal_getStream(PVAnal* self) { GET_STREAM };
static PyObject * PVAnal_getPVStream(PVAnal* self) { GET_PV_STREAM };

static PyObject * PVAnal_play(PVAnal *self, PyObject *args, PyObject *kwds) { PLAY };
static PyObject * PVAnal_stop(PVAnal *self) { STOP };

static PyObject *
PVAnal_setSize(PVAnal *self, PyObject *arg)
{    
    int k;
    if (PyLong_Check(arg) || PyInt_Check(arg)) {
        self->size = PyInt_AsLong(arg);
        if (!isPowerOfTwo(self->size)) {
            k = 1;
            while (k < self->size)
                k *= 2;
            self->size = k;
            printf("FFT size must be a power-of-2, using the next power-of-2 greater than size : %d\n", self->size);
        }
        PVAnal_realloc_memories(self);
    }    
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
PVAnal_setOverlaps(PVAnal *self, PyObject *arg)
{    
    int k;
    if (PyLong_Check(arg) || PyInt_Check(arg)) {
        self->olaps = PyInt_AsLong(arg);
        if (!isPowerOfTwo(self->olaps)) {
            k = 1;
            while (k < self->olaps)
                k *= 2;
            self->olaps = k;
            printf("FFT overlaps must be a power-of-2, using the next power-of-2 greater than olaps : %d\n", self->olaps);
        }
        PVAnal_realloc_memories(self);
    }    
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
PVAnal_setWinType(PVAnal *self, PyObject *arg)
{    
    if (PyLong_Check(arg) || PyInt_Check(arg)) {
        self->wintype = PyInt_AsLong(arg);
        gen_window(self->window, self->size, self->wintype);
    }    
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyMemberDef PVAnal_members[] = {
{"server", T_OBJECT_EX, offsetof(PVAnal, server), 0, "Pyo server."},
{"stream", T_OBJECT_EX, offsetof(PVAnal, stream), 0, "Stream object."},
{"pv_stream", T_OBJECT_EX, offsetof(PVAnal, pv_stream), 0, "Phase Vocoder Stream object."},
{"input", T_OBJECT_EX, offsetof(PVAnal, input), 0, "FFT sound object."},
{NULL}  /* Sentinel */
};

static PyMethodDef PVAnal_methods[] = {
{"getServer", (PyCFunction)PVAnal_getServer, METH_NOARGS, "Returns server object."},
{"_getStream", (PyCFunction)PVAnal_getStream, METH_NOARGS, "Returns stream object."},
{"_getPVStream", (PyCFunction)PVAnal_getPVStream, METH_NOARGS, "Returns pvstream object."},
{"play", (PyCFunction)PVAnal_play, METH_VARARGS|METH_KEYWORDS, "Starts computing without sending sound to soundcard."},
{"stop", (PyCFunction)PVAnal_stop, METH_NOARGS, "Stops computing."},
{"setSize", (PyCFunction)PVAnal_setSize, METH_O, "Sets a new FFT size."},
{"setOverlaps", (PyCFunction)PVAnal_setOverlaps, METH_O, "Sets a new number of overlaps."},
{"setWinType", (PyCFunction)PVAnal_setWinType, METH_O, "Sets a new window type."},
{NULL}  /* Sentinel */
};

PyTypeObject PVAnalType = {
PyObject_HEAD_INIT(NULL)
0,                                              /*ob_size*/
"_pyo.PVAnal_base",                                   /*tp_name*/
sizeof(PVAnal),                                 /*tp_basicsize*/
0,                                              /*tp_itemsize*/
(destructor)PVAnal_dealloc,                     /*tp_dealloc*/
0,                                              /*tp_print*/
0,                                              /*tp_getattr*/
0,                                              /*tp_setattr*/
0,                                              /*tp_compare*/
0,                                              /*tp_repr*/
0,                              /*tp_as_number*/
0,                                              /*tp_as_sequence*/
0,                                              /*tp_as_mapping*/
0,                                              /*tp_hash */
0,                                              /*tp_call*/
0,                                              /*tp_str*/
0,                                              /*tp_getattro*/
0,                                              /*tp_setattro*/
0,                                              /*tp_as_buffer*/
Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_CHECKTYPES, /*tp_flags*/
"PVAnal objects. Phase Vocoder analysis object.",           /* tp_doc */
(traverseproc)PVAnal_traverse,                  /* tp_traverse */
(inquiry)PVAnal_clear,                          /* tp_clear */
0,                                              /* tp_richcompare */
0,                                              /* tp_weaklistoffset */
0,                                              /* tp_iter */
0,                                              /* tp_iternext */
PVAnal_methods,                                 /* tp_methods */
PVAnal_members,                                 /* tp_members */
0,                                              /* tp_getset */
0,                                              /* tp_base */
0,                                              /* tp_dict */
0,                                              /* tp_descr_get */
0,                                              /* tp_descr_set */
0,                                              /* tp_dictoffset */
0,                          /* tp_init */
0,                                              /* tp_alloc */
PVAnal_new,                                     /* tp_new */
};

/*****************/
/**** PVSynth ****/
/*****************/
typedef struct {
    pyo_audio_HEAD
    PyObject *input;
    PVStream *input_stream;
    PVStream *pv_stream;
    int size;
    int hsize;
    int olaps;
    int hopsize;
    int wintype;
    int inputLatency;
    int overcount;
    MYFLT ampscl;
    MYFLT factor;
    MYFLT scale;
    MYFLT *output_buffer;
    MYFLT *outputAccum;
    MYFLT *inframe;
    MYFLT *outframe; 
    MYFLT *real; 
    MYFLT *imag; 
    MYFLT *sumPhase;
    MYFLT **twiddle;
    MYFLT *window;
    int modebuffer[2]; // need at least 2 slots for mul & add
} PVSynth;


static void
PVSynth_realloc_memories(PVSynth *self) {
    int i, n8;
    self->hsize = self->size / 2;
    self->hopsize = self->size / self->olaps;
    self->factor = self->hopsize * TWOPI / self->sr;
    self->scale = self->sr / self->size;
    self->inputLatency = self->size - self->hopsize;
    self->overcount = 0;
    self->ampscl = 1.0 / MYSQRT(self->olaps);
    n8 = self->size >> 3;
    self->output_buffer = (MYFLT *)realloc(self->output_buffer, self->size * sizeof(MYFLT));
    self->inframe = (MYFLT *)realloc(self->inframe, self->size * sizeof(MYFLT));
    self->outframe = (MYFLT *)realloc(self->outframe, self->size * sizeof(MYFLT));    
    for (i=0; i<self->size; i++)
        self->output_buffer[i] = self->inframe[i] = self->outframe[i] = 0.0;
    self->sumPhase = (MYFLT *)realloc(self->sumPhase, self->hsize * sizeof(MYFLT)); 
    self->real = (MYFLT *)realloc(self->real, self->hsize * sizeof(MYFLT)); 
    self->imag = (MYFLT *)realloc(self->imag, self->hsize * sizeof(MYFLT)); 
    for (i=0; i<self->hsize; i++)
        self->sumPhase[i] = self->real[i] = self->imag[i] = 0.0;
    self->outputAccum = (MYFLT *)realloc(self->outputAccum, (self->size+self->hopsize) * sizeof(MYFLT)); 
    for (i=0; i<(self->size+self->hopsize); i++)
        self->outputAccum[i] = 0.0;
    self->twiddle = (MYFLT **)realloc(self->twiddle, 4 * sizeof(MYFLT *));
    for(i=0; i<4; i++)
        self->twiddle[i] = (MYFLT *)malloc(n8 * sizeof(MYFLT));
    fft_compute_split_twiddle(self->twiddle, self->size);
    self->window = (MYFLT *)realloc(self->window, self->size * sizeof(MYFLT));
    gen_window(self->window, self->size, self->wintype);
}

static void
PVSynth_process(PVSynth *self) {
    int i, k, mod;
    MYFLT mag, phase, tmp;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVSynth_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->data[i] = self->output_buffer[count[i] - self->inputLatency];
        if (count[i] >= (self->size-1)) {
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                tmp = freq[self->overcount][k];                
                tmp = (tmp - k * self->scale) * self->factor;
                self->sumPhase[k] += tmp;
                phase = self->sumPhase[k];
                self->real[k] = mag * MYCOS(phase);
                self->imag[k] = mag * MYSIN(phase);
            }
            
            self->inframe[0] = self->real[0];
            self->inframe[self->hsize] = 0.0;
            for (k=1; k<self->hsize; k++) {
                self->inframe[k] = self->real[k];
                self->inframe[self->size - k] = self->imag[k];
            }
            irealfft_split(self->inframe, self->outframe, self->size, self->twiddle);
            mod = self->hopsize * self->overcount;
            for (k=0; k<self->size; k++) {
                self->outputAccum[k] += self->outframe[(k+mod)%self->size] * self->window[k] * self->ampscl;
            }
            for (k=0; k<self->hopsize; k++) {
                self->output_buffer[k] = self->outputAccum[k];
            }
            for (k=0; k<self->size; k++) {
                self->outputAccum[k] = self->outputAccum[k + self->hopsize];
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void PVSynth_postprocessing_ii(PVSynth *self) { POST_PROCESSING_II };
static void PVSynth_postprocessing_ai(PVSynth *self) { POST_PROCESSING_AI };
static void PVSynth_postprocessing_ia(PVSynth *self) { POST_PROCESSING_IA };
static void PVSynth_postprocessing_aa(PVSynth *self) { POST_PROCESSING_AA };
static void PVSynth_postprocessing_ireva(PVSynth *self) { POST_PROCESSING_IREVA };
static void PVSynth_postprocessing_areva(PVSynth *self) { POST_PROCESSING_AREVA };
static void PVSynth_postprocessing_revai(PVSynth *self) { POST_PROCESSING_REVAI };
static void PVSynth_postprocessing_revaa(PVSynth *self) { POST_PROCESSING_REVAA };
static void PVSynth_postprocessing_revareva(PVSynth *self) { POST_PROCESSING_REVAREVA };

static void
PVSynth_setProcMode(PVSynth *self)
{
    int muladdmode;
    muladdmode = self->modebuffer[0] + self->modebuffer[1] * 10;

    self->proc_func_ptr = PVSynth_process;  

	switch (muladdmode) {
        case 0:        
            self->muladd_func_ptr = PVSynth_postprocessing_ii;
            break;
        case 1:    
            self->muladd_func_ptr = PVSynth_postprocessing_ai;
            break;
        case 2:    
            self->muladd_func_ptr = PVSynth_postprocessing_revai;
            break;
        case 10:        
            self->muladd_func_ptr = PVSynth_postprocessing_ia;
            break;
        case 11:    
            self->muladd_func_ptr = PVSynth_postprocessing_aa;
            break;
        case 12:    
            self->muladd_func_ptr = PVSynth_postprocessing_revaa;
            break;
        case 20:        
            self->muladd_func_ptr = PVSynth_postprocessing_ireva;
            break;
        case 21:    
            self->muladd_func_ptr = PVSynth_postprocessing_areva;
            break;
        case 22:    
            self->muladd_func_ptr = PVSynth_postprocessing_revareva;
            break;
    }
}

static void
PVSynth_compute_next_data_frame(PVSynth *self)
{
    (*self->proc_func_ptr)(self); 
    (*self->muladd_func_ptr)(self);
}

static int
PVSynth_traverse(PVSynth *self, visitproc visit, void *arg)
{
    pyo_VISIT
    Py_VISIT(self->input);
    Py_VISIT(self->input_stream);
    Py_VISIT(self->pv_stream);
    return 0;
}

static int 
PVSynth_clear(PVSynth *self)
{
    pyo_CLEAR
    Py_CLEAR(self->input);
    Py_CLEAR(self->input_stream);
    Py_CLEAR(self->pv_stream);
    return 0;
}

static void
PVSynth_dealloc(PVSynth* self)
{
    int i;
    pyo_DEALLOC
    free(self->output_buffer);
    free(self->outputAccum);
    free(self->inframe);
    free(self->outframe); 
    free(self->real); 
    free(self->imag); 
    free(self->sumPhase);
    for(i=0; i<4; i++) {
        free(self->twiddle[i]);
    }
    free(self->twiddle);
    free(self->window);
    PVSynth_clear(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
PVSynth_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    int i;
    PyObject *inputtmp, *input_streamtmp, *multmp=NULL, *addtmp=NULL;
    PVSynth *self;
    self = (PVSynth *)type->tp_alloc(type, 0);

	self->modebuffer[0] = 0;
	self->modebuffer[1] = 0;

    self->wintype = 2;
    INIT_OBJECT_COMMON
    Stream_setFunctionPtr(self->stream, PVSynth_compute_next_data_frame);
    self->mode_func_ptr = PVSynth_setProcMode;

    static char *kwlist[] = {"input", "wintype", "mul", "add", NULL};
    
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|iOO", kwlist, &inputtmp, &self->wintype, &multmp, &addtmp))
        Py_RETURN_NONE;

    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVSynth \"input\" argument must be a PyoPVObject.\n");
        if (PyInt_AsLong(PyObject_CallMethod(self->server, "getIsBooted", NULL))) {
            PyObject_CallMethod(self->server, "shutdown", NULL);
        }
        Py_Exit(1);
    }
    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;

    self->size = PVStream_getFFTsize(self->input_stream);
    self->olaps = PVStream_getOlaps(self->input_stream);
 
    if (multmp) {
        PyObject_CallMethod((PyObject *)self, "setMul", "O", multmp);
    }
    
    if (addtmp) {
        PyObject_CallMethod((PyObject *)self, "setAdd", "O", addtmp);
    }

    PyObject_CallMethod(self->server, "addStream", "O", self->stream);

    PVSynth_realloc_memories(self);

    (*self->mode_func_ptr)(self);

    return (PyObject *)self;
}

static PyObject *
PVSynth_setInput(PVSynth *self, PyObject *arg)
{
	PyObject *inputtmp, *input_streamtmp;

    inputtmp = arg;
    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVSynth \"input\" argument must be a PyoPVObject.\n");
        Py_INCREF(Py_None);
        return Py_None;
    }

    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;
    
	Py_INCREF(Py_None);
	return Py_None;
}	

static PyObject *
PVSynth_setWinType(PVSynth *self, PyObject *arg)
{    
    if (PyLong_Check(arg) || PyInt_Check(arg)) {
        self->wintype = PyInt_AsLong(arg);
        gen_window(self->window, self->size, self->wintype);
    }    
    
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject * PVSynth_getServer(PVSynth* self) { GET_SERVER };
static PyObject * PVSynth_getStream(PVSynth* self) { GET_STREAM };
static PyObject * PVSynth_getPVStream(PVSynth* self) { GET_PV_STREAM };
static PyObject * PVSynth_setMul(PVSynth *self, PyObject *arg) { SET_MUL };	
static PyObject * PVSynth_setAdd(PVSynth *self, PyObject *arg) { SET_ADD };	
static PyObject * PVSynth_setSub(PVSynth *self, PyObject *arg) { SET_SUB };	
static PyObject * PVSynth_setDiv(PVSynth *self, PyObject *arg) { SET_DIV };	

static PyObject * PVSynth_play(PVSynth *self, PyObject *args, PyObject *kwds) { PLAY };
static PyObject * PVSynth_out(PVSynth *self, PyObject *args, PyObject *kwds) { OUT };
static PyObject * PVSynth_stop(PVSynth *self) { STOP };

static PyObject * PVSynth_multiply(PVSynth *self, PyObject *arg) { MULTIPLY };
static PyObject * PVSynth_inplace_multiply(PVSynth *self, PyObject *arg) { INPLACE_MULTIPLY };
static PyObject * PVSynth_add(PVSynth *self, PyObject *arg) { ADD };
static PyObject * PVSynth_inplace_add(PVSynth *self, PyObject *arg) { INPLACE_ADD };
static PyObject * PVSynth_sub(PVSynth *self, PyObject *arg) { SUB };
static PyObject * PVSynth_inplace_sub(PVSynth *self, PyObject *arg) { INPLACE_SUB };
static PyObject * PVSynth_div(PVSynth *self, PyObject *arg) { DIV };
static PyObject * PVSynth_inplace_div(PVSynth *self, PyObject *arg) { INPLACE_DIV };

static PyMemberDef PVSynth_members[] = {
{"server", T_OBJECT_EX, offsetof(PVSynth, server), 0, "Pyo server."},
{"stream", T_OBJECT_EX, offsetof(PVSynth, stream), 0, "Stream object."},
{"pv_stream", T_OBJECT_EX, offsetof(PVSynth, pv_stream), 0, "Phase Vocoder Stream object."},
{"input", T_OBJECT_EX, offsetof(PVSynth, input), 0, "FFT sound object."},
{"mul", T_OBJECT_EX, offsetof(PVSynth, mul), 0, "Mul factor."},
{"add", T_OBJECT_EX, offsetof(PVSynth, add), 0, "Add factor."},
{NULL}  /* Sentinel */
};

static PyMethodDef PVSynth_methods[] = {
{"getServer", (PyCFunction)PVSynth_getServer, METH_NOARGS, "Returns server object."},
{"_getStream", (PyCFunction)PVSynth_getStream, METH_NOARGS, "Returns stream object."},
{"_getPVStream", (PyCFunction)PVSynth_getPVStream, METH_NOARGS, "Returns pvstream object."},
{"play", (PyCFunction)PVSynth_play, METH_VARARGS|METH_KEYWORDS, "Starts computing without sending sound to soundcard."},
{"out", (PyCFunction)PVSynth_out, METH_VARARGS|METH_KEYWORDS, "Starts computing and sends sound to soundcard channel speficied by argument."},
{"stop", (PyCFunction)PVSynth_stop, METH_NOARGS, "Stops computing."},
{"setInput", (PyCFunction)PVSynth_setInput, METH_O, "Sets a new input object."},
{"setWinType", (PyCFunction)PVSynth_setWinType, METH_O, "Sets a new window type."},
{"setMul", (PyCFunction)PVSynth_setMul, METH_O, "Sets oscillator mul factor."},
{"setAdd", (PyCFunction)PVSynth_setAdd, METH_O, "Sets oscillator add factor."},
{"setSub", (PyCFunction)PVSynth_setSub, METH_O, "Sets inverse add factor."},
{"setDiv", (PyCFunction)PVSynth_setDiv, METH_O, "Sets inverse mul factor."},
{NULL}  /* Sentinel */
};

static PyNumberMethods PVSynth_as_number = {
(binaryfunc)PVSynth_add,                         /*nb_add*/
(binaryfunc)PVSynth_sub,                         /*nb_subtract*/
(binaryfunc)PVSynth_multiply,                    /*nb_multiply*/
(binaryfunc)PVSynth_div,                         /*nb_divide*/
0,                                              /*nb_remainder*/
0,                                              /*nb_divmod*/
0,                                              /*nb_power*/
0,                                              /*nb_neg*/
0,                                              /*nb_pos*/
0,                                              /*(unaryfunc)array_abs,*/
0,                                              /*nb_nonzero*/
0,                                              /*nb_invert*/
0,                                              /*nb_lshift*/
0,                                              /*nb_rshift*/
0,                                              /*nb_and*/
0,                                              /*nb_xor*/
0,                                              /*nb_or*/
0,                                              /*nb_coerce*/
0,                                              /*nb_int*/
0,                                              /*nb_long*/
0,                                              /*nb_float*/
0,                                              /*nb_oct*/
0,                                              /*nb_hex*/
(binaryfunc)PVSynth_inplace_add,                 /*inplace_add*/
(binaryfunc)PVSynth_inplace_sub,                 /*inplace_subtract*/
(binaryfunc)PVSynth_inplace_multiply,            /*inplace_multiply*/
(binaryfunc)PVSynth_inplace_div,                                              /*inplace_divide*/
0,                                              /*inplace_remainder*/
0,                                              /*inplace_power*/
0,                                              /*inplace_lshift*/
0,                                              /*inplace_rshift*/
0,                                              /*inplace_and*/
0,                                              /*inplace_xor*/
0,                                              /*inplace_or*/
0,                                              /*nb_floor_divide*/
0,                                              /*nb_true_divide*/
0,                                              /*nb_inplace_floor_divide*/
0,                                              /*nb_inplace_true_divide*/
0,                                              /* nb_index */
};

PyTypeObject PVSynthType = {
PyObject_HEAD_INIT(NULL)
0,                                              /*ob_size*/
"_pyo.PVSynth_base",                                   /*tp_name*/
sizeof(PVSynth),                                 /*tp_basicsize*/
0,                                              /*tp_itemsize*/
(destructor)PVSynth_dealloc,                     /*tp_dealloc*/
0,                                              /*tp_print*/
0,                                              /*tp_getattr*/
0,                                              /*tp_setattr*/
0,                                              /*tp_compare*/
0,                                              /*tp_repr*/
&PVSynth_as_number,                              /*tp_as_number*/
0,                                              /*tp_as_sequence*/
0,                                              /*tp_as_mapping*/
0,                                              /*tp_hash */
0,                                              /*tp_call*/
0,                                              /*tp_str*/
0,                                              /*tp_getattro*/
0,                                              /*tp_setattro*/
0,                                              /*tp_as_buffer*/
Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_CHECKTYPES, /*tp_flags*/
"PVSynth objects. Phase Vocoder synthesis object.",           /* tp_doc */
(traverseproc)PVSynth_traverse,                  /* tp_traverse */
(inquiry)PVSynth_clear,                          /* tp_clear */
0,                                              /* tp_richcompare */
0,                                              /* tp_weaklistoffset */
0,                                              /* tp_iter */
0,                                              /* tp_iternext */
PVSynth_methods,                                 /* tp_methods */
PVSynth_members,                                 /* tp_members */
0,                                              /* tp_getset */
0,                                              /* tp_base */
0,                                              /* tp_dict */
0,                                              /* tp_descr_get */
0,                                              /* tp_descr_set */
0,                                              /* tp_dictoffset */
0,                          /* tp_init */
0,                                              /* tp_alloc */
PVSynth_new,                                     /* tp_new */
};

/*****************/
/** PVTranspose **/
/*****************/
typedef struct {
    pyo_audio_HEAD
    PyObject *input;
    PVStream *input_stream;
    PVStream *pv_stream;
    PyObject *transpo;
    Stream *transpo_stream;
    int size;
    int olaps;
    int hsize;
    int hopsize;
    int overcount;
    MYFLT **magn;
    MYFLT **freq;
    int *count;
    int modebuffer[1];
} PVTranspose;

static void
PVTranspose_realloc_memories(PVTranspose *self) {
    int i, j, inputLatency;
    self->hsize = self->size / 2;
    self->hopsize = self->size / self->olaps;
    inputLatency = self->size - self->hopsize;
    self->overcount = 0;
    self->magn = (MYFLT **)realloc(self->magn, self->olaps * sizeof(MYFLT *)); 
    self->freq = (MYFLT **)realloc(self->freq, self->olaps * sizeof(MYFLT *));
    for (i=0; i<self->olaps; i++) {
        self->magn[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        self->freq[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        for (j=0; j<self->hsize; j++)
            self->magn[i][j] = self->freq[i][j] = 0.0;
    } 
    for (i=0; i<self->bufsize; i++)
        self->count[i] = inputLatency;
    PVStream_setFFTsize(self->pv_stream, self->size);
    PVStream_setOlaps(self->pv_stream, self->olaps);
    PVStream_setMagn(self->pv_stream, self->magn);
    PVStream_setFreq(self->pv_stream, self->freq);
    PVStream_setCount(self->pv_stream, self->count);
}

static void
PVTranspose_process_i(PVTranspose *self) {
    int i, k, index;
    MYFLT transpo;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    transpo = PyFloat_AS_DOUBLE(self->transpo);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVTranspose_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            for (k=0; k<self->hsize; k++) {
                self->magn[self->overcount][k] = 0.0;
                self->freq[self->overcount][k] = 0.0;
            }
            for (k=0; k<self->hsize; k++) {
                index = (int)(k * transpo);
                if (index < self->hsize) {
                    self->magn[self->overcount][index] += magn[self->overcount][k];
                    self->freq[self->overcount][index] = freq[self->overcount][k] * transpo;
                }
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVTranspose_process_a(PVTranspose *self) {
    int i, k, index;
    MYFLT transpo;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    MYFLT *tr = Stream_getData((Stream *)self->transpo_stream);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVTranspose_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            transpo = tr[i];
            for (k=0; k<self->hsize; k++) {
                self->magn[self->overcount][k] = 0.0;
                self->freq[self->overcount][k] = 0.0;
            }
            for (k=0; k<self->hsize; k++) {
                index = (int)(k * transpo);
                if (index < self->hsize) {
                    self->magn[self->overcount][index] += magn[self->overcount][k];
                    self->freq[self->overcount][index] = freq[self->overcount][k] * transpo;
                }
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVTranspose_setProcMode(PVTranspose *self)
{        
    int procmode;
    procmode = self->modebuffer[0];
    
	switch (procmode) {
        case 0:    
            self->proc_func_ptr = PVTranspose_process_i;
            break;
        case 1:    
            self->proc_func_ptr = PVTranspose_process_a;
            break;
    } 
}

static void
PVTranspose_compute_next_data_frame(PVTranspose *self)
{
    (*self->proc_func_ptr)(self); 
}

static int
PVTranspose_traverse(PVTranspose *self, visitproc visit, void *arg)
{
    pyo_VISIT
    Py_VISIT(self->input);
    Py_VISIT(self->input_stream);
    Py_VISIT(self->pv_stream);
    Py_VISIT(self->transpo);    
    Py_VISIT(self->transpo_stream);    
    return 0;
}

static int 
PVTranspose_clear(PVTranspose *self)
{
    pyo_CLEAR
    Py_CLEAR(self->input);
    Py_CLEAR(self->input_stream);
    Py_CLEAR(self->pv_stream);
    Py_CLEAR(self->transpo);    
    Py_CLEAR(self->transpo_stream);    
    return 0;
}

static void
PVTranspose_dealloc(PVTranspose* self)
{
    int i;
    pyo_DEALLOC
    for(i=0; i<self->olaps; i++) {
        free(self->magn[i]);
        free(self->freq[i]);
    }
    free(self->magn);
    free(self->freq);
    free(self->count);
    PVTranspose_clear(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
PVTranspose_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    int i;
    PyObject *inputtmp, *input_streamtmp, *transpotmp;
    PVTranspose *self;
    self = (PVTranspose *)type->tp_alloc(type, 0);

    self->transpo = PyFloat_FromDouble(1);
    self->size = 1024;
    self->olaps = 4;
    INIT_OBJECT_COMMON
    Stream_setFunctionPtr(self->stream, PVTranspose_compute_next_data_frame);
    self->mode_func_ptr = PVTranspose_setProcMode;

    static char *kwlist[] = {"input", "transpo", NULL};
    
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|O", kwlist, &inputtmp, &transpotmp))
        Py_RETURN_NONE;

    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVTranspose \"input\" argument must be a PyoPVObject.\n");
        if (PyInt_AsLong(PyObject_CallMethod(self->server, "getIsBooted", NULL))) {
            PyObject_CallMethod(self->server, "shutdown", NULL);
        }
        Py_Exit(1);
    }
    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;

    self->size = PVStream_getFFTsize(self->input_stream);
    self->olaps = PVStream_getOlaps(self->input_stream);

    if (transpotmp) {
        PyObject_CallMethod((PyObject *)self, "setTranspo", "O", transpotmp);
    }
 
    PyObject_CallMethod(self->server, "addStream", "O", self->stream);

    MAKE_NEW_PV_STREAM(self->pv_stream, &PVStreamType, NULL);

    self->count = (int *)realloc(self->count, self->bufsize * sizeof(int));

    PVTranspose_realloc_memories(self);

    (*self->mode_func_ptr)(self);

    return (PyObject *)self;
}

static PyObject * PVTranspose_getServer(PVTranspose* self) { GET_SERVER };
static PyObject * PVTranspose_getStream(PVTranspose* self) { GET_STREAM };
static PyObject * PVTranspose_getPVStream(PVTranspose* self) { GET_PV_STREAM };

static PyObject * PVTranspose_play(PVTranspose *self, PyObject *args, PyObject *kwds) { PLAY };
static PyObject * PVTranspose_stop(PVTranspose *self) { STOP };

static PyObject *
PVTranspose_setInput(PVTranspose *self, PyObject *arg)
{
	PyObject *inputtmp, *input_streamtmp;

    inputtmp = arg;
    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVTranspose \"input\" argument must be a PyoPVObject.\n");
        Py_INCREF(Py_None);
        return Py_None;
    }

    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;
    
	Py_INCREF(Py_None);
	return Py_None;
}	

static PyObject *
PVTranspose_setTranspo(PVTranspose *self, PyObject *arg)
{
	PyObject *tmp, *streamtmp;
	
	if (arg == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
    
	int isNumber = PyNumber_Check(arg);
	
	tmp = arg;
	Py_INCREF(tmp);
	Py_DECREF(self->transpo);
	if (isNumber == 1) {
		self->transpo = PyNumber_Float(tmp);
        self->modebuffer[0] = 0;
	}
	else {
		self->transpo = tmp;
        streamtmp = PyObject_CallMethod((PyObject *)self->transpo, "_getStream", NULL);
        Py_INCREF(streamtmp);
        Py_XDECREF(self->transpo_stream);
        self->transpo_stream = (Stream *)streamtmp;
		self->modebuffer[0] = 1;
	}
    
    (*self->mode_func_ptr)(self);
    
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMemberDef PVTranspose_members[] = {
{"server", T_OBJECT_EX, offsetof(PVTranspose, server), 0, "Pyo server."},
{"stream", T_OBJECT_EX, offsetof(PVTranspose, stream), 0, "Stream object."},
{"pv_stream", T_OBJECT_EX, offsetof(PVTranspose, pv_stream), 0, "Phase Vocoder Stream object."},
{"input", T_OBJECT_EX, offsetof(PVTranspose, input), 0, "FFT sound object."},
{"transpo", T_OBJECT_EX, offsetof(PVTranspose, transpo), 0, "Transposition factor."},
{NULL}  /* Sentinel */
};

static PyMethodDef PVTranspose_methods[] = {
{"getServer", (PyCFunction)PVTranspose_getServer, METH_NOARGS, "Returns server object."},
{"_getStream", (PyCFunction)PVTranspose_getStream, METH_NOARGS, "Returns stream object."},
{"_getPVStream", (PyCFunction)PVTranspose_getPVStream, METH_NOARGS, "Returns pvstream object."},
{"setInput", (PyCFunction)PVTranspose_setInput, METH_O, "Sets a new input object."},
{"setTranspo", (PyCFunction)PVTranspose_setTranspo, METH_O, "Sets the transposition factor."},
{"play", (PyCFunction)PVTranspose_play, METH_VARARGS|METH_KEYWORDS, "Starts computing without sending sound to soundcard."},
{"stop", (PyCFunction)PVTranspose_stop, METH_NOARGS, "Stops computing."},
{NULL}  /* Sentinel */
};

PyTypeObject PVTransposeType = {
PyObject_HEAD_INIT(NULL)
0,                                              /*ob_size*/
"_pyo.PVTranspose_base",                                   /*tp_name*/
sizeof(PVTranspose),                                 /*tp_basicsize*/
0,                                              /*tp_itemsize*/
(destructor)PVTranspose_dealloc,                     /*tp_dealloc*/
0,                                              /*tp_print*/
0,                                              /*tp_getattr*/
0,                                              /*tp_setattr*/
0,                                              /*tp_compare*/
0,                                              /*tp_repr*/
0,                              /*tp_as_number*/
0,                                              /*tp_as_sequence*/
0,                                              /*tp_as_mapping*/
0,                                              /*tp_hash */
0,                                              /*tp_call*/
0,                                              /*tp_str*/
0,                                              /*tp_getattro*/
0,                                              /*tp_setattro*/
0,                                              /*tp_as_buffer*/
Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_CHECKTYPES, /*tp_flags*/
"PVTranspose objects. Spectral domain transposition.",           /* tp_doc */
(traverseproc)PVTranspose_traverse,                  /* tp_traverse */
(inquiry)PVTranspose_clear,                          /* tp_clear */
0,                                              /* tp_richcompare */
0,                                              /* tp_weaklistoffset */
0,                                              /* tp_iter */
0,                                              /* tp_iternext */
PVTranspose_methods,                                 /* tp_methods */
PVTranspose_members,                                 /* tp_members */
0,                                              /* tp_getset */
0,                                              /* tp_base */
0,                                              /* tp_dict */
0,                                              /* tp_descr_get */
0,                                              /* tp_descr_set */
0,                                              /* tp_dictoffset */
0,                          /* tp_init */
0,                                              /* tp_alloc */
PVTranspose_new,                                     /* tp_new */
};

/*****************/
/** PVVerb **/
/*****************/
typedef struct {
    pyo_audio_HEAD
    PyObject *input;
    PVStream *input_stream;
    PVStream *pv_stream;
    PyObject *revtime;
    Stream *revtime_stream;
    PyObject *damp;
    Stream *damp_stream;
    int size;
    int olaps;
    int hsize;
    int hopsize;
    int overcount;
    MYFLT *l_magn;
    MYFLT **magn;
    MYFLT **freq;
    int *count;
    int modebuffer[2];
} PVVerb;

static void
PVVerb_realloc_memories(PVVerb *self) {
    int i, j, inputLatency;
    self->hsize = self->size / 2;
    self->hopsize = self->size / self->olaps;
    inputLatency = self->size - self->hopsize;
    self->overcount = 0;
    self->l_magn = (MYFLT *)realloc(self->l_magn, self->hsize * sizeof(MYFLT)); 
    for (i=0; i<self->hsize; i++)
        self->l_magn[i] = 0.0;
    self->magn = (MYFLT **)realloc(self->magn, self->olaps * sizeof(MYFLT *));
    self->freq = (MYFLT **)realloc(self->freq, self->olaps * sizeof(MYFLT *));
    for (i=0; i<self->olaps; i++) {
        self->magn[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        self->freq[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        for (j=0; j<self->hsize; j++)
            self->magn[i][j] = self->freq[i][j] = 0.0;
    } 
    for (i=0; i<self->bufsize; i++)
        self->count[i] = inputLatency;
    PVStream_setFFTsize(self->pv_stream, self->size);
    PVStream_setOlaps(self->pv_stream, self->olaps);
    PVStream_setMagn(self->pv_stream, self->magn);
    PVStream_setFreq(self->pv_stream, self->freq);
    PVStream_setCount(self->pv_stream, self->count);
}

static void
PVVerb_process_ii(PVVerb *self) {
    int i, k;
    MYFLT revtime, damp, mag, amp;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    revtime = PyFloat_AS_DOUBLE(self->revtime);
    damp = PyFloat_AS_DOUBLE(self->damp);
    if (revtime < 0.0)
        revtime = 0.0;
    else if (revtime > 1.0)
        revtime = 1.0;
    revtime = revtime * 0.25 + 0.75;
    if (damp < 0.0)
        damp = 0.0;
    else if (damp > 1.0)
        damp = 1.0;
    damp = damp * 0.003 + 0.997;

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVVerb_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            amp = 1.0;
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag > self->l_magn[k])
                    self->magn[self->overcount][k] = self->l_magn[k] = mag;
                else
                    self->magn[self->overcount][k] = self->l_magn[k] = mag + (self->l_magn[k] - mag) * revtime * amp;
                self->freq[self->overcount][k] = freq[self->overcount][k];
                amp *= damp;
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVVerb_process_ai(PVVerb *self) {
    int i, k;
    MYFLT revtime, damp, mag, amp;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    MYFLT *rvt = Stream_getData((Stream *)self->revtime_stream);
    damp = PyFloat_AS_DOUBLE(self->damp);
    if (damp < 0.0)
        damp = 0.0;
    else if (damp > 1.0)
        damp = 1.0;
    damp = damp * 0.003 + 0.997;

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVVerb_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            revtime = rvt[i];
            if (revtime < 0.0)
                revtime = 0.0;
            else if (revtime > 1.0)
                revtime = 1.0;
            revtime = revtime * 0.25 + 0.75;
            amp = 1.0;
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag > self->l_magn[k])
                    self->magn[self->overcount][k] = self->l_magn[k] = mag;
                else
                    self->magn[self->overcount][k] = self->l_magn[k] = mag + (self->l_magn[k] - mag) * revtime * amp;
                self->freq[self->overcount][k] = freq[self->overcount][k];
                amp *= damp;
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVVerb_process_ia(PVVerb *self) {
    int i, k;
    MYFLT revtime, damp, mag, amp;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    revtime = PyFloat_AS_DOUBLE(self->revtime);
    MYFLT *dmp = Stream_getData((Stream *)self->damp_stream);
    if (revtime < 0.0)
        revtime = 0.0;
    else if (revtime > 1.0)
        revtime = 1.0;
    revtime = revtime * 0.25 + 0.75;

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVVerb_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            damp = dmp[i];
            if (damp < 0.0)
                damp = 0.0;
            else if (damp > 1.0)
                damp = 1.0;
            damp = damp * 0.003 + 0.997;
            amp = 1.0;
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag > self->l_magn[k])
                    self->magn[self->overcount][k] = self->l_magn[k] = mag;
                else
                    self->magn[self->overcount][k] = self->l_magn[k] = mag + (self->l_magn[k] - mag) * revtime * amp;
                self->freq[self->overcount][k] = freq[self->overcount][k];
                amp *= damp;
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVVerb_process_aa(PVVerb *self) {
    int i, k;
    MYFLT revtime, damp, mag, amp;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    MYFLT *rvt = Stream_getData((Stream *)self->revtime_stream);
    MYFLT *dmp = Stream_getData((Stream *)self->damp_stream);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVVerb_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            revtime = rvt[i];
            if (revtime < 0.0)
                revtime = 0.0;
            else if (revtime > 1.0)
                revtime = 1.0;
            revtime = revtime * 0.25 + 0.75;
            damp = dmp[i];
            if (damp < 0.0)
                damp = 0.0;
            else if (damp > 1.0)
                damp = 1.0;
            damp = damp * 0.003 + 0.997;
            amp = 1.0;
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag > self->l_magn[k])
                    self->magn[self->overcount][k] = self->l_magn[k] = mag;
                else
                    self->magn[self->overcount][k] = self->l_magn[k] = mag + (self->l_magn[k] - mag) * revtime * amp;
                self->freq[self->overcount][k] = freq[self->overcount][k];
                amp *= damp;
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVVerb_setProcMode(PVVerb *self)
{        
    int procmode;
    procmode = self->modebuffer[0] + self->modebuffer[1] * 10;
    
	switch (procmode) {
        case 0:    
            self->proc_func_ptr = PVVerb_process_ii;
            break;
        case 1:    
            self->proc_func_ptr = PVVerb_process_ai;
            break;
        case 10:    
            self->proc_func_ptr = PVVerb_process_ia;
            break;
        case 11:    
            self->proc_func_ptr = PVVerb_process_aa;
            break;
    } 
}

static void
PVVerb_compute_next_data_frame(PVVerb *self)
{
    (*self->proc_func_ptr)(self); 
}

static int
PVVerb_traverse(PVVerb *self, visitproc visit, void *arg)
{
    pyo_VISIT
    Py_VISIT(self->input);
    Py_VISIT(self->input_stream);
    Py_VISIT(self->pv_stream);
    Py_VISIT(self->revtime);    
    Py_VISIT(self->revtime_stream);    
    Py_VISIT(self->damp);    
    Py_VISIT(self->damp_stream);    
    return 0;
}

static int 
PVVerb_clear(PVVerb *self)
{
    pyo_CLEAR
    Py_CLEAR(self->input);
    Py_CLEAR(self->input_stream);
    Py_CLEAR(self->pv_stream);
    Py_CLEAR(self->revtime);    
    Py_CLEAR(self->revtime_stream);    
    Py_CLEAR(self->damp);    
    Py_CLEAR(self->damp_stream);    
    return 0;
}

static void
PVVerb_dealloc(PVVerb* self)
{
    int i;
    pyo_DEALLOC
    for(i=0; i<self->olaps; i++) {
        free(self->magn[i]);
        free(self->freq[i]);
    }
    free(self->magn);
    free(self->freq);
    free(self->l_magn);
    free(self->count);
    PVVerb_clear(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
PVVerb_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    int i;
    PyObject *inputtmp, *input_streamtmp, *revtimetmp=NULL, *damptmp=NULL;
    PVVerb *self;
    self = (PVVerb *)type->tp_alloc(type, 0);

    self->revtime = PyFloat_FromDouble(0.75);
    self->damp = PyFloat_FromDouble(0.75);
    self->size = 1024;
    self->olaps = 4;
    INIT_OBJECT_COMMON
    Stream_setFunctionPtr(self->stream, PVVerb_compute_next_data_frame);
    self->mode_func_ptr = PVVerb_setProcMode;

    static char *kwlist[] = {"input", "revtime", "damp", NULL};
    
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|OO", kwlist, &inputtmp, &revtimetmp, &damptmp))
        Py_RETURN_NONE;

    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVVerb \"input\" argument must be a PyoPVObject.\n");
        if (PyInt_AsLong(PyObject_CallMethod(self->server, "getIsBooted", NULL))) {
            PyObject_CallMethod(self->server, "shutdown", NULL);
        }
        Py_Exit(1);
    }
    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;

    self->size = PVStream_getFFTsize(self->input_stream);
    self->olaps = PVStream_getOlaps(self->input_stream);

    if (revtimetmp) {
        PyObject_CallMethod((PyObject *)self, "setRevtime", "O", revtimetmp);
    }

    if (damptmp) {
        PyObject_CallMethod((PyObject *)self, "setDamp", "O", damptmp);
    }
 
    PyObject_CallMethod(self->server, "addStream", "O", self->stream);

    MAKE_NEW_PV_STREAM(self->pv_stream, &PVStreamType, NULL);

    self->count = (int *)realloc(self->count, self->bufsize * sizeof(int));

    PVVerb_realloc_memories(self);

    (*self->mode_func_ptr)(self);

    return (PyObject *)self;
}

static PyObject * PVVerb_getServer(PVVerb* self) { GET_SERVER };
static PyObject * PVVerb_getStream(PVVerb* self) { GET_STREAM };
static PyObject * PVVerb_getPVStream(PVVerb* self) { GET_PV_STREAM };

static PyObject * PVVerb_play(PVVerb *self, PyObject *args, PyObject *kwds) { PLAY };
static PyObject * PVVerb_stop(PVVerb *self) { STOP };

static PyObject *
PVVerb_setInput(PVVerb *self, PyObject *arg)
{
	PyObject *inputtmp, *input_streamtmp;

    inputtmp = arg;
    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVVerb \"input\" argument must be a PyoPVObject.\n");
        Py_INCREF(Py_None);
        return Py_None;
    }

    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;
    
	Py_INCREF(Py_None);
	return Py_None;
}	

static PyObject *
PVVerb_setRevtime(PVVerb *self, PyObject *arg)
{
	PyObject *tmp, *streamtmp;
	
	if (arg == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
    
	int isNumber = PyNumber_Check(arg);
	
	tmp = arg;
	Py_INCREF(tmp);
	Py_DECREF(self->revtime);
	if (isNumber == 1) {
		self->revtime = PyNumber_Float(tmp);
        self->modebuffer[0] = 0;
	}
	else {
		self->revtime = tmp;
        streamtmp = PyObject_CallMethod((PyObject *)self->revtime, "_getStream", NULL);
        Py_INCREF(streamtmp);
        Py_XDECREF(self->revtime_stream);
        self->revtime_stream = (Stream *)streamtmp;
		self->modebuffer[0] = 1;
	}
    
    (*self->mode_func_ptr)(self);
    
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
PVVerb_setDamp(PVVerb *self, PyObject *arg)
{
	PyObject *tmp, *streamtmp;
	
	if (arg == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
    
	int isNumber = PyNumber_Check(arg);
	
	tmp = arg;
	Py_INCREF(tmp);
	Py_DECREF(self->damp);
	if (isNumber == 1) {
		self->damp = PyNumber_Float(tmp);
        self->modebuffer[1] = 0;
	}
	else {
		self->damp = tmp;
        streamtmp = PyObject_CallMethod((PyObject *)self->damp, "_getStream", NULL);
        Py_INCREF(streamtmp);
        Py_XDECREF(self->damp_stream);
        self->damp_stream = (Stream *)streamtmp;
		self->modebuffer[1] = 1;
	}
    
    (*self->mode_func_ptr)(self);
    
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMemberDef PVVerb_members[] = {
{"server", T_OBJECT_EX, offsetof(PVVerb, server), 0, "Pyo server."},
{"stream", T_OBJECT_EX, offsetof(PVVerb, stream), 0, "Stream object."},
{"pv_stream", T_OBJECT_EX, offsetof(PVVerb, pv_stream), 0, "Phase Vocoder Stream object."},
{"input", T_OBJECT_EX, offsetof(PVVerb, input), 0, "FFT sound object."},
{"revtime", T_OBJECT_EX, offsetof(PVVerb, revtime), 0, "reverberation factor."},
{"damp", T_OBJECT_EX, offsetof(PVVerb, damp), 0, "High frequency damping factor."},
{NULL}  /* Sentinel */
};

static PyMethodDef PVVerb_methods[] = {
{"getServer", (PyCFunction)PVVerb_getServer, METH_NOARGS, "Returns server object."},
{"_getStream", (PyCFunction)PVVerb_getStream, METH_NOARGS, "Returns stream object."},
{"_getPVStream", (PyCFunction)PVVerb_getPVStream, METH_NOARGS, "Returns pvstream object."},
{"setInput", (PyCFunction)PVVerb_setInput, METH_O, "Sets a new input object."},
{"setRevtime", (PyCFunction)PVVerb_setRevtime, METH_O, "Sets the reverberation factor."},
{"setDamp", (PyCFunction)PVVerb_setDamp, METH_O, "Sets the high frequency damping factor."},
{"play", (PyCFunction)PVVerb_play, METH_VARARGS|METH_KEYWORDS, "Starts computing without sending sound to soundcard."},
{"stop", (PyCFunction)PVVerb_stop, METH_NOARGS, "Stops computing."},
{NULL}  /* Sentinel */
};

PyTypeObject PVVerbType = {
PyObject_HEAD_INIT(NULL)
0,                                              /*ob_size*/
"_pyo.PVVerb_base",                                   /*tp_name*/
sizeof(PVVerb),                                 /*tp_basicsize*/
0,                                              /*tp_itemsize*/
(destructor)PVVerb_dealloc,                     /*tp_dealloc*/
0,                                              /*tp_print*/
0,                                              /*tp_getattr*/
0,                                              /*tp_setattr*/
0,                                              /*tp_compare*/
0,                                              /*tp_repr*/
0,                              /*tp_as_number*/
0,                                              /*tp_as_sequence*/
0,                                              /*tp_as_mapping*/
0,                                              /*tp_hash */
0,                                              /*tp_call*/
0,                                              /*tp_str*/
0,                                              /*tp_getattro*/
0,                                              /*tp_setattro*/
0,                                              /*tp_as_buffer*/
Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_CHECKTYPES, /*tp_flags*/
"PVVerb objects. Spectral reverberation.",           /* tp_doc */
(traverseproc)PVVerb_traverse,                  /* tp_traverse */
(inquiry)PVVerb_clear,                          /* tp_clear */
0,                                              /* tp_richcompare */
0,                                              /* tp_weaklistoffset */
0,                                              /* tp_iter */
0,                                              /* tp_iternext */
PVVerb_methods,                                 /* tp_methods */
PVVerb_members,                                 /* tp_members */
0,                                              /* tp_getset */
0,                                              /* tp_base */
0,                                              /* tp_dict */
0,                                              /* tp_descr_get */
0,                                              /* tp_descr_set */
0,                                              /* tp_dictoffset */
0,                          /* tp_init */
0,                                              /* tp_alloc */
PVVerb_new,                                     /* tp_new */
};

/*****************/
/** PVGate **/
/*****************/
typedef struct {
    pyo_audio_HEAD
    PyObject *input;
    PVStream *input_stream;
    PVStream *pv_stream;
    PyObject *thresh;
    Stream *thresh_stream;
    PyObject *damp;
    Stream *damp_stream;
    int size;
    int olaps;
    int hsize;
    int hopsize;
    int overcount;
    MYFLT **magn;
    MYFLT **freq;
    int *count;
    int modebuffer[2];
} PVGate;

static void
PVGate_realloc_memories(PVGate *self) {
    int i, j, inputLatency;
    self->hsize = self->size / 2;
    self->hopsize = self->size / self->olaps;
    inputLatency = self->size - self->hopsize;
    self->overcount = 0;
    self->magn = (MYFLT **)realloc(self->magn, self->olaps * sizeof(MYFLT *));
    self->freq = (MYFLT **)realloc(self->freq, self->olaps * sizeof(MYFLT *));
    for (i=0; i<self->olaps; i++) {
        self->magn[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        self->freq[i] = (MYFLT *)malloc(self->hsize * sizeof(MYFLT));
        for (j=0; j<self->hsize; j++)
            self->magn[i][j] = self->freq[i][j] = 0.0;
    } 
    for (i=0; i<self->bufsize; i++)
        self->count[i] = inputLatency;
    PVStream_setFFTsize(self->pv_stream, self->size);
    PVStream_setOlaps(self->pv_stream, self->olaps);
    PVStream_setMagn(self->pv_stream, self->magn);
    PVStream_setFreq(self->pv_stream, self->freq);
    PVStream_setCount(self->pv_stream, self->count);
}

static void
PVGate_process_ii(PVGate *self) {
    int i, k;
    MYFLT thresh, damp, mag;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    thresh = PyFloat_AS_DOUBLE(self->thresh);
    damp = PyFloat_AS_DOUBLE(self->damp);
    thresh = MYPOW(10.0, thresh * 0.05);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVGate_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag < thresh)
                    self->magn[self->overcount][k] = mag * damp;
                else
                    self->magn[self->overcount][k] = mag;
                self->freq[self->overcount][k] = freq[self->overcount][k];
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVGate_process_ai(PVGate *self) {
    int i, k;
    MYFLT thresh, damp, mag;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    MYFLT *rvt = Stream_getData((Stream *)self->thresh_stream);
    damp = PyFloat_AS_DOUBLE(self->damp);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVGate_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            thresh = rvt[i];
            thresh = MYPOW(10.0, thresh * 0.05);
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag < thresh)
                    self->magn[self->overcount][k] = mag * damp;
                else
                    self->magn[self->overcount][k] = mag;
                self->freq[self->overcount][k] = freq[self->overcount][k];
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVGate_process_ia(PVGate *self) {
    int i, k;
    MYFLT thresh, damp, mag;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    thresh = PyFloat_AS_DOUBLE(self->thresh);
    MYFLT *dmp = Stream_getData((Stream *)self->damp_stream);
    thresh = MYPOW(10.0, thresh * 0.05);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVGate_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            damp = dmp[i];
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag < thresh)
                    self->magn[self->overcount][k] = mag * damp;
                else
                    self->magn[self->overcount][k] = mag;
                self->freq[self->overcount][k] = freq[self->overcount][k];
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVGate_process_aa(PVGate *self) {
    int i, k;
    MYFLT thresh, damp, mag;
    MYFLT **magn = PVStream_getMagn((PVStream *)self->input_stream);
    MYFLT **freq = PVStream_getFreq((PVStream *)self->input_stream);
    int *count = PVStream_getCount((PVStream *)self->input_stream);
    int size = PVStream_getFFTsize((PVStream *)self->input_stream);
    int olaps = PVStream_getOlaps((PVStream *)self->input_stream);
    MYFLT *rvt = Stream_getData((Stream *)self->thresh_stream);
    MYFLT *dmp = Stream_getData((Stream *)self->damp_stream);

    if (self->size != size || self->olaps != olaps) {
        self->size = size;
        self->olaps = olaps;
        PVGate_realloc_memories(self);
    }

    for (i=0; i<self->bufsize; i++) {
        self->count[i] = count[i];
        if (count[i] >= (self->size-1)) {
            thresh = rvt[i];
            thresh = MYPOW(10.0, thresh * 0.05);
            damp = dmp[i];
            for (k=0; k<self->hsize; k++) {
                mag = magn[self->overcount][k];
                if (mag < thresh)
                    self->magn[self->overcount][k] = mag * damp;
                else
                    self->magn[self->overcount][k] = mag;
                self->freq[self->overcount][k] = freq[self->overcount][k];
            }
            self->overcount++;
            if (self->overcount >= self->olaps)
                self->overcount = 0;
        }
    }
}

static void
PVGate_setProcMode(PVGate *self)
{        
    int procmode;
    procmode = self->modebuffer[0] + self->modebuffer[1] * 10;
    
	switch (procmode) {
        case 0:    
            self->proc_func_ptr = PVGate_process_ii;
            break;
        case 1:    
            self->proc_func_ptr = PVGate_process_ai;
            break;
        case 10:    
            self->proc_func_ptr = PVGate_process_ia;
            break;
        case 11:    
            self->proc_func_ptr = PVGate_process_aa;
            break;
    } 
}

static void
PVGate_compute_next_data_frame(PVGate *self)
{
    (*self->proc_func_ptr)(self); 
}

static int
PVGate_traverse(PVGate *self, visitproc visit, void *arg)
{
    pyo_VISIT
    Py_VISIT(self->input);
    Py_VISIT(self->input_stream);
    Py_VISIT(self->pv_stream);
    Py_VISIT(self->thresh);    
    Py_VISIT(self->thresh_stream);    
    Py_VISIT(self->damp);    
    Py_VISIT(self->damp_stream);    
    return 0;
}

static int 
PVGate_clear(PVGate *self)
{
    pyo_CLEAR
    Py_CLEAR(self->input);
    Py_CLEAR(self->input_stream);
    Py_CLEAR(self->pv_stream);
    Py_CLEAR(self->thresh);    
    Py_CLEAR(self->thresh_stream);    
    Py_CLEAR(self->damp);    
    Py_CLEAR(self->damp_stream);    
    return 0;
}

static void
PVGate_dealloc(PVGate* self)
{
    int i;
    pyo_DEALLOC
    for(i=0; i<self->olaps; i++) {
        free(self->magn[i]);
        free(self->freq[i]);
    }
    free(self->magn);
    free(self->freq);
    free(self->count);
    PVGate_clear(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
PVGate_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    int i;
    PyObject *inputtmp, *input_streamtmp, *threshtmp=NULL, *damptmp=NULL;
    PVGate *self;
    self = (PVGate *)type->tp_alloc(type, 0);

    self->thresh = PyFloat_FromDouble(-20);
    self->damp = PyFloat_FromDouble(0.0);
    self->size = 1024;
    self->olaps = 4;
    INIT_OBJECT_COMMON
    Stream_setFunctionPtr(self->stream, PVGate_compute_next_data_frame);
    self->mode_func_ptr = PVGate_setProcMode;

    static char *kwlist[] = {"input", "thresh", "damp", NULL};
    
    if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|OO", kwlist, &inputtmp, &threshtmp, &damptmp))
        Py_RETURN_NONE;

    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVGate \"input\" argument must be a PyoPVObject.\n");
        if (PyInt_AsLong(PyObject_CallMethod(self->server, "getIsBooted", NULL))) {
            PyObject_CallMethod(self->server, "shutdown", NULL);
        }
        Py_Exit(1);
    }
    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;

    self->size = PVStream_getFFTsize(self->input_stream);
    self->olaps = PVStream_getOlaps(self->input_stream);

    if (threshtmp) {
        PyObject_CallMethod((PyObject *)self, "setThresh", "O", threshtmp);
    }

    if (damptmp) {
        PyObject_CallMethod((PyObject *)self, "setDamp", "O", damptmp);
    }
 
    PyObject_CallMethod(self->server, "addStream", "O", self->stream);

    MAKE_NEW_PV_STREAM(self->pv_stream, &PVStreamType, NULL);

    self->count = (int *)realloc(self->count, self->bufsize * sizeof(int));

    PVGate_realloc_memories(self);

    (*self->mode_func_ptr)(self);

    return (PyObject *)self;
}

static PyObject * PVGate_getServer(PVGate* self) { GET_SERVER };
static PyObject * PVGate_getStream(PVGate* self) { GET_STREAM };
static PyObject * PVGate_getPVStream(PVGate* self) { GET_PV_STREAM };

static PyObject * PVGate_play(PVGate *self, PyObject *args, PyObject *kwds) { PLAY };
static PyObject * PVGate_stop(PVGate *self) { STOP };

static PyObject *
PVGate_setInput(PVGate *self, PyObject *arg)
{
	PyObject *inputtmp, *input_streamtmp;

    inputtmp = arg;
    if ( PyObject_HasAttrString((PyObject *)inputtmp, "pv_stream") == 0 ) {
        PySys_WriteStderr("TypeError: PVGate \"input\" argument must be a PyoPVObject.\n");
        Py_INCREF(Py_None);
        return Py_None;
    }

    Py_INCREF(inputtmp);
    Py_XDECREF(self->input);
    self->input = inputtmp;
    input_streamtmp = PyObject_CallMethod((PyObject *)self->input, "_getPVStream", NULL);
    Py_INCREF(input_streamtmp);
    Py_XDECREF(self->input_stream);
    self->input_stream = (PVStream *)input_streamtmp;
    
	Py_INCREF(Py_None);
	return Py_None;
}	

static PyObject *
PVGate_setThresh(PVGate *self, PyObject *arg)
{
	PyObject *tmp, *streamtmp;
	
	if (arg == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
    
	int isNumber = PyNumber_Check(arg);
	
	tmp = arg;
	Py_INCREF(tmp);
	Py_DECREF(self->thresh);
	if (isNumber == 1) {
		self->thresh = PyNumber_Float(tmp);
        self->modebuffer[0] = 0;
	}
	else {
		self->thresh = tmp;
        streamtmp = PyObject_CallMethod((PyObject *)self->thresh, "_getStream", NULL);
        Py_INCREF(streamtmp);
        Py_XDECREF(self->thresh_stream);
        self->thresh_stream = (Stream *)streamtmp;
		self->modebuffer[0] = 1;
	}
    
    (*self->mode_func_ptr)(self);
    
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
PVGate_setDamp(PVGate *self, PyObject *arg)
{
	PyObject *tmp, *streamtmp;
	
	if (arg == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}
    
	int isNumber = PyNumber_Check(arg);
	
	tmp = arg;
	Py_INCREF(tmp);
	Py_DECREF(self->damp);
	if (isNumber == 1) {
		self->damp = PyNumber_Float(tmp);
        self->modebuffer[1] = 0;
	}
	else {
		self->damp = tmp;
        streamtmp = PyObject_CallMethod((PyObject *)self->damp, "_getStream", NULL);
        Py_INCREF(streamtmp);
        Py_XDECREF(self->damp_stream);
        self->damp_stream = (Stream *)streamtmp;
		self->modebuffer[1] = 1;
	}
    
    (*self->mode_func_ptr)(self);
    
	Py_INCREF(Py_None);
	return Py_None;
}

static PyMemberDef PVGate_members[] = {
{"server", T_OBJECT_EX, offsetof(PVGate, server), 0, "Pyo server."},
{"stream", T_OBJECT_EX, offsetof(PVGate, stream), 0, "Stream object."},
{"pv_stream", T_OBJECT_EX, offsetof(PVGate, pv_stream), 0, "Phase Vocoder Stream object."},
{"input", T_OBJECT_EX, offsetof(PVGate, input), 0, "FFT sound object."},
{"thresh", T_OBJECT_EX, offsetof(PVGate, thresh), 0, "Threshold factor."},
{"damp", T_OBJECT_EX, offsetof(PVGate, damp), 0, "Damping factor for bins below threshold."},
{NULL}  /* Sentinel */
};

static PyMethodDef PVGate_methods[] = {
{"getServer", (PyCFunction)PVGate_getServer, METH_NOARGS, "Returns server object."},
{"_getStream", (PyCFunction)PVGate_getStream, METH_NOARGS, "Returns stream object."},
{"_getPVStream", (PyCFunction)PVGate_getPVStream, METH_NOARGS, "Returns pvstream object."},
{"setInput", (PyCFunction)PVGate_setInput, METH_O, "Sets a new input object."},
{"setThresh", (PyCFunction)PVGate_setThresh, METH_O, "Sets the Threshold factor."},
{"setDamp", (PyCFunction)PVGate_setDamp, METH_O, "Sets the damping factor."},
{"play", (PyCFunction)PVGate_play, METH_VARARGS|METH_KEYWORDS, "Starts computing without sending sound to soundcard."},
{"stop", (PyCFunction)PVGate_stop, METH_NOARGS, "Stops computing."},
{NULL}  /* Sentinel */
};

PyTypeObject PVGateType = {
PyObject_HEAD_INIT(NULL)
0,                                              /*ob_size*/
"_pyo.PVGate_base",                                   /*tp_name*/
sizeof(PVGate),                                 /*tp_basicsize*/
0,                                              /*tp_itemsize*/
(destructor)PVGate_dealloc,                     /*tp_dealloc*/
0,                                              /*tp_print*/
0,                                              /*tp_getattr*/
0,                                              /*tp_setattr*/
0,                                              /*tp_compare*/
0,                                              /*tp_repr*/
0,                              /*tp_as_number*/
0,                                              /*tp_as_sequence*/
0,                                              /*tp_as_mapping*/
0,                                              /*tp_hash */
0,                                              /*tp_call*/
0,                                              /*tp_str*/
0,                                              /*tp_getattro*/
0,                                              /*tp_setattro*/
0,                                              /*tp_as_buffer*/
Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_CHECKTYPES, /*tp_flags*/
"PVGate objects. Spectral gate.",           /* tp_doc */
(traverseproc)PVGate_traverse,                  /* tp_traverse */
(inquiry)PVGate_clear,                          /* tp_clear */
0,                                              /* tp_richcompare */
0,                                              /* tp_weaklistoffset */
0,                                              /* tp_iter */
0,                                              /* tp_iternext */
PVGate_methods,                                 /* tp_methods */
PVGate_members,                                 /* tp_members */
0,                                              /* tp_getset */
0,                                              /* tp_base */
0,                                              /* tp_dict */
0,                                              /* tp_descr_get */
0,                                              /* tp_descr_set */
0,                                              /* tp_dictoffset */
0,                          /* tp_init */
0,                                              /* tp_alloc */
PVGate_new,                                     /* tp_new */
};
