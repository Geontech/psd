#include "psd_base.h"

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY

    The following class functions are for the base class for the component class. To
    customize any of these functions, do not modify them here. Instead, overload them
    on the child class

******************************************************************************************/

psd_base::psd_base(const char *uuid, const char *label) :
    Component(uuid, label),
    ThreadedComponent()
{
    loadProperties();

    dataFloat_in = new bulkio::InFloatPort("dataFloat_in");
    addPort("dataFloat_in", "Float input port for real or complex time domain data. ", dataFloat_in);
    dataShort_in = new bulkio::InShortPort("dataShort_in");
    addPort("dataShort_in", dataShort_in);
    psd_dataFloat_out = new bulkio::OutFloatPort("psd_dataFloat_out");
    addPort("psd_dataFloat_out", "Float output port for power spectral density. The output will be two dimentional data with a subsize of half the FFT size plus one for real input data and equal to the FFT size for complex input data. The PSD output data is always scalar.  ", psd_dataFloat_out);
    fft_dataFloat_out = new bulkio::OutFloatPort("fft_dataFloat_out");
    addPort("fft_dataFloat_out", "Float output port for the FFT of the input data. The output will be two dimentional data with a subsize of half the FFT size plus one for real input data and equal to the FFT size for complex input data. The FFT output data is always complex.  ", fft_dataFloat_out);
    psd_dataShort_out = new bulkio::OutShortPort("psd_dataShort_out");
    addPort("psd_dataShort_out", psd_dataShort_out);
}

psd_base::~psd_base()
{
    delete dataFloat_in;
    dataFloat_in = 0;
    delete dataShort_in;
    dataShort_in = 0;
    delete psd_dataFloat_out;
    psd_dataFloat_out = 0;
    delete fft_dataFloat_out;
    fft_dataFloat_out = 0;
    delete psd_dataShort_out;
    psd_dataShort_out = 0;
}

/*******************************************************************************************
    Framework-level functions
    These functions are generally called by the framework to perform housekeeping.
*******************************************************************************************/
void psd_base::start() throw (CORBA::SystemException, CF::Resource::StartError)
{
    Component::start();
    ThreadedComponent::startThread();
}

void psd_base::stop() throw (CORBA::SystemException, CF::Resource::StopError)
{
    Component::stop();
    if (!ThreadedComponent::stopThread()) {
        throw CF::Resource::StopError(CF::CF_NOTSET, "Processing thread did not die");
    }
}

void psd_base::releaseObject() throw (CORBA::SystemException, CF::LifeCycle::ReleaseError)
{
    // This function clears the component running condition so main shuts down everything
    try {
        stop();
    } catch (CF::Resource::StopError& ex) {
        // TODO - this should probably be logged instead of ignored
    }

    Component::releaseObject();
}

void psd_base::loadProperties()
{
    addProperty(fftSize,
                32768,
                "fftSize",
                "",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(overlap,
                0,
                "overlap",
                "",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(numAvg,
                0,
                "numAvg",
                "",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(logCoefficient,
                0.0,
                "logCoefficient",
                "",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(rfFreqUnits,
                false,
                "rfFreqUnits",
                "",
                "readwrite",
                "",
                "external",
                "property");

}


