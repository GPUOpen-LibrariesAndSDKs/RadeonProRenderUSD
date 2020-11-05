from . import _rprUsd
from pxr import Tf
Tf.PrepareModule(_rprUsd, locals())
del Tf

try:
    from . import __DOC
    __DOC.Execute(locals())
    del __DOC
except Exception:
    pass
