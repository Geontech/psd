#include <iostream>
#include "ossie/ossieSupport.h"
#include <ossie/Device_impl.h>

#include "psd.h"

#include <uhd/device3.hpp>

psd_i *resourcePtr;

void signal_catcher(int sig)
{
    // IMPORTANT Don't call exit(...) in this function
    // issue all CORBA calls that you need for cleanup here before calling ORB shutdown
    if (resourcePtr) {
        resourcePtr->halt();
    }
}
int main(int argc, char* argv[])
{
    struct sigaction sa;
    sa.sa_handler = signal_catcher;
    sa.sa_flags = 0;
    resourcePtr = 0;

    //Component::start_component(&resourcePtr, argc, argv);
    Resource_impl::start_component(resourcePtr, argc, argv);
    return 0;
}

extern "C" {
    Resource_impl* construct(int argc, char* argv[], Device_impl* parentDevice, uhd::device3::sptr usrp, blockInfoCallback cb, setSetStreamerCallback setSetRxStreamerCb, setSetStreamerCallback setSetTxStreamerCb) {

        struct sigaction sa;
        sa.sa_handler = signal_catcher;
        sa.sa_flags = 0;
        resourcePtr = 0;

        Resource_impl::start_component(resourcePtr, argc, argv);

        // Any addition parameters passed into construct can now be
        // set directly onto resourcePtr since it is the instantiated
        // Redhawk device
        //      Example:
        //         resourcePtr->setSharedAPI(sharedAPI);
        //resourcePtr->setParentDevice(parentDevice);
        resourcePtr->setBlockInfoCallback(cb);
        resourcePtr->setUsrp(usrp);
        setSetRxStreamerCb(resourcePtr->_identifier, boost::bind(&psd_i::setRxStreamer, resourcePtr, _1));
        setSetTxStreamerCb(resourcePtr->_identifier, boost::bind(&psd_i::setTxStreamer, resourcePtr, _1));

        return resourcePtr;
    }
}
