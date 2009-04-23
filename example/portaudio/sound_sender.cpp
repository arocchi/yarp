#include <stdio.h>
#include <yarp/dev/PolyDriver.h>
#include <yarp/dev/AudioGrabberInterfaces.h>
#include <yarp/os/Property.h>

#include <yarp/os/Network.h>
#include <yarp/os/Port.h>

using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::dev;

int main(int argc, char *argv[]) {

    // Open the network
    Network yarp;
    Port p;
    p.open("/sender");

    // Get a portaudio read device.
    Property conf;
    conf.put("device","portaudio");
    conf.put("read", "");
    PolyDriver poly(conf);
    IAudioGrabberSound *get;

    // Make sure we can read sound
    poly.view(get);
    if (get==NULL) {
        printf("cannot open interface\n");
        return 1;
    }

    //Grab and send
    Sound s;
    while (true)
      {
	get->getSound(s);
	p.write(s);
      }
    return 0;
}

