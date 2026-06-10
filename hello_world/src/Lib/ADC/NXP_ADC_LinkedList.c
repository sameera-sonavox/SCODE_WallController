#include "NXP_ADC_LinkedList.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../GenericMacro.h"

sT_ADC_CommandConfig_t* pstCreate_ADCCommandConfigNode(sT_ADC_CMDData_t *pstCMDData)
{
    if(pstCMDData == NULL)
    {
        FHALT("ADC Command Data pointer is NULL. Cannot create command config node.\n");
        return NULL;
    }

    sT_ADC_CommandConfig_t *pstNode = (sT_ADC_CommandConfig_t *)malloc(sizeof(sT_ADC_CommandConfig_t));
    if(pstNode == NULL)
    {
        FHALT("Memory allocation failed for ADC Command Config Node.\n");
        return NULL;
    }

    memset(pstNode, 0, sizeof(sT_ADC_CommandConfig_t)); // Clear the allocated memory
    pstNode->stTCMDData = *pstCMDData; // Copy the command data
    pstNode->pstNextCommandConfig = NULL; // Initialize next pointer to NULL
    return pstNode;
}

bool bInsertCommand_AtBeginning(sT_ADC_CommandConfig_t **ppstHead, sT_ADC_CMDData_t *pstCMDData)
{
    if(ppstHead == NULL)
    {
        FHALT("Pointer to head of list is NULL. Cannot insert node.\n");
        return false;
    }
    if(pstCMDData == NULL)
    {
        FHALT("ADC Command Data pointer is NULL. Cannot insert node.\n");
        return false;
    }

    sT_ADC_CommandConfig_t *pstNewNode = pstCreate_ADCCommandConfigNode(pstCMDData);
    if(pstNewNode == NULL)
    {
        FHALT("Failed to create new ADC Command Config Node. Cannot insert node.\n");
        return false;
    }

    pstNewNode->pstNextCommandConfig = *ppstHead; // Point new node to current head
    *ppstHead = pstNewNode; // Update head to new node
    return true;
}

bool bInsertCommand_AtEnd(sT_ADC_CommandConfig_t **ppstHead, sT_ADC_CMDData_t *pstCMDData)
{
    if(ppstHead == NULL)
    {
        FHALT("Pointer to head of list is NULL. Cannot insert node.\n");
        return false;
    }
    if(pstCMDData == NULL)
    {
        FHALT("ADC Command Data pointer is NULL. Cannot insert node.\n");
        return false;
    }

    sT_ADC_CommandConfig_t *pstNewNode = pstCreate_ADCCommandConfigNode(pstCMDData);
    if(pstNewNode == NULL)
    {
        FHALT("Failed to create new ADC Command Config Node. Cannot insert node.\n");
        return false;
    }
    
    sT_ADC_CommandConfig_t *pstCurrent = *ppstHead;

    while(pstCurrent != NULL && pstCurrent->pstNextCommandConfig != NULL)
    {
        pstCurrent = pstCurrent->pstNextCommandConfig; // Traverse to the end of the list
    }
    
    if(pstCurrent == NULL)
    {
        *ppstHead = pstNewNode;
    }
    else
    {
        pstCurrent->pstNextCommandConfig = pstNewNode; // Link the new node at the end
    }

    return true;
}

void vRelease_CMDBuffers(sT_ADC_CommandConfig_t **ppstHead)
{
    if((ppstHead == NULL) || (*ppstHead == NULL))
        return;

    sT_ADC_CommandConfig_t *pstCurrent = *ppstHead;

    while(pstCurrent != NULL)
    {
        sT_ADC_CommandConfig_t *pstNext = pstCurrent->pstNextCommandConfig;
        free(pstCurrent);
        pstCurrent = pstNext;
    }

    *ppstHead = NULL;
}

sT_ADC_CommandConfig_t *pstGetCommandConfig(eADC_Command_t eCommandId, sT_ADC_CommandConfig_t *pstHead)
{
    if(pstHead == NULL)
    {
        FHALT("Null reference pointer");
        return NULL;
    }
    if(eCommandId < 0 || eCommandId >= eNUMBER_OF_ADC_COMMANDs)
    {
        FHALT("Invalid ADC CMD : %d", eCommandId);
        return NULL;        
    }

    sT_ADC_CommandConfig_t *temp = pstHead;
    while(temp != NULL)
    {
        if(temp->stTCMDData.eCommandId == eCommandId)
            return temp;
        temp = temp->pstNextCommandConfig;
    }
    return temp;
}