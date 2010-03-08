from _core import *

class Pattern(PyoObject):
    """
    Periodically calls a Python function.
        
    Parent class: PyoObject
    
    Parameters:

    function : Python function
    time : float or PyoObject, optional
        Time, in seconds, between each call. Default to 1.
        
    Methods:

    setTime(x) : Replace the `time` attribute.

    Attributes:
    
    time : Time, in seconds, between each call.
    
    Notes:

    The out() method is bypassed. Pattern doesn't return signal.
    
    Pattern has no `mul` and `add` attributes.

    Examples:
    
    >>> s = Server().boot()
    >>> s.start()
    >>> t = HarmTable([1,0,.33,0,.2,0,.143,0,.111])
    >>> a = Osc(t, 250, 0, .5).out()
    >>> def pat():
    ...     a.freq = random.randint(200, 400)
    ...    
    >>> p = Pattern(pat, .125)
    >>> p.play()
    
    """
    def __init__(self, function, time=1):
        self._function = function
        self._time = time
        function, time, lmax = convertArgsToLists(function, time)
        self._base_objs = [Pattern_base(wrap(function,i), wrap(time,i)) for i in range(lmax)]

    def __dir__(self):
        return ['time']

    def setTime(self, x):
        """
        Replace the `time` attribute.
        
        Parameters:
        
        x : float or PyoObject
            New `time` attribute.
        
        """
        self._time = x
        x, lmax = convertArgsToLists(x)
        [obj.setTime(wrap(x,i)) for i, obj in enumerate(self._base_objs)]

    def out(self, x=0, inc=1):
        return self
        
    def setMul(self, x):
        pass

    def setAdd(self, x):
        pass

    def setSub(self, x):
        pass

    def setDiv(self, x):
        pass

    def ctrl(self, map_list=None, title=None):
        self._map_list = [SLMap(0.125, 4., 'lin', 'time', self._time)]
        PyoObject.ctrl(self, map_list, title)
         
    @property
    def time(self):
        """float or PyoObject. Time, in seconds, between each call.""" 
        return self._time
    @time.setter
    def time(self, x): self.setTime(x)
