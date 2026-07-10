
#include "../API_Usage_Definition.h"

#if defined(USE_SPI)

#include <string.h>
#include "NXP_SPI_LinkedList.h"
#include "NXP_SPI_API.h"
#include "../GenericMacro.h"

sT_SPISlave_Control_t* pstAllocate_MemoryForNode( void );

sT_SPISlave_Control_t* pstCreate_SPISlave(sT_SPISlave_Config_t *pstSlaveConfig)
{
    if(pstSlaveConfig == NULL)
    {
        FHALT("Null Pointer reference");
        return NULL;
    }
    sT_SPISlave_Control_t *pstSlave = pstAllocate_MemoryForNode();
    if(pstSlave == NULL)
        return NULL;

    pstSlave->stTConfigs = *pstSlaveConfig;
    pstSlave->pstNextSlave = NULL;
    return pstSlave;
}

bool bInsert_SPISlave_AtBeginning(sT_SPISlave_Control_t **ppstHead, sT_SPISlave_Config_t *pstSlaveConfig)
{
    if(ppstHead == NULL || pstSlaveConfig == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    sT_SPISlave_Control_t *pstSlave = pstCreate_SPISlave(pstSlaveConfig);
    if(pstSlave == NULL)
        return false;
    
    pstSlave->pstNextSlave = *ppstHead;
    *ppstHead = pstSlave;
    return true;
}

bool bInsert_SPISlave_AtEnd(sT_SPISlave_Control_t **ppstHead, sT_SPISlave_Config_t *pstSlaveConfig)
{
    if(ppstHead == NULL || pstSlaveConfig == NULL)
    {
        FHALT("Null Pointer reference");
        return false;
    }

    sT_SPISlave_Control_t *pstSlave = pstCreate_SPISlave(pstSlaveConfig);
    if(pstSlave == NULL)
        return false;
    
    sT_SPISlave_Control_t *pstTemp = *ppstHead;

    while(pstTemp != NULL && pstTemp->pstNextSlave != NULL)
    {
        pstTemp = pstTemp->pstNextSlave;
    }

    if(pstTemp == NULL)
    {
        *ppstHead = pstSlave;
    }
    else
    {
        pstTemp->pstNextSlave = pstSlave;
    }
    
    return true;
}

void vRelease_SPISLaves(sT_SPISlave_Control_t **ppstHead)
{
    if(ppstHead == NULL)
    {
        FHALT("Null Pointer reference");
        return;
    }

    sT_SPISlave_Control_t *pstSlave = *ppstHead;
    while(pstSlave != NULL)
    {
        sT_SPISlave_Control_t *pstTemp = pstSlave->pstNextSlave;
        free(pstSlave);
        pstSlave = pstTemp;
    }
    *ppstHead = NULL;
}

sT_SPISlave_Control_t *pstGetSlaveInfo(eSPI_Slave_Id_t eSlaveId, sT_SPISlave_Control_t *pstHead)
{
    if(pstHead == NULL)
    {
        FHALT("Null Pointer reference");
        return NULL;
    }
    if(eSlaveId >= eNUMBER_OF_SPI_SLAVEs)
    {
        FHALT("Slave does not exist @Id : %d", eSlaveId);
        return NULL;        
    }

    sT_SPISlave_Control_t *pstSlave = pstHead;
    while(pstSlave != NULL)
    {
        sT_SPISlave_Config_t *pstTConfigs = &pstSlave->stTConfigs;
        if(pstTConfigs->eSlaveId == eSlaveId)
            return pstSlave;
        pstSlave = pstSlave->pstNextSlave;
    }
    return pstSlave;
}

sT_SPISlave_Control_t* pstAllocate_MemoryForNode( void )
{
    sT_SPISlave_Control_t *pstSlave = (sT_SPISlave_Control_t *)malloc(sizeof(sT_SPISlave_Control_t));
    if(pstSlave == NULL)
    {
        FHALT("SPI Slave could not be created");
        return NULL;
    }
    memset(pstSlave, 0, sizeof(sT_SPISlave_Control_t));
    return pstSlave;
}


sT_SPIRxBuff_t *pstCreate_SPIRxBuffer(uint8_t uiId, eRxBuffer_State_t eState, size_t uisize)
{
    sT_SPIRxBuff_t *pstRxBuffConfig = (sT_SPIRxBuff_t *)malloc(sizeof(sT_SPIRxBuff_t));
    if(pstRxBuffConfig == NULL)
    {
        FHALT("Couldn't allocate memory for Rx Buff configs @Id: %d", uiId);
        return NULL;        
    }

    uint8_t *puiRxBuffer = (uint8_t * )malloc(uisize * sizeof(uint8_t));
    if(puiRxBuffer == NULL)
    {
        free(pstRxBuffConfig);
        pstRxBuffConfig = NULL;
        FHALT("Couldn't allocate memory for Rx Buffer @Id: %d", uiId);
        return NULL;
    }

    pstRxBuffConfig->uiBuffId = uiId;
    pstRxBuffConfig->eState = eState;
    pstRxBuffConfig->uisize = uisize;
    pstRxBuffConfig->puiBuffer = puiRxBuffer;

    puiRxBuffer = NULL;    
    pstRxBuffConfig->pstNextRxBuff = NULL;
    return pstRxBuffConfig;
}

bool bInsert_SPIRxBuffer_AtEnd(sT_SPIRxBuff_t **ppstHead, uint8_t uiId, eRxBuffer_State_t eState, size_t uisize)
{
    sT_SPIRxBuff_t *pstNewBuffer = pstCreate_SPIRxBuffer(uiId, eState, uisize);
    if(pstNewBuffer == NULL)
    {
        return false;
    }

    sT_SPIRxBuff_t *pstTemp = *ppstHead;
    while(pstTemp != NULL && pstTemp->pstNextRxBuff != NULL)
    {
        pstTemp = pstTemp->pstNextRxBuff;
    }

    if(pstTemp == NULL)
    {
        *ppstHead = pstNewBuffer;
    }
    else
    {
        pstTemp->pstNextRxBuff = pstNewBuffer;
    }    
    return true;
}

sT_SPIRxBuff_t *pstGet_RxBuffer_byState(sT_SPIRxBuff_t *pstHead, eRxBuffer_State_t eState)
{
    sT_SPIRxBuff_t *pstTemp = pstHead;
    while(pstTemp != NULL)
    {
        if(pstTemp->eState == eState)
            return pstTemp;
        pstTemp = pstTemp->pstNextRxBuff;
    }
    return NULL;
}

sT_SPIRxBuff_t *pstGet_RxReadyBuffer_byId(sT_SPIRxBuff_t *pstHead, uint8_t uiId)
{
    sT_SPIRxBuff_t *pstTemp = pstHead;

    while(pstTemp != NULL)
    {
        if(pstTemp->uiBuffId == uiId && pstTemp->eState == eBuffer_Ready)
            return pstTemp;
        pstTemp = pstTemp->pstNextRxBuff;
    }
    return NULL;
}

void vSet_RxBufferState(uint8_t uiId, eRxBuffer_State_t eState, sT_SPIRxBuff_t *pstHead)
{
    sT_SPIRxBuff_t *pstTemp = pstHead;
    while(pstTemp != NULL)
    {
        if(pstTemp->uiBuffId == uiId)
            break;
        pstTemp = pstTemp->pstNextRxBuff;
    }

    if(pstTemp == NULL)
    {
        FHALT("Invalid Buffer Id: %d", uiId);
        return;
    }

    pstTemp->eState = eState;
}

void vFree_SPIRxBuffers( sT_SPIRxBuff_t **ppstHead )
{
    if(ppstHead == NULL)
    {
        FHALT("Null Pointer reference");
        return;
    }

    sT_SPIRxBuff_t *pstTemp = *ppstHead;

    while(pstTemp != NULL && pstTemp->pstNextRxBuff != NULL)
    {
        sT_SPIRxBuff_t *next = pstTemp->pstNextRxBuff;
        free(pstTemp->puiBuffer);
        pstTemp->puiBuffer = NULL;

        free(pstTemp);
        pstTemp = next;
    }

    if(pstTemp != NULL)
    {
        if(pstTemp->puiBuffer != NULL)
        {
            free(pstTemp->puiBuffer);
            pstTemp->puiBuffer = NULL;
        }
        free(pstTemp);
    }
    pstTemp = NULL;
    *ppstHead = NULL;
}

#endif