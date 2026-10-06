#include "NXP_CAN_ProjDef.h"
#include "GenericMacro.h"

const uint32_t uiaCAN_TxConfigIDs[eNUMBER_OF_CAN_HOST_RXIDs] = {
    CAN_HOST_NODE_BOOTLOADER_ID,
    CAN_HOST_NODE_SYSMGMT_ID
};

static const struct device *const pstCAN_Device = DEVICE_DT_GET(DT_NODELABEL(flexcan0));

const struct device *pstGetCAN_Device(void)
{
    return pstCAN_Device;
}

uint32_t uiGetCAN_DestID(eT_CAN_DestFiltID_t eDestNode)
{
    if((unsigned)eDestNode >= eNUMBER_OF_CAN_HOST_RXIDs)
    {
        FHALT("Invalid CAN destination node");
        return UINT32_MAX;
    }
    return uiaCAN_TxConfigIDs[eDestNode];
}

int iGetCAN_RxFilterID(eT_CAN_RxFilter_ID_t eRxFilterId)
{
    switch(eRxFilterId)
    {
        case eCAN_RxFilter_ID_SysMgt:
            return (int)CAN_RX_SYS_MGMT_ID;
        case eCAN_RxFilter_ID_Bootloader:
            return (int)CAN_RX_BOOTLOADER_ID;
        case eCAN_RxFilter_ID_HostBootloader:
            return (int)CAN_RX_HOST_BOOTLOADER_ID;
        default:
            FHALT("Invalid CAN RX filter configuration");
            return -1;
    }
}
