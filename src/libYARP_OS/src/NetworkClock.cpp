// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2014 iCub Facility
 * Authors: Paul Fitzpatrick
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */


#include <yarp/os/NetworkClock.h>
#include <yarp/os/SystemClock.h>
#include <yarp/os/NestedContact.h>
#include <yarp/os/Network.h>

#include <stdio.h>

using namespace yarp::os;

NetworkClock::NetworkClock() : tick(tick_mutex) {
    sec = 0;
    nsec = 0;
    t = 0;
}

bool NetworkClock::open(const ConstString& name) {
    port.setReadOnly();
    port.setReader(*this);
    NestedContact nc(name);
    if (nc.getNestedName()=="") {
        Contact src = NetworkBase::queryName(name);
        if (src.isValid()) {
            bool ok = port.open("");
            if (!ok) return false;
            return NetworkBase::connect(name,port.getName());
        } else {
            fprintf(stderr,"Cannot find time port \"%s\"; for a time topic specify \"%s@\"\n", name.c_str(), name.c_str());
            return false;
        }
    }
    return port.open(name);
}

double NetworkClock::now() {
    mutex.wait();
    double result = t;
    mutex.post();
    return t;
}

void NetworkClock::delay(double seconds) {
    if (seconds<=1E-12) {
        return;
    }

    tick_mutex.acquire();
    double start = now();
    do {
        tick.wait();
    } while ( seconds - (now()-start) > 1E-12);
    tick_mutex.release();
}

bool NetworkClock::isValid() const {
    return (sec!=0) || (nsec!=0);
}

bool NetworkClock::read(ConnectionReader& reader) {
    Bottle bot;
    bool ok = bot.read(reader);
    if(!ok) return false;

    mutex.wait();
    sec = bot.get(0).asInt();
    nsec = bot.get(1).asInt();
    t = sec + (nsec*1e-9);
    mutex.post();
    tick.broadcast();
    return true;
}
