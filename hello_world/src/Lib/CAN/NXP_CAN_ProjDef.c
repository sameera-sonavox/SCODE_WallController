#include "NXP_CAN_ProjDef.h"
#include "../GenericMacro.h"

const uint32_t uiaCAN_TxConfigIDs[eNUMBER_OF_CAN_TX_CONFIGs] = {
    CAN_NODE_0_ID
};

const uint32_t uiaCAN_RxFilterIDs[eNUMBER_OF_CAN_RX_CONFIGs] = {
    CAN_RX_SYS_MGMT_ID,
    CAN_RX_BOOTLOADER_ID
};

static const struct device *const pstCAN_Device = DEVICE_DT_GET(DT_NODELABEL(flexcan0));

const struct device * pstGetCAN_Device(void)
{
    return pstCAN_Device;
}

int iGetCAN_DestID(eT_CAN_DestNode_t eDestNode)
{
    if(eDestNode >= eNUMBER_OF_CAN_TX_CONFIGs || eDestNode < 0)
    {
        FHALT("Invalid CAN Destination Node");
        //Invalid Destination Node
        return -1;
    }

    return uiaCAN_TxConfigIDs[eDestNode];
}

int iGetCAN_RxFilterID(eT_CAN_RxFilter_ID_t eRxFilterId)
{
    if(eRxFilterId >= eNUMBER_OF_CAN_RX_CONFIGs || eRxFilterId < 0)
    {
        FHALT("Invalid CAN Rx Filter Configuration");
        //Invalid Rx Filter Configuration
        return -1;
    }

    return uiaCAN_RxFilterIDs[eRxFilterId];
}