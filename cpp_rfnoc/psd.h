#ifndef PSD_I_IMPL_H
#define PSD_I_IMPL_H

// Base Include(s)
#include "psd_base.h"

// RF-NoC RH Include(s)
#include <GenericThreadedComponent.h>
#include <RFNoC_Component.h>

// UHD Include(s)
#include <uhd/device3.hpp>
#include <uhd/rfnoc/block_ctrl.hpp>
#include <uhd/rfnoc/graph.hpp>

/*
 * The class for the component
 */
class psd_i : public psd_base, public RFNoC_RH::RFNoC_Component
{
    ENABLE_LOGGING

	// Constructor(s) and/or Destructor
    public:
        psd_i(const char *uuid, const char *label);
        ~psd_i();

    // Public Method(s)
    public:
        void releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException);

        void start() throw (CF::Resource::StartError, CORBA::SystemException);

        void stop() throw (CF::Resource::StopError, CORBA::SystemException);

    // Public RFNoC_Component Method(s)
    public:
		// Methods to be called by the persona, inherited from RFNoC_Component
		void setRxStreamer(uhd::rx_streamer::sptr rxStreamer);

		void setTxStreamer(uhd::tx_streamer::sptr txStreamer);

	// Protected Method(s)
    protected:
        void constructor();

        // Don't use the default serviceFunction for clarity
        int serviceFunction() { return FINISH; }

        // Service functions for RX and TX
        int rxServiceFunction();

        int txServiceFunction();

    // Private Method(s)
    private:
        void fftSizeChanged(const CORBA::ULong &oldValue, const CORBA::ULong &newValue);

        void newConnection(const char *connectionID);

        void newDisconnection(const char *connectionID);

        void setFftReset();

        bool setFftSize(const CORBA::ULong &newFftSize);

        bool setMagnitudeOut(const std::string &newMagnitudeOut);

        void sriChanged(const BULKIO::StreamSRI &newSRI);

        void startRxStream();

        void stopRxStream();

        void streamChanged(bulkio::InShortPort::StreamType stream);

    private:
        bool eob;
        bool expectEob;
        uhd::rfnoc::block_ctrl_base::sptr fft;
        size_t fftPort;
        size_t fftSpp;
        std::vector<std::complex<short> > output;
        bool receivedSRI;
        uhd::rx_streamer::sptr rxStreamer;
        bool rxStreamStarted;
        boost::shared_ptr<RFNoC_RH::GenericThreadedComponent> rxThread;
        BULKIO::StreamSRI sri;
        std::map<std::string, bool> streamMap;
        uhd::tx_streamer::sptr txStreamer;
        boost::shared_ptr<RFNoC_RH::GenericThreadedComponent> txThread;
};

#endif
