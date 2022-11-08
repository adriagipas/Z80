#!/usr/bin/env python
# -*- coding: utf-8 -*-

for i in xrange(0,256):
    aux= 0
    for j in xrange(0,8):
        if i&0x1 == 0x1 : aux+= 1
        i>>= 1
    if aux%2== 0 : print "PVFLAG,"
    else : print "0x00,"
