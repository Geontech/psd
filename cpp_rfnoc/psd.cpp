/**************************************************************************

    This is the component code. This file contains the child class where
    custom functionality can be added to the component. Custom
    functionality to the base class can be extended here. Access to
    the ports can also be done from this class

**************************************************************************/

#include "RFNoC_Utils.h"
#include "psd.h"

PREPARE_LOGGING(psd_i)

psd_i::psd_i(const char *uuid, const char *label) :
    psd_base(uuid, label),
    eob(false),
    expectEob(false),
    fftPort(-1),
    fftSpp(512),
    receivedSRI(false),
    rxStreamStarted(false),
    rxThread(NULL),
    txThread(NULL)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);
}

psd_i::~psd_i()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // Stop streaming
    stopRxStream();

    // Reset the RF-NoC blocks
    if (this->fft.get()) {
        this->fft->clear(this->fftPort);
    }

    // Release the threads if necessary
    if (this->rxThread) {
        this->rxThread->stop();
        delete this->rxThread;
    }

    if (this->txThread) {
        this->txThread->stop();
        delete this->txThread;
    }

    LOG_INFO(psd_i, "END OF DESTRUCTOR");
}

void psd_i::constructor()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // Find available sinks and sources
    BlockInfo fftInfo = findAvailableChannel(this->usrp, "FFT");
    BlockInfo oneInNInfo = findAvailableChannel(this->usrp, "KEEP_ONE_IN_N");

    // Without these, there's no need to continue
    if (not uhd::rfnoc::block_id_t::is_valid_block_id(fftInfo.blockID)) {
        LOG_FATAL(psd_i, "Unable to find RF-NoC block with hint 'FFT'");
        throw CF::LifeCycle::InitializeError();
    } else if (fftInfo.port == size_t(-1)) {
        LOG_FATAL(psd_i, "Unable to find RF-NoC block with hint 'FFT' with available port");
        throw CF::LifeCycle::InitializeError();
    }

    if (not uhd::rfnoc::block_id_t::is_valid_block_id(oneInNInfo.blockID)) {
        LOG_FATAL(psd_i, "Unable to find RF-NoC block with hint 'KEEP_ONE_IN_N'");
        throw CF::LifeCycle::InitializeError();
    } else if (oneInNInfo.port == size_t(-1)) {
        LOG_FATAL(psd_i, "Unable to find RF-NoC block with hint 'KEEP_ONE_IN_N' with available port");
        throw CF::LifeCycle::InitializeError();
    }

    // Get the pointers to the blocks
    this->fft = this->usrp->get_block_ctrl<uhd::rfnoc::block_ctrl_base>(fftInfo.blockID);
    this->fftPort = fftInfo.port;

    if (not this->fft.get()) {
        LOG_FATAL(psd_i, "Unable to retrieve RF-NoC block with ID: " << this->fft->get_block_id());
        throw CF::LifeCycle::InitializeError();
    } else {
        LOG_DEBUG(psd_i, "Got the block: " << this->fft->get_block_id());
    }

    this->oneInN = this->usrp->get_block_ctrl<uhd::rfnoc::block_ctrl_base>(oneInNInfo.blockID);
    this->oneInNPort = oneInNInfo.port;

    if (not this->oneInN.get()) {
        LOG_FATAL(psd_i, "Unable to retrieve RF-NoC block with ID: " << this->oneInN->get_block_id());
        throw CF::LifeCycle::InitializeError();
    } else {
        LOG_DEBUG(psd_i, "Got the block: " << this->oneInN->get_block_id());
    }

    // Create a graph
    this->graph = this->usrp->create_graph("psd_" + this->_identifier);

    // Without this, there's no need to continue
    if (not this->graph.get()) {
        LOG_FATAL(psd_i, "Unable to retrieve RF-NoC graph");
        throw CF::LifeCycle::InitializeError();
    }

    // Connect the blocks
    this->graph->connect(this->fft->get_block_id(), this->fftPort, this->oneInN->get_block_id(), this->oneInNPort);

    // Setup based on properties initially
    if (not setFftSize(this->fftSize)) {
        LOG_FATAL(psd_i, "Unable to set FFT size with default value");
        throw CF::LifeCycle::InitializeError();
    }

    if (not setMagnitudeOut("MAGNITUDE_SQUARED")) {
        LOG_FATAL(psd_i, "Unable to set FFT magnitude_out with default value");
        throw CF::LifeCycle::InitializeError();
    }

    // Register property change listeners
    addPropertyListener(fftSize, this, &psd_i::fftSizeChanged);

    // Alert the persona of the block ID
    if (this->blockInfoChange) {
        std::vector<BlockInfo> blocks(1);

        blocks[0] = fftInfo;

        this->blockInfoChange(this->_identifier, blocks);
    }

    LOG_DEBUG(psd_i, "Adding a stream listener");

    // Add an SRI change listener
    this->dataShort_in->addStreamListener(this, &psd_i::streamChanged);
}

// Service functions for RX and TX
int psd_i::rxServiceFunction()
{
    // Perform RX, if necessary
    if (this->rxStream.get()) {
        // Don't bother doing anything until the SRI has been received
        if (not this->receivedSRI) {
            LOG_DEBUG(psd_i, "RX Thread active but no SRI has been received");

            if (not this->txThread) {
                bulkio::InShortPort::DataTransferType *packet = this->dataShort_in->getPacket(bulkio::Const::NON_BLOCKING);

                if (not packet) {
                    return NOOP;
                }

                bool updated = packet->sriChanged;

                if (updated) {
                    sriChanged(packet->SRI);
                }

                delete packet;

                if (not updated) {
                    return NOOP;
                }
            }
        }

        // Recv from the block
        uhd::rx_metadata_t md;

        LOG_TRACE(psd_i, "Calling recv on the rx_stream");

        size_t num_rx_samps = this->rxStream->recv(&this->output.front(), this->output.size(), md, 3.0);

        // Check the meta data for error codes
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            LOG_ERROR(psd_i, "Timeout while streaming");
            return NOOP;
        } else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            LOG_WARN(psd_i, "Overflow while streaming");
        } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            LOG_WARN(psd_i, md.strerror());
            return NOOP;
        }

        /*if (this->output.size() != num_rx_samps) {
            LOG_DEBUG(psd_i, "The RX stream is no longer valid, obtaining a new one");

            retrieveRxStream();
        }*/

        LOG_TRACE(psd_i, "RX Thread Requested " << this->output.size() << " samples");
        LOG_TRACE(psd_i, "RX Thread Received " << num_rx_samps << " samples");

        // Get the time stamps from the meta data
        BULKIO::PrecisionUTCTime rxTime;

        rxTime.twsec = md.time_spec.get_full_secs();
        rxTime.tfsec = md.time_spec.get_frac_secs();

        // Write the data to the output stream
        if (md.end_of_burst and this->expectEob) {
            this->psd_dataShort_out->pushPacket((short *) this->output.data(), this->output.size() * 2, rxTime, false, this->sri.streamID._ptr);
            this->expectEob = false;
        } else {
            this->psd_dataShort_out->pushPacket((short *) this->output.data(), this->output.size() * 2, rxTime, md.end_of_burst, this->sri.streamID._ptr);
        }
    }

    return NORMAL;
}

int psd_i::txServiceFunction()
{
    // Perform TX, if necessary
    if (this->txStream.get()) {
        // Wait on input data
        bulkio::InShortPort::DataTransferType *packet = this->dataShort_in->getPacket(bulkio::Const::BLOCKING);

        if (not packet) {
            return NOOP;
        }

        // Respond to the SRI changing
        if (packet->sriChanged) {
            sriChanged(packet->SRI);
        }

        // Prepare the metadata
        uhd::tx_metadata_t md;
        std::complex<short> *block = (std::complex<short> *) packet->dataBuffer.data();
        size_t blockSize = packet->dataBuffer.size() / 2;

        LOG_TRACE(psd_i, "TX Thread Received " << blockSize << " samples");

        if (blockSize == 0) {
            LOG_TRACE(psd_i, "Skipping empty packet");
            delete packet;
            return NOOP;
        }

        // Get the timestamp to send to the RF-NoC block
        BULKIO::PrecisionUTCTime time = packet->T;

        md.end_of_burst = this->eob;
        md.has_time_spec = true;
        md.time_spec = uhd::time_spec_t(time.twsec, time.tfsec);

        if (this->eob) {
            this->eob = false;
            this->expectEob = true;
        }

        // Send the data
        size_t num_tx_samps = this->txStream->send(block, blockSize, md);

        if (blockSize != 0 and num_tx_samps == 0) {
            LOG_DEBUG(psd_i, "The TX stream is no longer valid, obtaining a new one");

            retrieveTxStream();
        }

        LOG_TRACE(psd_i, "TX Thread Sent " << num_tx_samps << " samples");

        // On EOS, forward to the RF-NoC Block
        if (packet->EOS) {
            LOG_DEBUG(psd_i, "EOS");

            md.end_of_burst = true;

            std::vector<std::complex<short> > empty;
            this->txStream->send(&empty.front(), empty.size(), md);
        }

        delete packet;
    }

    return NORMAL;
}

// Override start and stop
void psd_i::start() throw (CF::Resource::StartError, CORBA::SystemException)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    psd_base::start();

    if (this->rxThread) {
        startRxStream();

        this->rxThread->start();
    }

    if (this->txThread) {
        this->txThread->start();
    }
}

void psd_i::stop() throw (CF::Resource::StopError, CORBA::SystemException)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (this->rxThread) {
        if (not this->rxThread->stop()) {
            LOG_WARN(psd_i, "RX Thread had to be killed");
        }

        stopRxStream();
    }

    if (this->txThread) {
        if (not this->txThread->stop()) {
            LOG_WARN(psd_i, "TX Thread had to be killed");
        }
    }

    psd_base::stop();
}

void psd_i::releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // This function clears the component running condition so main shuts down everything
    try {
        stop();
    } catch (CF::Resource::StopError& ex) {
        // TODO - this should probably be logged instead of ignored
    }

    releasePorts();
    stopPropertyChangeMonitor();
    // This is a singleton shared by all code running in this process
    //redhawk::events::Manager::Terminate();
    PortableServer::POA_ptr root_poa = ossie::corba::RootPOA();
    PortableServer::ObjectId_var oid = root_poa->servant_to_id(this);
    root_poa->deactivate_object(oid);

    component_running.signal();
}

/*
 * A method which allows a callback to be set for the block info changing. This
 * callback should point back to the persona to alert it of the component's
 * block IDs and ports
 */
void psd_i::setBlockInfoCallback(blockInfoCallback cb)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    this->blockInfoChange = cb;
}

/*
 * A method which allows the persona to set this component as an RX streamer.
 * This means the component should retrieve the data from block and then send
 * it out as bulkio data.
 */
void psd_i::setRxStreamer(bool enable)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (enable) {
        // Don't create an RX stream if it already exists
        if (this->rxStream.get()) {
            LOG_DEBUG(psd_i, "Attempted to set RX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(psd_i, "Attempting to set RX streamer");

        // Get the RX stream
        retrieveRxStream();

        // Create the receive buffer
        this->output.resize((0.8 * bulkio::Const::MAX_TRANSFER_BYTES / sizeof(std::complex<short>)));

        // Create the RX receive thread
        this->rxThread = new GenericThreadedComponent(boost::bind(&psd_i::rxServiceFunction, this));

        // If the component is already started, then start the RX receive thread
        if (this->_started) {
            startRxStream();

            this->rxThread->start();
        }
    } else {
        // Don't clean up the stream if it's not already running
        if (not this->rxStream.get()) {
            LOG_DEBUG(psd_i, "Attempted to unset RX streamer, but not streaming");
            return;
        }

        // Stop and delete the RX stream thread
        if (not this->rxThread->stop()) {
            LOG_WARN(psd_i, "RX Thread had to be killed");
        }

        // Stop continuous streaming
        stopRxStream();

        // Release the RX stream pointer
        this->rxStream.reset();

        delete this->rxThread;
        this->rxThread = NULL;
    }
}

/*
 * A method which allows the persona to set this component as a TX streamer.
 * This means the component should retrieve the data from the bulkio port and
 *  then send it to the block.
 */
void psd_i::setTxStreamer(bool enable)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (enable) {
        // Don't create a TX stream if it already exists
        if (this->txStream.get()) {
            LOG_DEBUG(psd_i, "Attempted to set TX streamer, but already streaming");
            return;
        }

        LOG_DEBUG(psd_i, "Attempting to set TX streamer");

        // Get the TX stream
        retrieveTxStream();

        // Create the TX transmit thread
        this->txThread = new GenericThreadedComponent(boost::bind(&psd_i::txServiceFunction, this));

        // If the component is already started, then start the TX transmit thread
        if (this->_started) {
            this->txThread->start();
        }
    } else {
        // Don't clean up the stream if it's not already running
        if (not this->txStream.get()) {
            LOG_DEBUG(psd_i, "Attempted to unset TX streamer, but not streaming");
            return;
        }

        // Stop and delete the TX stream thread
        if (not this->txThread->stop()) {
            LOG_WARN(psd_i, "TX Thread had to be killed");
        }

        // Release the TX stream pointer
        this->txStream.reset();

        delete this->txThread;
        this->txThread = NULL;
    }
}

/*
 * A method which allows the persona to set the USRP it is using.
 */
void psd_i::setUsrp(uhd::device3::sptr usrp)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // Save the USRP for later
    this->usrp = usrp;

    // Without a valid USRP, this component can't do anything
    if (not usrp.get()) {
        LOG_FATAL(psd_i, "Received a USRP which is not RF-NoC compatible.");
        throw std::exception();
    }
}

void psd_i::fftSizeChanged(const CORBA::ULong &oldValue, const CORBA::ULong &newValue)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (newValue < 16 or newValue > 4096) {
        LOG_WARN(psd_i, "Attempted to set fftSize to a value not in the range [16, 4096]");
        this->fftSize = oldValue;
        return;
    } else if ((newValue & (newValue - 1)) != 0) {
        LOG_WARN(psd_i, "Attempted to set fftSize to a value not a power of two");
        this->fftSize = oldValue;
        return;
    }

    if (not setFftSize(newValue)) {
        LOG_WARN(psd_i, "Unable to configure FFT with requested value");
        this->fftSize = oldValue;
    }
}

void psd_i::streamChanged(bulkio::InShortPort::StreamType stream)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    sriChanged(stream.sri());
}

void psd_i::retrieveRxStream()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // Release the old stream if necessary
    if (this->rxStream.get()) {
        LOG_DEBUG(psd_i, "Releasing old RX stream");
        this->rxStream.reset();
    }

    // Set the stream arguments
    // Only support short complex for now
    uhd::stream_args_t stream_args("sc16", "sc16");
    uhd::device_addr_t streamer_args;

    streamer_args["block_id"] = this->oneInN->get_block_id();

    // Get the spp from the block
    this->oneInNSpp = this->oneInN->get_args().cast<size_t>("spp", 1024);

    streamer_args["block_port"] = boost::lexical_cast<std::string>(this->oneInNPort);
    streamer_args["spp"] = boost::lexical_cast<std::string>(this->oneInNSpp);

    stream_args.args = streamer_args;

    LOG_DEBUG(psd_i, "Using streamer arguments: " << stream_args.args.to_string());

    // Retrieve the RX stream as specified from the device 3
    try {
        this->rxStream = this->usrp->get_rx_stream(stream_args);
    } catch(uhd::runtime_error &e) {
        LOG_ERROR(psd_i, "Failed to retrieve RX stream: " << e.what());
    } catch(...) {
        LOG_ERROR(psd_i, "Unexpected error occurred while retrieving RX stream");
    }
}

void psd_i::retrieveTxStream()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // Release the old stream if necessary
    if (this->txStream.get()) {
        LOG_DEBUG(psd_i, "Releasing old TX stream");
        this->txStream.reset();
    }

    // Set the stream arguments
    // Only support short complex for now
    uhd::stream_args_t stream_args("sc16", "sc16");
    uhd::device_addr_t streamer_args;

    streamer_args["block_id"] = this->fft->get_block_id();

    // Get the spp from the block
    this->fftSpp = this->fft->get_args().cast<size_t>("spp", 1024);

    streamer_args["block_port"] = boost::lexical_cast<std::string>(this->fftPort);
    streamer_args["spp"] = boost::lexical_cast<std::string>(this->fftSpp);

    stream_args.args = streamer_args;

    LOG_DEBUG(psd_i, "Using streamer arguments: " << stream_args.args.to_string());

    // Retrieve the TX stream as specified from the device 3
    try {
        this->txStream = this->usrp->get_tx_stream(stream_args);
    } catch(uhd::runtime_error &e) {
        LOG_ERROR(psd_i, "Failed to retrieve TX stream: " << e.what());
    } catch(...) {
        LOG_ERROR(psd_i, "Unexpected error occurred while retrieving TX stream");
    }
}

// Set the fft size on the FFT RF-NoC block
bool psd_i::setFftSize(const CORBA::ULong &newFftSize)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    try {
        uhd::device_addr_t args;

        args["spp"] = boost::lexical_cast<std::string>(newFftSize);

        this->fft->set_args(args, this->fftPort);
    } catch(uhd::value_error &e) {
        LOG_ERROR(psd_i, "Error while setting spp on FFT RF-NoC block: " << e.what());
        return false;
    } catch(...) {
        LOG_ERROR(psd_i, "Unknown error occurred while setting spp on FFT RF-NoC block");
        return false;
    }

    return true;
}

bool psd_i::setMagnitudeOut(const std::string &newMagnitudeOut)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    try {
        uhd::device_addr_t args;

        args["magnitude_out"] = boost::lexical_cast<std::string>(newMagnitudeOut);

        this->fft->set_args(args, this->fftPort);
    } catch(uhd::value_error &e) {
        LOG_ERROR(psd_i, "Error while setting magnitude_out on FFT RF-NoC block: " << e.what());
        return false;
    } catch(...) {
        LOG_ERROR(psd_i, "Unknown error occurred while setting magnitude_out on FFT RF-NoC block");
        return false;
    }

    return true;
}

void psd_i::sriChanged(const BULKIO::StreamSRI &newSRI)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    this->sri = newSRI;

    this->psd_dataShort_out->pushSRI(this->sri);

    this->receivedSRI = true;
}

void psd_i::startRxStream()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (not this->rxStreamStarted) {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = 0;
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();

        this->rxStream->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = true;
    }
}

void psd_i::stopRxStream()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (this->rxStreamStarted) {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStream->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = false;
    }
}

