#ifndef RFNOCCOMPONENT_H
#define RFNOCCOMPONENT_H

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <uhd/device3.hpp>
#include <uhd/rfnoc/block_id.hpp>
#include <string>
#include <vector>

#include "RFNoC_Utils.h"

/*
 * A callback on the component to be called by the persona. This lets the
 * persona inform the component of its status as an RX and/or TX endpoint to the
 * RF-NoC block.
 */
typedef boost::function<void(bool shouldStream)> setStreamerCallback;

/*
 * A callback on the persona to be called by the component. This lets the
 * component inform the persona which function it should call to set the
 * component up for RX and/or TX streaming from/to the RF-NoC block
 */
typedef boost::function<void(const std::string &componentID, setStreamerCallback setStreamCb)> setSetStreamerCallback;

/*
 * A callback on the persona to be called by the component. This lets the
 * component inform the persona which RF-NoC block(s) the component is
 * claiming.
 */
typedef boost::function<void(const std::string &componentID, const std::vector<BlockInfo> &blockInfos)> blockInfoCallback;

/*
 * A callback on the persona to be called by the component. This lets the
 * component inform the persona of incoming/outgoing connections being added or
 * removed
 */
typedef boost::function<void(const std::string &ID, const CORBA::ULong &hash)> connectionCallback;

/*
 * An abstract base class to be implemented by an RF-NoC component designer.
 */
class RFNoC_ComponentInterface {
    public:
        /*
         * This method should keep a copy of the blockInfoCallback and/or call it
         * with the component's block info(s).
         */
        virtual void setBlockInfoCallback(blockInfoCallback cb) = 0;

        /*
         * This method should keep a copy of the connectionCallback to be called
         * when a new connection on the provides port occurs
         */
        virtual void setNewIncomingConnectionCallback(connectionCallback cb) = 0;

        /*
         * This method should keep a copy of the connectionCallback to be called
         * when a new connection on the uses port occurs
         */
        virtual void setNewOutgoingConnectionCallback(connectionCallback cb) = 0;

        /*
         * This method should keep a copy of the connectionCallback to be called
         * when a connection is removed on the provides port
         */
        virtual void setRemovedIncomingConnectionCallback(connectionCallback cb) = 0;

        /*
         * This method should keep a copy of the connectionCallback to be called
         * when a connection is removed on the uses port
         */
        virtual void setRemovedOutgoingConnectionCallback(connectionCallback cb) = 0;

        /*
         * This method should enable streaming from the component's last/only
         * RF-NoC block. This includes converting the data to bulkio and
         * creating appropriate SRI.
         */
        virtual void setRxStreamer(bool enable) = 0;

        /*
         * This method should enable streaming to the component's first/only
         * RF-NoC block. This includes converting the bulkio data to the
         * appropriate format for the block and responding to the upstream SRI.
         */
        virtual void setTxStreamer(bool enable) = 0;

        /*
         * This method should keep a copy of the device3 shared pointer.
         */
        virtual void setUsrp(uhd::device3::sptr usrp) = 0;

        /*
         * For safe inheritance.
         */
        virtual ~RFNoC_ComponentInterface() {};
};

#endif
