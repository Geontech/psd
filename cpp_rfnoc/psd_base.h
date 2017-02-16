#ifndef PSD_BASE_IMPL_BASE_H
#define PSD_BASE_IMPL_BASE_H

#include <boost/thread.hpp>
#include <ossie/Component.h>
#include <ossie/ThreadedComponent.h>

#include <bulkio/bulkio.h>

class psd_base : public Component, protected ThreadedComponent
{
    public:
        psd_base(const char *uuid, const char *label);
        ~psd_base();

        void start() throw (CF::Resource::StartError, CORBA::SystemException);

        void stop() throw (CF::Resource::StopError, CORBA::SystemException);

        void releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException);

        void loadProperties();

    protected:
        // Member variables exposed as properties
        /// Property: fftSize
        CORBA::ULong fftSize;
        /// Property: overlap
        CORBA::Long overlap;
        /// Property: numAvg
        CORBA::ULong numAvg;
        /// Property: logCoefficient
        float logCoefficient;
        /// Property: rfFreqUnits
        bool rfFreqUnits;

        // Ports
        /// Port: dataFloat_in
        bulkio::InFloatPort *dataFloat_in;
        /// Port: dataShort_in
        bulkio::InShortPort *dataShort_in;
        /// Port: psd_dataFloat_out
        bulkio::OutFloatPort *psd_dataFloat_out;
        /// Port: fft_dataFloat_out
        bulkio::OutFloatPort *fft_dataFloat_out;
        /// Port: psd_dataShort_out
        bulkio::OutShortPort *psd_dataShort_out;

    private:
};
#endif // PSD_BASE_IMPL_BASE_H
