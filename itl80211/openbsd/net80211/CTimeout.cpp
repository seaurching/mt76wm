//
//  MtkCTimeout.cpp
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

#include <sys/MtkCTimeout.hpp>

void MtkCTimeout::timeoutOccurred(OSObject* owner, IOTimerEventSource* timer)
{
//    IOLog("itlwm %s\n", __FUNCTION__);
    if (owner == NULL) {
        IOLog("itlwm tm owner == NULL!!!\n");
    }
    MtkCTimeout *tm = OSDynamicCast(MtkCTimeout, owner);
    if (tm == NULL) {
        IOLog("itlwm tm == NULL!!!\n");
        return;
    }
    //callback
    tm->isPending = false;
    tm->to_func(tm->to_arg);
}

IOReturn MtkCTimeout::timeout_add_msec(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    IOWorkLoop *wl = (IOWorkLoop*)arg1;
    int msecs = *(int*)arg2;
    MtkCTimeout **ccto = (MtkCTimeout **)arg0;
    
    if (ccto == NULL || *ccto == NULL) {
        return kIOReturnError;
    }
    MtkCTimeout *cto = *ccto;
    if (cto->tm == NULL) {
        cto->tm = IOTimerEventSource::timerEventSource(cto, &MtkCTimeout::timeoutOccurred);
        if (cto->tm == NULL) {
            return kIOReturnError;
        }
        cto->tm->enable();
        wl->addEventSource(cto->tm);
    }
    cto->tm->setTimeoutMS(msecs);
    cto->isPending = true;
    return kIOReturnSuccess;
}

IOReturn MtkCTimeout::timeout_del(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    MtkCTimeout **ccto = (MtkCTimeout **)arg0;
    if (ccto == NULL || *ccto == NULL) {
        return kIOReturnSuccess;
    }
    MtkCTimeout *cto = *ccto;
    if (cto->tm == NULL || !cto->isPending) {
        return kIOReturnSuccess;
    }
    cto->tm->cancelTimeout();
    cto->isPending = false;
    return kIOReturnSuccess;
}

IOReturn MtkCTimeout::timeout_free(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    MtkCTimeout **cto = (MtkCTimeout **)arg0;
    IOWorkLoop *wl = (IOWorkLoop*)arg1;
    if (cto == NULL || *cto == NULL) {
        return kIOReturnSuccess;
    }
    MtkCTimeout *tm = *cto;
    if (tm->tm != NULL) {
        wl->removeEventSource(tm->tm);
        tm->tm->release();
        tm->tm = NULL;
    }
    tm->release();
    *cto = NULL;
    return kIOReturnSuccess;
}

typedef void (*callback)(void *);

IOReturn MtkCTimeout::timeout_set(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    MtkCTimeout *tm;
    MtkCTimeout **cto = (MtkCTimeout **)arg0;
    if (cto == NULL) {
        return kIOReturnError;
    }
    if ((*cto) == NULL) {
        *cto = new MtkCTimeout;
    }
    tm = *cto;
    tm->isPending = false;
    tm->to_func = (callback)arg1;
    tm->to_arg = arg2;
    return kIOReturnSuccess;
}

IOReturn MtkCTimeout::timeout_pending(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    MtkCTimeout **cto = (MtkCTimeout **)arg0;
    MtkCTimeout *tm;
    if (cto == NULL) {
        return kIOReturnError;
    }
    tm = *cto;
    if (tm != NULL && tm->isPending) {
        return kIOReturnSuccess;
    }
    return kIOReturnError;
}
