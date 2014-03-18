// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2014 iCub Facility
 * Authors: Paul Fitzpatrick
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#ifndef _YARP2_NETWORKCLOCK_
#define _YARP2_NETWORKCLOCK_

#include <yarp/os/Clock.h>
#include <yarp/os/Port.h>
#include <yarp/os/NetInt32.h>
#include <yarp/os/Semaphore.h>
#include <yarp/conf/numeric.h>

#include <ace/Synch.h>

namespace yarp {
    namespace os {
        class NetworkClock;
    }
};


class YARP_OS_API yarp::os::NetworkClock : public Clock, PortReader {
public:
    NetworkClock();
    
    bool open(const ConstString& name);

    virtual double now();
    virtual void delay(double seconds);
    virtual bool isValid() const;

    virtual bool read(ConnectionReader& reader);
private:
    Port port;

    /** look at
     *  http://huihoo.org/ace_tao/ACE-5.2+TAO-1.2/ACE_wrappers/docs/tutorials/016/page02.html
     *  for a wrapper implementation of ACE_Condition
     */
    ACE_Condition_Thread_Mutex tick;
    ACE_Thread_Mutex tick_mutex;

    Semaphore mutex;
    
    YARP_INT32 sec;
    YARP_INT32 nsec;
    double t;
};

#endif



