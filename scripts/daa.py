#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys

# NOTA: No se a quin valor tinc que ficar el flag H, el que estic fent
#       és ficar-lo al valor que resulta de fer la suma per a
#       corregir-lo.

# (INPUT 16) H,N,C,A
# (OUTPUT 16) MASKF(S Z PV H? C) A

def parity(val):
    aux= 0
    for i in xrange(0,8):
        if val&0x1 == 0x1 : aux+= 1
        val>>= 1
    if aux%2== 0 : return 0x0400
    return 0x000
    
def set_flags(val):
    if (val&0xff) == 0 : val|= 0x4000
    if (val&0x80) : val|= 0x8000
    val|= parity ( val&0xff )
    return val

def calc_ret(old,add):
    ret= (old+add)&0xff
    if ((old^add)^ret)&0x10 : ret|= 0x1000
    return ret

# H=0,N=0,C=0
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h <= 9 and l <= 9 :
        ret= i
    elif h <= 8 and l >= 0xa :
        ret= calc_ret ( i, 6 )
    elif h >= 0xa and l <= 9 :
        ret= calc_ret ( i, 0x60 )
        ret|= 0x0100
    elif h >= 9 and l >= 0xa :
        ret= calc_ret ( i, 0x66 )
        ret|= 0x0100
    else: sys.exit ( 'Falta cas en H=0 N=0 C=0' )
    print '0x%04x,'%set_flags ( ret )

# H=0,N=0,C=1
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h <= 2 and l <= 9 :
        ret= calc_ret ( i, 0x60 )
        ret|= 0x0100
    elif h <= 2 and l >= 0xa :
        ret= calc_ret ( i, 0x66 )
        ret|= 0x0100
    else: # Dona igual
        ret= 0x00
    print '0x%04x,'%set_flags ( ret )

# H=0,N=1,C=0
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h <= 9 and l <= 9 :
        ret= i
    else: # Dona igual
        ret= 0x00
    print '0x%04x,'%set_flags ( ret )
    
# H=0,N=1,C=1
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h >= 7 and l <= 9 :
        ret= calc_ret ( i, 0xa0 )
        ret|= 0x0100
    else: # Dona igual
        ret= 0x00
    print '0x%04x,'%set_flags ( ret )

# H=1,N=0,C=0
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h <= 9 and l <= 3 :
        ret= calc_ret ( i, 0x06 )
    elif h >= 0xa and l <= 3 :
        ret= calc_ret ( i, 0x66 )
        ret|= 0x0100
    else: # Dona igual
        ret= 0x00
    print '0x%04x,'%set_flags ( ret )

# H=1,N=0,C=1
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h <= 3 and l <= 3 :
        ret= calc_ret ( i, 0x66 )
        ret|= 0x0100
    else: # Dona igual
        ret= 0x00
    print '0x%04x,'%set_flags ( ret )

# H=1,N=1,C=0
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h <= 8 and l >= 6 :
        ret= calc_ret ( i, 0xfa )
    else: # Dona igual
        ret= 0x00
    print '0x%04x,'%set_flags ( ret )

# H=1,N=1,C=1
for i in xrange(0,256):
    h= i>>4; l= i&0xf
    if h >= 6 and l >= 6 :
#    if (h == 6 or h == 7) and l >= 6 : ERROR EN EL MANUAL!!!!!
        ret= calc_ret ( i, 0x9a )
        ret|= 0x0100
    else: # Dona igual
        ret= 0x00
    print '0x%04x,'%set_flags ( ret )
    
