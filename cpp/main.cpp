#include <iostream>
#include "ossie/ossieSupport.h"

#include "psd.h"
int main(int argc, char* argv[])
{
    psd_i* psd_servant;
    Component::start_component(psd_servant, argc, argv);
    return 0;
}

