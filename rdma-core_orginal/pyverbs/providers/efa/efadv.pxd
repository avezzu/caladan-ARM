# SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB)
# Copyright 2020 Amazon.com, Inc. or its affiliates. All rights reserved.

#cython: language_level=3

cimport pyverbs.providers.efa.libefa as dv

from pyverbs.addr cimport AH
from pyverbs.base cimport PyverbsObject
from pyverbs.device cimport Context
from pyverbs.qp cimport QP


cdef class EfaContext(Context):
    pass


cdef class EfaDVDeviceAttr(PyverbsObject):
    cdef dv.efadv_device_attr dv


cdef class EfaAH(AH):
    pass


cdef class EfaDVAHAttr(PyverbsObject):
    cdef dv.efadv_ah_attr ah_attr


cdef class SRDQP(QP):
    pass
