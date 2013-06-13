"""
Phase vocoder.

The phase vocoder is a digital signal processing technique of potentially 
great musical significance. It can be used to perform very high fidelity 
time scaling, pitch transposition, and myriad other modifications of sounds.

"""

"""
Copyright 2011 Olivier Belanger

This file is part of pyo, a python module to help digital signal
processing script creation.

pyo is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

pyo is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with pyo.  If not, see <http://www.gnu.org/licenses/>.
"""
from _core import *
from _maps import *
from _widgets import createSpectrumWindow
from pattern import Pattern

class PVAnal(PyoPVObject):
    """
    Phase Vocoder analysis object.

    PVAnal takes an input sound and performs the phase vocoder
    analysis on it. This results in two streams, one for the bin's 
    magnitudes and the other for the bin's true frequencies. These 
    two streams are used by the PVxxx object family to transform 
    the input signal using spectral domain algorithms. The last
    object in the phase vocoder chain must be a PVSynh to perform
    the spectral to time domain conversion.
    
    :Parent: :py:class:`PyoPVObject`
    
    :Args:
    
        input : PyoObject
            Input signal to process.
        size : int {pow-of-two > 4}, optional
            FFT size. Must be a power of two greater than 4.
            Defaults to 1024.

            The FFT size is the number of samples used in each
            analysis frame. 
        overlaps : int, optional
            The number of overlaped analysis block. Must be a
            power of two. Defaults to 4.
            
            More overlaps can greatly improved sound quality 
            synthesis but it is also more CPU expensive.
        wintype : int, optional
            Shape of the envelope used to filter each input frame.
            Possible shapes are:
                0. rectangular (no windowing)
                1. Hamming
                2. Hanning (default)
                3. Bartlett (triangular)
                4. Blackman 3-term
                5. Blackman-Harris 4-term
                6. Blackman-Harris 7-term
                7. Tuckey (alpha = 0.66)
                8. Sine (half-sine window)

    >>> s = Server().boot()
    >>> s.start()
    >>> a = SfPlayer(SNDS_PATH+"/transparent.aif", loop=True, mul=0.7)
    >>> pva = PVAnal(a, size=1024, overlaps=4, wintype=2)
    >>> pvs = PVSynth(pva).mix(2).out()

    """
    def __init__(self, input, size=1024, overlaps=4, wintype=2):
        PyoPVObject.__init__(self)
        self._input = input
        self._size = size
        self._overlaps = overlaps
        self._wintype = wintype
        self._in_fader = InputFader(input)
        in_fader, size, overlaps, wintype, lmax = convertArgsToLists(self._in_fader, size, overlaps, wintype)
        self._base_objs = [PVAnal_base(wrap(in_fader,i), wrap(size,i), wrap(overlaps,i), wrap(wintype,i)) for i in range(lmax)]
 
    def setInput(self, x, fadetime=0.05):
        """
        Replace the `input` attribute.
        
        :Args:

            x : PyoObject
                New signal to process.
            fadetime : float, optional
                Crossfade time between old and new input. Default to 0.05.

        """
        self._input = x
        self._in_fader.setInput(x, fadetime)

    def setSize(self, x):
        """
        Replace the `size` attribute.
        
        :Args:

            x : int
                new `size` attribute.
        
        """
        self._size = x
        x, lmax = convertArgsToLists(x)
        [obj.setSize(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setOverlaps(self, x):
        """
        Replace the `overlaps` attribute.
        
        :Args:

            x : int
                new `overlaps` attribute.
        
        """
        self._overlaps = x
        x, lmax = convertArgsToLists(x)
        [obj.setOverlaps(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setWinType(self, x):
        """
        Replace the `wintype` attribute.
        
        :Args:

            x : int
                new `wintype` attribute.
        
        """
        self._wintype = x
        x, lmax = convertArgsToLists(x)
        [obj.setWinType(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    @property
    def input(self):
        """PyoObject. Input signal to process.""" 
        return self._input
    @input.setter
    def input(self, x): self.setInput(x)

    @property
    def size(self):
        """int. FFT size."""
        return self._size
    @size.setter
    def size(self, x): self.setSize(x)

    @property
    def overlaps(self):
        """int. FFT overlap factor."""
        return self._overlaps
    @overlaps.setter
    def overlaps(self, x): self.setOverlaps(x)

    @property
    def wintype(self):
        """int. Windowing method."""
        return self._wintype
    @wintype.setter
    def wintype(self, x): self.setWinType(x)

class PVSynth(PyoObject):
    """
    Phase Vocoder synthesis object.

    PVSynth takes a PyoPVObject as its input and performed
    the spectral to time domain conversion on it. This step
    converts phase vocoder magnitude and true frequency's
    streams back to a real signal.
    
    :Parent: :py:class:`PyoObject`
    
    :Args:
    
        input : PyoPVObject
            Phase vocoder streaming object to process.
        wintype : int, optional
            Shape of the envelope used to filter each input frame. 
            Possible shapes are:
                0. rectangular (no windowing)
                1. Hamming
                2. Hanning (default)
                3. Bartlett (triangular)
                4. Blackman 3-term
                5. Blackman-Harris 4-term
                6. Blackman-Harris 7-term
                7. Tuckey (alpha = 0.66)
                8. Sine (half-sine window)

    >>> s = Server().boot()
    >>> s.start()
    >>> a = SfPlayer(SNDS_PATH+"/transparent.aif", loop=True, mul=0.7)
    >>> pva = PVAnal(a, size=1024, overlaps=4, wintype=2)
    >>> pvs = PVSynth(pva).mix(2).out()

    """
    def __init__(self, input, wintype=2, mul=1, add=0):
        PyoObject.__init__(self, mul, add)
        self._input = input
        self._wintype = wintype
        input, wintype, mul, add, lmax = convertArgsToLists(self._input, wintype, mul, add)
        self._base_objs = [PVSynth_base(wrap(input,i), wrap(wintype,i), wrap(mul,i), wrap(add,i)) for i in range(lmax)]
 
    def setInput(self, x):
        """
        Replace the `input` attribute.
        
        :Args:

            x : PyoObject
                New signal to process.

        """
        self._input = x
        x, lmax = convertArgsToLists(x)
        [obj.setInput(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setWinType(self, x):
        """
        Replace the `wintype` attribute.
        
        :Args:

            x : int
                new `wintype` attribute.
        
        """
        self._wintype = x
        x, lmax = convertArgsToLists(x)
        [obj.setWinType(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def ctrl(self, map_list=None, title=None, wxnoserver=False):
        self._map_list = [SLMapMul(self._mul)]
        PyoObject.ctrl(self, map_list, title, wxnoserver)

    @property
    def input(self):
        """PyoPVObject. Input signal to process.""" 
        return self._input
    @input.setter
    def input(self, x): self.setInput(x)

    @property
    def wintype(self):
        """int. Windowing method."""
        return self._wintype
    @wintype.setter
    def wintype(self, x): self.setWinType(x)

class PVAddSynth(PyoObject):
    """
    Phase Vocoder additive synthesis object.

    PVAddSynth takes a PyoPVObject as its input and resynthesize
    the real signal using the magnitude and true frequency's 
    streams to control amplitude and frequency envelopes of an 
    oscillator bank.

    :Parent: :py:class:`PyoObject`
    
    :Args:
    
        input : PyoPVObject
            Phase vocoder streaming object to process.
        pitch : float or PyoObject, optional
            Transposition factor. Defaults to 1.
        num : int, optional
            Number of oscillators used to synthesize the
            output sound. Defaults to 100.
        first : int, optional
            The first bin to synthesize, starting from 0. 
            Defaults to 0.
        inc : int, optional
            Starting from bin `first`, resynthesize bins 
            `inc` apart. Defaults to 1. 
            

    >>> s = Server().boot()
    >>> s.start()
    >>> a = SfPlayer(SNDS_PATH+"/transparent.aif", loop=True, mul=0.7)
    >>> pva = PVAnal(a, size=1024, overlaps=4, pitch=2)
    >>> pvs = PVAddSynth(pva, pitch=1.25, num=100, first=0, inc=2)

    """
    def __init__(self, input, pitch=1, num=100, first=0, inc=1, mul=1, add=0):
        PyoObject.__init__(self, mul, add)
        self._input = input
        self._pitch = pitch
        self._num = num
        self._first = first
        self._inc = inc
        input, pitch, num, first, inc, mul, add, lmax = convertArgsToLists(self._input, pitch, num, first, inc, mul, add)
        self._base_objs = [PVAddSynth_base(wrap(input,i), wrap(pitch,i), wrap(num,i), wrap(first,i), wrap(inc,i), wrap(mul,i), wrap(add,i)) for i in range(lmax)]
 
    def setInput(self, x):
        """
        Replace the `input` attribute.
        
        :Args:

            x : PyoObject
                New signal to process.

        """
        self._input = x
        x, lmax = convertArgsToLists(x)
        [obj.setInput(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setPitch(self, x):
        """
        Replace the `pitch` attribute.
        
        :Args:

            x : float or PyoObject
                new `pitch` attribute.
        
        """
        self._pitch = x
        x, lmax = convertArgsToLists(x)
        [obj.setPitch(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setNum(self, x):
        """
        Replace the `num` attribute.
        
        :Args:

            x : int
                new `num` attribute.
        
        """
        self._num = x
        x, lmax = convertArgsToLists(x)
        [obj.setNum(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setFirst(self, x):
        """
        Replace the `first` attribute.
        
        :Args:

            x : int
                new `first` attribute.
        
        """
        self._first = x
        x, lmax = convertArgsToLists(x)
        [obj.setFirst(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setInc(self, x):
        """
        Replace the `inc` attribute.
        
        :Args:

            x : int
                new `inc` attribute.
        
        """
        self._inc = x
        x, lmax = convertArgsToLists(x)
        [obj.setInc(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def ctrl(self, map_list=None, title=None, wxnoserver=False):
        self._map_list = [SLMap(0.25, 4, "lin", "pitch", self._pitch),
                          SLMapMul(self._mul)]
        PyoObject.ctrl(self, map_list, title, wxnoserver)

    @property
    def input(self):
        """PyoPVObject. Input signal to process.""" 
        return self._input
    @input.setter
    def input(self, x): self.setInput(x)

    @property
    def pitch(self):
        """float or PyoObject. Transposition factor."""
        return self._pitch
    @pitch.setter
    def pitch(self, x): self.setPitch(x)

    @property
    def num(self):
        """int. Number of oscillators."""
        return self._num
    @num.setter
    def num(self, x): self.setNum(x)

    @property
    def first(self):
        """int. First bin to synthesize."""
        return self._first
    @first.setter
    def first(self, x): self.setFirst(x)

    @property
    def inc(self):
        """int. Synthesized bin increment."""
        return self._inc
    @inc.setter
    def inc(self, x): self.setInc(x)

class PVTranspose(PyoPVObject):
    """
    Transpose the frequency components of a pv stream.
    
    :Parent: :py:class:`PyoPVObject`
    
    :Args:
    
        input : PyoPVObject
            Phase vocoder streaming object to process.
        transpo : float or PyoObject, optional
            Transposition factor. Defaults to 1.

    >>> s = Server().boot()
    >>> s.start()
    >>> sf = SfPlayer(SNDS_PATH+"/transparent.aif", loop=True, mul=.7)
    >>> pva = PVAnal(sf, size=1024)
    >>> pvt = PVTranspose(pva, transpo=1.5)
    >>> pvs = PVSynth(pvt).out()
    >>> dry = Delay(sf, delay=1024./s.getSamplingRate(), mul=.7).out(1)

    """
    def __init__(self, input, transpo=1):
        PyoPVObject.__init__(self)
        self._input = input
        self._transpo = transpo
        input, transpo, lmax = convertArgsToLists(self._input, transpo)
        self._base_objs = [PVTranspose_base(wrap(input,i), wrap(transpo,i)) for i in range(lmax)]
 
    def setInput(self, x):
        """
        Replace the `input` attribute.
        
        :Args:

            x : PyoObject
                New signal to process.

        """
        self._input = x
        x, lmax = convertArgsToLists(x)
        [obj.setInput(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setTranspo(self, x):
        """
        Replace the `transpo` attribute.
        
        :Args:

            x : int
                new `transpo` attribute.
        
        """
        self._transpo = x
        x, lmax = convertArgsToLists(x)
        [obj.setTranspo(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def ctrl(self, map_list=None, title=None, wxnoserver=False):
        self._map_list = [SLMap(0.25, 4, "lin", "transpo", self._transpo)]
        PyoPVObject.ctrl(self, map_list, title, wxnoserver)

    @property
    def input(self):
        """PyoPVObject. Input signal to process.""" 
        return self._input
    @input.setter
    def input(self, x): self.setInput(x)

    @property
    def transpo(self):
        """float or PyoObject. Transposition factor."""
        return self._transpo
    @transpo.setter
    def transpo(self, x): self.setTranspo(x)

class PVVerb(PyoPVObject):
    """
    Spectral domain reverberation.

    :Parent: :py:class:`PyoPVObject`

    :Args:

        input : PyoPVObject
            Phase vocoder streaming object to process.
        revtime : float or PyoObject, optional
            Reverberation factor, between 0 and 1. 
            Defaults to 0.75.
        damp : float or PyoObject, optional
            High frequency damping factor, between 0 and 1. 
            1 means no damping and 0 is the most damping.
            Defaults to 0.75.

    >>> s = Server().boot()
    >>> s.start()
    >>> sf = SfPlayer(SNDS_PATH+"/transparent.aif", loop=True, mul=.5)
    >>> pva = PVAnal(sf, size=2048)
    >>> pvg = PVGate(pva, thresh=-36, damp=0)
    >>> pvv = PVVerb(pvg, revtime=0.95, damp=0.95)
    >>> pvs = PVSynth(pvv).mix(2).out()
    >>> dry = Delay(sf, delay=2048./s.getSamplingRate(), mul=.4).mix(2).out()

    """
    def __init__(self, input, revtime=0.75, damp=0.75):
        PyoPVObject.__init__(self)
        self._input = input
        self._revtime = revtime
        self._damp = damp
        input, revtime, damp, lmax = convertArgsToLists(self._input, revtime, damp)
        self._base_objs = [PVVerb_base(wrap(input,i), wrap(revtime,i), wrap(damp,i)) for i in range(lmax)]
 
    def setInput(self, x):
        """
        Replace the `input` attribute.
        
        :Args:

            x : PyoObject
                New signal to process.

        """
        self._input = x
        x, lmax = convertArgsToLists(x)
        [obj.setInput(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setRevtime(self, x):
        """
        Replace the `revtime` attribute.
        
        :Args:

            x : int
                new `revtime` attribute.
        
        """
        self._revtime = x
        x, lmax = convertArgsToLists(x)
        [obj.setRevtime(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setDamp(self, x):
        """
        Replace the `damp` attribute.
        
        :Args:

            x : int
                new `damp` attribute.
        
        """
        self._damp = x
        x, lmax = convertArgsToLists(x)
        [obj.setDamp(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def ctrl(self, map_list=None, title=None, wxnoserver=False):
        self._map_list = [SLMap(0, 1, "lin", "revtime", self._revtime),
                          SLMap(0, 1, "lin", "damp", self._damp)]
        PyoPVObject.ctrl(self, map_list, title, wxnoserver)

    @property
    def input(self):
        """PyoPVObject. Input signal to process.""" 
        return self._input
    @input.setter
    def input(self, x): self.setInput(x)

    @property
    def revtime(self):
        """float or PyoObject. Reverberation factor."""
        return self._revtime
    @revtime.setter
    def revtime(self, x): self.setRevtime(x)
    
    @property
    def damp(self):
        """float or PyoObject. High frequency damping factor."""
        return self._damp
    @damp.setter
    def damp(self, x): self.setDamp(x)

class PVGate(PyoPVObject):
    """
    Spectral gate.

    :Parent: :py:class:`PyoObject`

    :Args:

        input : PyoPVObject
            Phase vocoder streaming object to process.
        thresh : float or PyoObject, optional
            Threshold factor in dB. Bins below that threshold
            will be scaled by `damp` factor. Defaults to -20.
        damp : float or PyoObject, optional
            Damping factor for low amplitude bins. Defaults to 0.

    >>> s = Server().boot()
    >>> s.start()
    >>> sf = SfPlayer(SNDS_PATH+"/transparent.aif", loop=True, mul=.5)
    >>> pva = PVAnal(sf, size=2048)
    >>> pvg = PVGate(pva, thresh=-50, damp=0)
    >>> pvs = PVSynth(pvg).mix(2).out()

    """
    def __init__(self, input, thresh=-20, damp=0.):
        PyoPVObject.__init__(self)
        self._input = input
        self._thresh = thresh
        self._damp = damp
        input, thresh, damp, lmax = convertArgsToLists(self._input, thresh, damp)
        self._base_objs = [PVGate_base(wrap(input,i), wrap(thresh,i), wrap(damp,i)) for i in range(lmax)]
 
    def setInput(self, x):
        """
        Replace the `input` attribute.
        
        :Args:

            x : PyoObject
                New signal to process.

        """
        self._input = x
        x, lmax = convertArgsToLists(x)
        [obj.setInput(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setThresh(self, x):
        """
        Replace the `thresh` attribute.
        
        :Args:

            x : int
                new `thresh` attribute.
        
        """
        self._thresh = x
        x, lmax = convertArgsToLists(x)
        [obj.setThresh(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def setDamp(self, x):
        """
        Replace the `damp` attribute.
        
        :Args:

            x : int
                new `damp` attribute.
        
        """
        self._damp = x
        x, lmax = convertArgsToLists(x)
        [obj.setDamp(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def ctrl(self, map_list=None, title=None, wxnoserver=False):
        self._map_list = [SLMap(-120, 18, "lin", "thresh", self._thresh),
                          SLMap(0, 2, "lin", "damp", self._damp)]
        PyoPVObject.ctrl(self, map_list, title, wxnoserver)

    @property
    def input(self):
        """PyoPVObject. Input signal to process.""" 
        return self._input
    @input.setter
    def input(self, x): self.setInput(x)

    @property
    def thresh(self):
        """float or PyoObject. Threshold factor."""
        return self._thresh
    @thresh.setter
    def thresh(self, x): self.setThresh(x)
    
    @property
    def damp(self):
        """float or PyoObject. Damping factor for low amplitude bins."""
        return self._damp
    @damp.setter
    def damp(self, x): self.setDamp(x)