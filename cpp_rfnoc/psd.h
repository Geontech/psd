#ifndef PSD_I_IMPL_H
#define PSD_I_IMPL_H

#include "psd_base.h"

#include <uhd/device3.hpp>
#include <uhd/rfnoc/block_ctrl.hpp>
#include <uhd/rfnoc/graph.hpp>

#include "GenericThreadedComponent.h"
#include "RFNoC_Component.h"

class psd_i : public psd_base, public RFNoC_ComponentInterface
{
    ENABLE_LOGGING
    public:
        psd_i(const char *uuid, const char *label);
        ~psd_i();

        void constructor();

        // Service functions for RX and TX
        int rxServiceFunction();
        int txServiceFunction();

        // Don't use the default serviceFunction for clarity
        int serviceFunction() { return FINISH; }

        // Override start and stop
        void start() throw (CF::Resource::StartError, CORBA::SystemException);
        void stop() throw (CF::Resource::StopError, CORBA::SystemException);

        // Override releaseObject
        void releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException);

        // Methods to be called by the persona, inherited from RFNoC_ComponentInterface
        void setBlockInfoCallback(blockInfoCallback cb);
        void setRxStreamer(bool enable);
        void setTxStreamer(bool enable);
        void setUsrp(uhd::device3::sptr usrp);

    private:
        // Property listeners
        void fftSizeChanged(const CORBA::ULong &oldValue, const CORBA::ULong &newValue);

        // Stream listeners
        void streamChanged(bulkio::InShortPort::StreamType stream);

    private:
        void retrieveRxStream();
        void retrieveTxStream();
        bool setFftSize(const CORBA::ULong &newFftSize);
        bool setMagnitudeOut(const std::string &newMagnitudeOut);
        void sriChanged(const BULKIO::StreamSRI &newSRI);
        void startRxStream();
        void stopRxStream();

    private:
        // RF-NoC Members
        uhd::rfnoc::block_ctrl_base::sptr fft;
        size_t fftPort;
        size_t fftSpp;
        uhd::rfnoc::graph::sptr graph;
        uhd::rfnoc::block_ctrl_base::sptr oneInN;
        size_t oneInNPort;
        size_t oneInNSpp;
        uhd::device3::sptr usrp;

        // UHD Members
        uhd::rx_streamer::sptr rxStream;
        uhd::tx_streamer::sptr txStream;

        // Miscellaneous
        blockInfoCallback blockInfoChange;
        bool eob;
        bool expectEob;
        std::vector<std::complex<short> > output;
        bool receivedSRI;
        bool rxStreamStarted;
        GenericThreadedComponent *rxThread;
        BULKIO::StreamSRI sri;
        GenericThreadedComponent *txThread;
};

#endif // PSD_I_IMPL_H
