/*
 * RFNoC_Utils.cpp
 *
 *  Created on: Dec 28, 2016
 *      Author: Patrick
 */

#include <uhd/rfnoc/sink_block_ctrl_base.hpp>
#include <uhd/rfnoc/source_block_ctrl_base.hpp>

#include "RFNoC_Utils.h"

bool operator==(const BlockInfo &lhs, const BlockInfo &rhs)
{
    return (lhs.blockID == rhs.blockID and lhs.port == rhs.port);
}

BlockInfo findAvailableChannel(const uhd::device3::sptr usrp, const std::string &blockID)
{
    std::vector<uhd::rfnoc::block_id_t> blockIDs = usrp->find_blocks(blockID);
    BlockInfo blockInfo;

    for (size_t i = 0; i < blockIDs.size(); ++i) {
        uhd::rfnoc::sink_block_ctrl_base::sptr sinkBlock;
        uhd::rfnoc::source_block_ctrl_base::sptr sourceBlock;

        try {
            sinkBlock = usrp->get_block_ctrl<uhd::rfnoc::sink_block_ctrl_base>(blockID);
        } catch(uhd::value_error &e) {
            continue;
        }

        try {
            sourceBlock = usrp->get_block_ctrl<uhd::rfnoc::source_block_ctrl_base>(blockID);
        } catch(uhd::value_error &e) {
            continue;
        }

        size_t numFullChannels = std::min(sinkBlock->get_input_ports().size(), sourceBlock->get_output_ports().size());

        for (size_t j = 0; j < numFullChannels; ++j) {
            try {
                sinkBlock->get_upstream_port(j);
            } catch(uhd::value_error &e) {
                try {
                    sourceBlock->get_downstream_port(j);
                } catch(uhd::value_error &e) {
                    blockInfo.blockID = blockIDs[i];
                    blockInfo.port = j;

                    return blockInfo;
                }
            }
        }
    }

    return blockInfo;
}

BlockInfo findAvailableSink(const uhd::device3::sptr usrp, const std::string &blockID)
{
    std::vector<uhd::rfnoc::block_id_t> blockIDs = usrp->find_blocks(blockID);
    BlockInfo blockInfo;

    for (size_t i = 0; i < blockIDs.size(); ++i) {
        uhd::rfnoc::sink_block_ctrl_base::sptr block = usrp->get_block_ctrl<uhd::rfnoc::sink_block_ctrl_base>(blockID);
        std::vector<size_t> inputPorts = block->get_input_ports();

        for (size_t j = 0; j < inputPorts.size(); ++j) {
            try {
                block->get_upstream_port(j);
            } catch(uhd::value_error &e) {
                blockInfo.blockID = blockIDs[i];
                blockInfo.port = j;

                return blockInfo;
            }
        }
    }

    return blockInfo;
}

BlockInfo findAvailableSource(const uhd::device3::sptr usrp, const std::string &blockID)
{
    std::vector<uhd::rfnoc::block_id_t> blockIDs = usrp->find_blocks(blockID);
    BlockInfo blockInfo;

    for (size_t i = 0; i < blockIDs.size(); ++i) {
        uhd::rfnoc::source_block_ctrl_base::sptr block = usrp->get_block_ctrl<uhd::rfnoc::source_block_ctrl_base>(blockID);
        std::vector<size_t> outputPorts = block->get_output_ports();

        for (size_t j = 0; j < outputPorts.size(); ++j) {
            try {
                block->get_downstream_port(j);
            } catch(uhd::value_error &e) {
                blockInfo.blockID = blockIDs[i];
                blockInfo.port = j;

                return blockInfo;
            }
        }
    }

    return blockInfo;
}
