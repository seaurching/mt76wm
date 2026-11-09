//
//  timeout.cpp
//  AppleIntelWifiAdapter
//
//  Created by 钟先耀 on 2020/1/30.
//  Copyright © 2020 钟先耀. All rights reserved.
//

/*
 * Copyright (C) 2020  钟先耀
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef timeout_cpp
#define timeout_cpp

#include <sys/timeout.h>
#include <IOKit/IOCommandGate.h>

extern IOWorkLoop *_fWorkloop;
extern IOCommandGate *_fCommandGate;

int splnet()
{
    return 1;
}

void splx(int s)
{
}

void timeout_set(MtkCTimeout **t, void (*fn)(void *), void *arg)
{
    _fCommandGate->runAction(&MtkCTimeout::timeout_set, t, (void*)fn, arg);
}

int timeout_add_msec(MtkCTimeout **to, int msecs)
{
    return _fCommandGate->runAction(&MtkCTimeout::timeout_add_msec, to, _fWorkloop, &msecs) == kIOReturnSuccess ? 1 : 0;
}

int timeout_add_sec(MtkCTimeout **to, int secs)
{
    return timeout_add_msec(to, secs * 1000);
}

int timeout_add_usec(MtkCTimeout **to, int usecs)
{
    return timeout_add_msec(to, (int) usecs / 1000);
}

int timeout_del(MtkCTimeout **to)
{
    //    IOLog("timeout_del\n");
    return _fCommandGate->runAction(&MtkCTimeout::timeout_del, to, _fWorkloop) == kIOReturnSuccess ? 1 : 0;
}

int timeout_free(MtkCTimeout **to)
{
    return _fCommandGate->runAction(&MtkCTimeout::timeout_free, to, _fWorkloop) == kIOReturnSuccess ? 1 : 0;
}

int timeout_pending(MtkCTimeout **to)
{
    return _fCommandGate->runAction(&MtkCTimeout::timeout_pending, to) == kIOReturnSuccess ? 1 : 0;
}

int timeout_initialized(MtkCTimeout **to)
{
    return (*to) != NULL;
}

#endif /* timeout_cpp */
