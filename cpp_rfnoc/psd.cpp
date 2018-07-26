// Class Include
#include "psd.h"

// RF-NoC RH Utils
#include <RFNoC_Utils.h>

PREPARE_LOGGING(psd_i)

// Initialize non-RH members
psd_i::psd_i(const char *uuid, const char *label) :
    psd_base(uuid, label),
    eob(false),
    expectEob(false),
    fftPort(-1),
    fftSpp(512),
    receivedSRI(false),
    rxStreamStarted(false)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);
}

// Clean up the RF-NoC stream and threads
psd_i::~psd_i()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // Stop streaming
    stopRxStream();

    // Release the threads if necessary
    if (this->rxThread)
    {
        this->rxThread->stop();
    }

    if (this->txThread)
    {
        this->txThread->stop();
    }
}

/*
 * Public Method(s)
 */

// Make sure the singleton isn't terminated
void psd_i::releaseObject() throw (CF::LifeCycle::ReleaseError, CORBA::SystemException)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // This function clears the component running condition so main shuts down everything
    try
    {
        stop();
    }
    catch (CF::Resource::StopError& ex)
    {
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

// Override start to call start on the RX and TX threads
void psd_i::start() throw (CF::Resource::StartError, CORBA::SystemException)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    psd_base::start();

    if (this->rxThread)
    {
        startRxStream();

        this->rxThread->start();
    }

    if (this->txThread)
    {
        this->txThread->start();
    }
}

// Override stop to call stop on the RX and TX threads
void psd_i::stop() throw (CF::Resource::StopError, CORBA::SystemException)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (this->rxThread)
    {
        if (not this->rxThread->stop())
        {
            LOG_WARN(psd_i, "RX Thread had to be killed");
        }

        stopRxStream();
    }

    if (this->txThread)
    {
        if (not this->txThread->stop())
        {
            LOG_WARN(psd_i, "TX Thread had to be killed");
        }
    }

    psd_base::stop();
}

/*
 * Public RFNoC_Component Method(s)
 */

// A method which allows the persona to set this component as an RX streamer.
// This means the component should retrieve the data from block and then send
// it out as bulkio data.
void psd_i::setRxStreamer(uhd::rx_streamer::sptr rxStreamer)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (rxStreamer)
    {
        // This shouldn't happen
        if (this->rxStreamer)
        {
            LOG_DEBUG(psd_i, "Replacing existing RX streamer");
            return;
        }

        LOG_DEBUG(psd_i, "Attempting to set RX streamer");

        // Set the RX stream
        this->rxStreamer = rxStreamer;

        // Create the RX receive thread
        this->rxThread = boost::make_shared<RFNoC_RH::GenericThreadedComponent>(boost::bind(&psd_i::rxServiceFunction, this));

        // If the component is already started, then start the RX receive thread
        if (this->_started)
        {
            startRxStream();

            this->rxThread->start();
        }
    }
    else
    {
        // Don't clean up the stream if it's not already running
        if (not this->rxStreamer)
        {
            LOG_DEBUG(psd_i, "Attempted to unset RX streamer, but not streaming");
            return;
        }

        // Stop and delete the RX stream thread
        if (not this->rxThread->stop())
        {
            LOG_WARN(psd_i, "RX Thread had to be killed");
        }

        // Stop continuous streaming
        stopRxStream();

        // Release the RX stream pointer
        LOG_DEBUG(psd_i, "Resetting RX stream");
        this->rxStreamer.reset();

        this->rxThread.reset();
    }
}

// A method which allows the persona to set this component as a TX streamer.
// This means the component should retrieve the data from the bulkio port and
// then send it to the block.
void psd_i::setTxStreamer(uhd::tx_streamer::sptr txStreamer)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (txStreamer)
    {
        // This shouldn't happen
        if (this->txStreamer)
        {
            LOG_DEBUG(psd_i, "Replacing TX streamer");
            return;
        }

        LOG_DEBUG(psd_i, "Attempting to set TX streamer");

        // Set the TX stream
        this->txStreamer = txStreamer;

        // Create the TX transmit thread
        this->txThread = boost::make_shared<RFNoC_RH::GenericThreadedComponent>(boost::bind(&psd_i::txServiceFunction, this));

        // If the component is already started, then start the TX transmit thread
        if (this->_started)
        {
            this->txThread->start();
        }
    }
    else
    {
        // Don't clean up the stream if it's not already running
        if (not this->txStreamer)
        {
            LOG_DEBUG(psd_i, "Attempted to unset TX streamer, but not streaming");
            return;
        }

        // Stop and delete the TX stream thread
        if (not this->txThread->stop())
        {
            LOG_WARN(psd_i, "TX Thread had to be killed");
        }

        // Release the TX stream pointer
        this->txStreamer.reset();

        this->txThread.reset();
    }
}

/*
 * Protected Method(s)
 */

// Initialize members dependent on RH properties
void psd_i::constructor()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    // Construct a BlockDescriptor
    RFNoC_RH::BlockDescriptor blockDescriptor;

    blockDescriptor.blockId = "FFT";

    // Grab the pointer to the specified block ID
    this->fft = this->persona->getBlock(blockDescriptor);

    // Without this, there is no need to continue
    if (not this->fft)
    {
        LOG_FATAL(psd_i, "Unable to find RF-NoC block with ID: FFT");
        throw CF::LifeCycle::InitializeError();
    }
    else
    {
        LOG_DEBUG(psd_i, "Got the block: FFT");
    }

    this->fftPort = blockDescriptor.port;

    // Setup based on properties initially
    if (not setMagnitudeOut("MAGNITUDE"))
    {
        LOG_FATAL(psd_i, "Unable to set FFT magnitude_out with default value");
        throw CF::LifeCycle::InitializeError();
    }

    if (not setFftSize(this->fftSize))
    {
        LOG_FATAL(psd_i, "Unable to set FFT size with default value");
        throw CF::LifeCycle::InitializeError();
    }

    // Alert the persona of stream descriptors for this component
    RFNoC_RH::StreamDescriptor streamDescriptor;

    streamDescriptor.cpuFormat = "sc16";
    streamDescriptor.otwFormat = "sc16";
    streamDescriptor.streamArgs["block_id"] = this->fft->get_block_id();
    streamDescriptor.streamArgs["block_port"] = this->fftPort;

    // Get the spp from the block
    this->fftSpp = this->fft->get_args().cast<size_t>("spp", 512);

    streamDescriptor.streamArgs["spp"] = boost::lexical_cast<std::string>(this->fftSpp);

    // Register property change listeners
    addPropertyListener(fftSize, this, &psd_i::fftSizeChanged);

    // Add an SRI change listener
    this->dataShort_in->addStreamListener(this, &psd_i::streamChanged);

    // Add a stream listener
    this->psd_dataShort_out->setNewConnectListener(this, &psd_i::newConnection);
    this->psd_dataShort_out->setNewDisconnectListener(this, &psd_i::newDisconnection);
}

// The service function for receiving from the RF-NoC block
int psd_i::rxServiceFunction()
{
    // Perform RX, if necessary
    if (this->rxStreamer)
    {
        // Don't bother doing anything until the SRI has been received
        if (not this->receivedSRI)
        {
            LOG_DEBUG(psd_i, "RX Thread active but no SRI has been received");
            return NOOP;
        }

        // Recv from the block
        uhd::rx_metadata_t md;

        size_t samplesRead = 0;
        size_t samplesToRead = this->output.size();

        while (samplesRead < this->output.size())
        {
            LOG_DEBUG(psd_i, "Calling recv on the rx_stream");

            size_t num_rx_samps = this->rxStreamer->recv(&output.front() + samplesRead, samplesToRead, md, 1.0);

            // Check the meta data for error codes
            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
            {
                LOG_ERROR(psd_i, "Timeout while streaming");
                return NOOP;
            }
            else if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW)
            {
                LOG_WARN(psd_i, "Overflow while streaming");
            }
            else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
            {
                LOG_WARN(psd_i, md.strerror());
                this->rxStreamStarted = false;
                startRxStream();
                return NOOP;
            }

            LOG_DEBUG(psd_i, "RX Thread Requested " << samplesToRead << " samples");
            LOG_DEBUG(psd_i, "RX Thread Received " << num_rx_samps << " samples");

            samplesRead += num_rx_samps;
            samplesToRead -= num_rx_samps;
        }

        // Get the time stamps from the meta data
        BULKIO::PrecisionUTCTime rxTime;

        rxTime.twsec = md.time_spec.get_full_secs();
        rxTime.tfsec = md.time_spec.get_frac_secs();

        // Write the data to the output stream
        if (md.end_of_burst and this->expectEob)
        {
            this->psd_dataShort_out->pushPacket((short *) this->output.data(), this->output.size() * 2, rxTime, false, this->sri.streamID._ptr);
            this->expectEob = false;
        }
        else
        {
            this->psd_dataShort_out->pushPacket((short *) this->output.data(), this->output.size() * 2, rxTime, md.end_of_burst, this->sri.streamID._ptr);
        }
    }

    return NORMAL;
}

int psd_i::txServiceFunction()
{
    // Perform TX, if necessary
    if (this->txStreamer)
    {
        // Wait on input data
        bulkio::InShortPort::DataTransferType *packet = this->dataShort_in->getPacket(bulkio::Const::BLOCKING);

        if (not packet)
        {
            return NOOP;
        }

        // Respond to the SRI changing
        if (packet->sriChanged)
        {
            sriChanged(packet->SRI);
        }

        // Prepare the metadata
        uhd::tx_metadata_t md;
        std::complex<short> *block = (std::complex<short> *) packet->dataBuffer.data();
        size_t blockSize = packet->dataBuffer.size() / 2;

        LOG_TRACE(psd_i, "TX Thread Received " << blockSize << " samples");

        if (blockSize == 0)
        {
            LOG_TRACE(psd_i, "Skipping empty packet");
            delete packet;
            return NOOP;
        }

        // Get the timestamp to send to the RF-NoC block
        BULKIO::PrecisionUTCTime time = packet->T;

        md.end_of_burst = this->eob;
        md.has_time_spec = true;
        md.time_spec = uhd::time_spec_t(time.twsec, time.tfsec);

        if (this->eob)
        {
            this->eob = false;
            this->expectEob = true;
        }

        // Send the data
        size_t samplesSent = 0;
        size_t samplesToSend = blockSize;

        while (samplesToSend != 0)
        {
            // Send the data
            size_t num_tx_samps = this->txStreamer->send(block + samplesSent, samplesToSend, md, 1);

            samplesSent += num_tx_samps;
            samplesToSend -= num_tx_samps;

            LOG_DEBUG(psd_i, "TX Thread Sent " << num_tx_samps << " samples");
        }

        // On EOS, forward to the RF-NoC Block
        if (packet->EOS)
        {
            LOG_DEBUG(psd_i, "EOS");

            md.end_of_burst = true;

            std::vector<std::complex<short> > empty;
            this->txStreamer->send(&empty.front(), empty.size(), md);
        }

        delete packet;
    }

    return NORMAL;
}

/*
 * Private Method(s)
 */

void psd_i::fftSizeChanged(const CORBA::ULong &oldValue, const CORBA::ULong &newValue)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (newValue < 16 or newValue > 4096)
    {
        LOG_WARN(psd_i, "Attempted to set fftSize to a value not in the range [16, 4096]");
        this->fftSize = oldValue;
        return;
    }
    else if ((newValue & (newValue - 1)) != 0)
    {
        LOG_WARN(psd_i, "Attempted to set fftSize to a value not a power of two");
        this->fftSize = oldValue;
        return;
    }

    if (not setFftSize(newValue))
    {
        LOG_WARN(psd_i, "Unable to configure FFT with requested value");
        this->fftSize = oldValue;
    }
}

void psd_i::streamChanged(bulkio::InShortPort::StreamType stream)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    std::map<std::string, bool>::iterator it = this->streamMap.find(stream.streamID());

    bool newIncomingConnection = (it == this->streamMap.end());
    bool removedIncomingConnection = (it != this->streamMap.end() and stream.eos());

    if (newIncomingConnection)
    {
        LOG_DEBUG(psd_i, "New incoming connection");

        this->persona->incomingConnectionAdded(this->identifier(),
        									   stream.streamID(),
											   this->dataShort_in->_this()->_hash(RFNoC_RH::HASH_SIZE));

        this->streamMap[stream.streamID()] = true;
    }
    else if (removedIncomingConnection)
    {
        LOG_DEBUG(psd_i, "Removed incoming connection");

        this->persona->incomingConnectionRemoved(this->identifier(),
												 stream.streamID(),
												 this->dataShort_in->_this()->_hash(RFNoC_RH::HASH_SIZE));

        this->streamMap.erase(it);
    }

    LOG_DEBUG(psd_i, "Got SRI for stream ID: " << stream.streamID());

    sriChanged(stream.sri());
}

void psd_i::newConnection(const char *connectionID)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

	BULKIO::UsesPortStatisticsProvider_ptr port = BULKIO::UsesPortStatisticsProvider::_narrow(this->getPort(this->psd_dataShort_out->getName().c_str()));

	this->persona->outgoingConnectionAdded(this->identifier(), connectionID, port->_hash(RFNoC_RH::HASH_SIZE));
}

void psd_i::newDisconnection(const char *connectionID)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

	BULKIO::UsesPortStatisticsProvider_ptr port = BULKIO::UsesPortStatisticsProvider::_narrow(this->getPort(this->psd_dataShort_out->getName().c_str()));

	this->persona->outgoingConnectionRemoved(this->identifier(), connectionID, port->_hash(RFNoC_RH::HASH_SIZE));
}

void psd_i::setFftReset()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    /*try
    {
        uhd::device_addr_t args;

        args["reset"] = boost::lexical_cast<std::string>(1);

        this->fft->set_args(args, this->fftPort);
    }
    catch(uhd::value_error &e)
    {
        LOG_ERROR(psd_i, "Error while setting reset on FFT RF-NoC block: " << e.what());
    }
    catch(...)
    {
        LOG_ERROR(psd_i, "Unknown error occurred while setting reset on FFT RF-NoC block");
    }*/
}

// Set the fft size on the FFT RF-NoC block
bool psd_i::setFftSize(const CORBA::ULong &newFftSize)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    try
    {
        uhd::device_addr_t args;

        args["spp"] = boost::lexical_cast<std::string>(newFftSize);

        this->fft->set_args(args, this->fftPort);
    }
    catch(uhd::value_error &e)
    {
        LOG_ERROR(psd_i, "Error while setting spp on FFT RF-NoC block: " << e.what());
        return false;
    }
    catch(...)
    {
        LOG_ERROR(psd_i, "Unknown error occurred while setting spp on FFT RF-NoC block");
        return false;
    }

    setFftReset();

    return true;
}

bool psd_i::setMagnitudeOut(const std::string &newMagnitudeOut)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    try
    {
        uhd::device_addr_t args;

        args["magnitude_out"] = boost::lexical_cast<std::string>(newMagnitudeOut);

        this->fft->set_args(args, this->fftPort);
    }
    catch(uhd::value_error &e)
    {
        LOG_ERROR(psd_i, "Error while setting magnitude_out on FFT RF-NoC block: " << e.what());
        return false;
    }
    catch(...)
    {
        LOG_ERROR(psd_i, "Unknown error occurred while setting magnitude_out on FFT RF-NoC block");
        return false;
    }

    setFftReset();

    return true;
}

void psd_i::sriChanged(const BULKIO::StreamSRI &newSRI)
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    this->sri = newSRI;

    this->sri.xdelta = 1.0 / (newSRI.xdelta * this->fftSize);

    double ifStart = -((this->fftSize / 2 - 1) * this->sri.xdelta);

    // TODO: Check for CHAN_RF/COL_RF
    this->sri.xstart = ifStart;

    this->sri.subsize = this->fftSize;

    this->sri.ydelta = newSRI.xdelta * this->fftSize;
    this->sri.yunits = BULKIO::UNITS_TIME;
    this->sri.xunits = BULKIO::UNITS_FREQUENCY;
    this->sri.mode = 1;

    this->psd_dataShort_out->pushSRI(this->sri);

    this->receivedSRI = true;
}

void psd_i::startRxStream()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (not this->rxStreamStarted)
    {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.num_samps = 0;
        stream_cmd.stream_now = true;
        stream_cmd.time_spec = uhd::time_spec_t();

        this->rxStreamer->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = true;
    }
}

void psd_i::stopRxStream()
{
    LOG_TRACE(psd_i, __PRETTY_FUNCTION__);

    if (this->rxStreamStarted)
    {
        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

        this->rxStreamer->issue_stream_cmd(stream_cmd);

        this->rxStreamStarted = false;

        // Run recv until nothing is left
        uhd::rx_metadata_t md;
        int num_post_samps = 0;

        LOG_DEBUG(psd_i, "Emptying receive queue...");

        do
        {
            num_post_samps = this->rxStreamer->recv(&this->output.front(), this->output.size(), md, 1.0);
        } while(num_post_samps and md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE);

        LOG_DEBUG(psd_i, "Emptied receive queue");
    }
}
