// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2012 Robotics Brain and Cognitive Sciences Department, Istituto Italiano di Tecnologia
 * Authors: Lorenzo Natale
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#ifndef __YARPIENCODERS_TIMED__
#define __YARPIENCODERS_TIMED__

#include <yarp/dev/IEncoders.h>
#include <yarp/dev/api.h>

namespace yarp {
    namespace dev {
        class IEncodersTimed;
        class IEncodersTimedRaw;
    }
}

    /**
 * @ingroup dev_iface_motor
 *
 * Control board, extend encoder raw interface adding timestamps.
 */
class yarp::dev::IEncodersTimedRaw: public IEncodersRaw
{
public:
    /**
     * Destructor.
     */
    virtual ~IEncodersTimedRaw() {}

     /**
     * Read the instantaneous acceleration of all axes.
     * @encs pointer to the array that will contain the output
     * @stamps pointer to the array that will contain individual timestamps
     * @return true if all goes well, false if anything bad happens. 
     */
    virtual bool getEncodersTimedRaw(double *encs, double *stamps);
    
     /**
     * Read the instantaneous acceleration of all axes.
     * @j axis index
     * @enc encoder value
     * @stamp corresponding timestamp
     * @return true if all goes well, false if anything bad happens. 
     */
    virtual bool getEncoderTimedRaw(int j, double *encs, double *stamp);
};

/**
 * @ingroup dev_iface_motor
 *
 * Control board, extend encoder interface with timestamps.
 */
class YARP_dev_API yarp::dev::IEncodersTimed: public IEncoders
{
public:
     /**
     * Destructor.
     */
    virtual ~IEncodersTimed() {}

     /**
     * Read the instantaneous acceleration of all axes.
     * @encs pointer to the array that will contain the output
     * @stamps pointer to the array that will contain individual timestamps
     * @return true if all goes well, false if anything bad happens. 
     */
    virtual bool getEncodersTimed(double *encs, double *time)=0;

    /**
    * Read the instantaneous acceleration of all axes.
    * @j axis index
    * @enc encoder value (pointer to)
    * @stamp corresponding timestamp (pointer to)
    * @return true if all goes well, false if anything bad happens. 
    */
    virtual bool getEncoderTimed(int j, double *encs, double *time)=0;
};


#endif

