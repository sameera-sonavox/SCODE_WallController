
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

#endif