#include "CAN_Controller.h"
#include <stdint.h>
#include <stdbool.h>

sT_CAN_RXFilterConfig_t stTCANConfig[eNUMBER_OF_CAN_RX_CONFIGs];
void vCAN_RXCallback(const struct device *dev, struct can_frame *frame, void *user_data);

void vInit_CANController( void )
{
    stTCANConfig[0].eID = eCAN_RxFilter_ID_SysMgt;
    stTCANConfig[0].eRxType = eCAN_RxType_Callback;
    stTCANConfig[0].eFilterIdType = eCAN_RxFilterType_Standard;
    
    if(stTCANConfig[0].eRxType == eCAN_RxType_Callback)
        stTCANConfig[0].rxCallback = &vCAN_RXCallback;
    stTCANConfig[0].pUserData = NULL;

    stTCANConfig[1].eID = eCAN_RxFilter_ID_Bootloader;
    stTCANConfig[1].eRxType = eCAN_RxType_Callback;
    stTCANConfig[1].eFilterIdType = eCAN_RxFilterType_Standard;
    
    if(stTCANConfig[1].eRxType == eCAN_RxType_Callback)
        stTCANConfig[1].rxCallback = &vCAN_RXCallback;
    stTCANConfig[1].pUserData = NULL;
    
    iFlexCAN_Init(stTCANConfig, eNUMBER_OF_CAN_RX_CONFIGs);
}

void vCAN_RXCallback(const struct device *dev, struct can_frame *frame, void *user_data)
{

}