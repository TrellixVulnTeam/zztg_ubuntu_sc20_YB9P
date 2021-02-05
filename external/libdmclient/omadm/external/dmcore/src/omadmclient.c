/*
 * Copyright (C) 2016 Verizon. All Rights Reserved.
 */

/*
 * libdmclient
 *
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * David Navarro <david.navarro@intel.com>
 *
 */

/*!
 * @file omadmclient.c
 *
 * @brief Main file for the omadmclient library.  Contains code for APIs.
 *
 ******************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "internals.h"
#include "dm_logger.h"


#define PRV_CHECK_SML_CALL(func)    if (SML_ERR_OK != (func)) return DMCLT_ERR_INTERNAL
#define PRV_MAX_MESSAGE_SIZE        "16384"

static void prvCreatePacket1(internals_t * internP)
{
    // this is the beginning of the session
    SmlAlertPtr_t   alertP;

    alertP = smlAllocAlert();
    if (alertP)
    {
        SmlReplacePtr_t replaceP;

        switch(internP->state)
        {
        case STATE_CLIENT_INIT:
            alertP->data = smlString2Pcdata("1201");
            break;
        case STATE_SERVER_INIT:
            alertP->data = smlString2Pcdata("1200");
            break;
        case STATE_ABORT:
            internP->state = STATE_IN_SESSION;
            return;
        default:
            smlFreeProtoElement((basicElement_t *)alertP);
            return;
        }
        smlFreeItemList(alertP->itemList);
        alertP->itemList = NULL;

        replaceP = get_device_info(internP);
        if (replaceP)
        {
            add_element(internP, (basicElement_t *)alertP);
            add_element(internP, (basicElement_t *)replaceP);
            internP->state = STATE_IN_SESSION;
        }
        else
        {
            smlFreeProtoElement((basicElement_t *)alertP);
        }
    }
}

static SmlSyncHdrPtr_t prvGetHeader(internals_t * internP)
{
    SmlSyncHdrPtr_t headerP;

    headerP = smlAllocSyncHdr();
    if (headerP)
    {
        set_pcdata_string(headerP->version, "1.2");
        set_pcdata_string(headerP->proto, "DM/1.2");
        set_pcdata_hex(headerP->sessionID, internP->session_id);
        set_pcdata_int(headerP->msgID, internP->message_id);
        set_pcdata_string(headerP->target->locURI, internP->account->server_uri);
        set_pcdata_string(headerP->source->locURI, internP->account->id);
        if (OMADM_SYNCML_ERROR_AUTHENTICATION_ACCEPTED != internP->clt_auth
         && OMADM_SYNCML_ERROR_SUCCESS != internP->clt_auth)
        {
            headerP->cred = get_credentials(internP->account->toServerCred);
        }
        headerP->meta = smlAllocPcdata();
        if (headerP->meta)
        {
            SmlMetInfMetInfPtr_t metInfP;

            metInfP = smlAllocMetInfMetInf();
            if (metInfP)
            {
                metInfP->maxmsgsize = smlString2Pcdata(PRV_MAX_MESSAGE_SIZE);
                headerP->meta->contentType = SML_PCDATA_EXTENSION;
                headerP->meta->extension = SML_EXT_METINF;
                headerP->meta->length = 0;
                headerP->meta->content = metInfP;
            }
            else
            {
                smlFreePcdata(headerP->meta);
                headerP->meta = NULL;
            }
        }

    }

    return headerP;
}

static int prvComposeMessage(internals_t * internP)
{
    int toSend = -1;
    Ret_t result;
    SmlSyncHdrPtr_t syncHdrP;
    elemCell_t * cell;

    internP->message_id++;
    internP->command_id = 1;

    syncHdrP = prvGetHeader(internP);

    result = smlStartMessageExt(internP->smlH, syncHdrP, SML_VERS_1_2);
	

    smlFreeSyncHdr(syncHdrP);

    cell = internP->elem_first;
    while(cell && result == SML_ERR_OK)
    {
        set_pcdata_int(cell->element->cmdID, internP->command_id++);
        cell->msg_id = internP->message_id;

        switch (cell->element->elementType)
        {
        case SML_PE_ALERT:
            result = smlAlertCmd(internP->smlH, (SmlAlertPtr_t)(cell->element));
            toSend = 1;
            break;

        case SML_PE_REPLACE:
            result = smlReplaceCmd(internP->smlH, (SmlReplacePtr_t)(cell->element));
            toSend = 1;
            break;

        case SML_PE_RESULTS:
            result = smlResultsCmd(internP->smlH, (SmlResultsPtr_t)(cell->element));
            toSend = 1;
            break;

        case SML_PE_STATUS:
            result = smlStatusCmd(internP->smlH, (SmlStatusPtr_t)(cell->element));
            toSend++;
            break;

        default:
            // should not happen
            break;
        }

        cell = cell->next;
    }

    if (result != SML_ERR_OK)
    {
        return DMCLT_ERR_INTERNAL;
    }

    PRV_CHECK_SML_CALL(smlEndMessage(internP->smlH, SmlFinal_f));

    refresh_elements(internP);

    if (toSend <= 0)
    {
        return DMCLT_ERR_END;
    }

    return DMCLT_ERR_NONE;
}

static void prvFreeAuth(authDesc_t * authP)
{
    if (!authP) return;

    if (authP->name) free(authP->name);
    if (authP->secret) free(authP->secret);
    if (authP->data.buffer) free(authP->data.buffer);

    free(authP);
}

dmclt_session * omadmclient_session_init(bool useWbxml)
{
    internals_t *        internP;
    SmlInstanceOptions_t options;

    internP = (internals_t *)malloc(sizeof(internals_t));
    if (!internP)
    {
        return NULL;
    }

    memset(internP, 0, sizeof(internals_t));

    memset(&options, 0, sizeof(options));
    if (useWbxml)
    {
        options.encoding= SML_WBXML;
    }
    else
    {
        options.encoding= SML_XML;
    }
    options.workspaceSize= PRV_MAX_WORKSPACE_SIZE;

    internP->sml_callbacks = get_callbacks();

    if (SML_ERR_OK != smlInitInstance(internP->sml_callbacks, &options, NULL, &(internP->smlH)))
    {
        omadmclient_session_close((void**)internP);
        free(internP);
        internP = NULL;
    }

    if (OMADM_SYNCML_ERROR_NONE != dmtree_open(&(internP->dmtreeH)))
    {
        omadmclient_session_close((void**)internP);
        free(internP);
        internP = NULL;
    }
 
    return (dmclt_session)internP;
}

dmclt_err_t omadmclient_set_UI_callback(dmclt_session sessionH,
                                        dmclt_callback_t UICallbacksP,
                                        void * userData)
{    internals_t * internP = (internals_t *)sessionH;

    if (internP == NULL)
    {
        return DMCLT_ERR_USAGE;
    }

    internP->alert_cb = UICallbacksP;
    internP->cb_data = userData;

    return DMCLT_ERR_NONE;
}

dmclt_err_t omadmclient_session_add_mo(dmclt_session sessionH,
                                       omadm_mo_interface_t * moP)
{
    internals_t * internP = (internals_t *)sessionH;

    if (internP == NULL || moP == NULL)
    {
        return DMCLT_ERR_USAGE;
    }

    if (OMADM_SYNCML_ERROR_NONE != momgr_add_plugin(internP->dmtreeH->MOs, moP))
    {
        return DMCLT_ERR_INTERNAL;
    }

    return DMCLT_ERR_NONE;
}

dmclt_err_t omadmclient_getUriList(dmclt_session sessionH,
                                   char * urn,
                                   char *** uriListP)
{
    internals_t * internP = (internals_t *)sessionH;

    if (internP == NULL || urn == NULL || uriListP == NULL)
    {
        return DMCLT_ERR_USAGE;
    }

    if (OMADM_SYNCML_ERROR_NONE != momgr_list_uri(internP->dmtreeH->MOs, urn, uriListP))
    {
        return DMCLT_ERR_INTERNAL;
    }

    return DMCLT_ERR_NONE;
}

dmclt_err_t omadmclient_session_start(dmclt_session sessionH,
                                      char * serverID,
                                      int sessionID)
{
    internals_t * internP = (internals_t *)sessionH;

    if (internP == NULL || serverID == NULL)
    {
        return DMCLT_ERR_USAGE;
    }

    if (OMADM_SYNCML_ERROR_NONE != dmtree_setServer(internP->dmtreeH, serverID))
    {
        return DMCLT_ERR_INTERNAL;
    }
    
    if (OMADM_SYNCML_ERROR_NONE != momgr_check_mandatory_mo(internP->dmtreeH->MOs))
    {
    	return DMCLT_ERR_USAGE;
    }

    if (OMADM_SYNCML_ERROR_NONE != get_server_account(internP->dmtreeH->MOs, serverID, &(internP->account)))
    {
        return DMCLT_ERR_INTERNAL;
    }

    if (NULL == internP->account->toClientCred)
    {
        internP->srv_auth = OMADM_SYNCML_ERROR_AUTHENTICATION_ACCEPTED;
    }
    if (NULL == internP->account->toServerCred)
    {
        internP->clt_auth = OMADM_SYNCML_ERROR_AUTHENTICATION_ACCEPTED;
    }

    internP->session_id = sessionID;
    internP->message_id = 0;
    internP->state = STATE_CLIENT_INIT;

    return DMCLT_ERR_NONE;
}

dmclt_err_t omadmclient_session_start_on_alert(dmclt_session sessionH,
                                               uint8_t * pkg0,
                                               int pkg0_len,
                                               char * flags,
                                               int * body_offset)
{
    internals_t * internP = (internals_t *)sessionH;
    char * serverID;
    int sessionID;
    dmclt_err_t err;
    buffer_t package;

    if (internP == NULL || pkg0 == NULL || pkg0_len <= 0)
    {
        return DMCLT_ERR_USAGE;
    }

    package.buffer = pkg0;
    package.len = pkg0_len;

    if (OMADM_SYNCML_ERROR_NONE != decode_package_0(package, &serverID, &sessionID, flags, body_offset))
    {
        return DMCLT_ERR_USAGE;
    }

    // We start the session now since we need to access the DM tree to validate the received package0.
    err = omadmclient_session_start(sessionH,
                                    serverID,
                                    sessionID);

    if (DMCLT_ERR_NONE == err)
    {
        if (OMADM_SYNCML_ERROR_NONE != validate_package_0(internP, package))
        {
            err = DMCLT_ERR_USAGE;
        }
    }

    internP->state = STATE_SERVER_INIT;
        
    return err;
}

void omadmclient_session_close(dmclt_session sessionH)
{
    internals_t * internP = (internals_t *)sessionH;

    if(!internP)
    {
        return;
    }

    if (internP->dmtreeH)
    {
        dmtree_close(internP->dmtreeH);
    }
    if (internP->smlH)
    {
        smlTerminateInstance(internP->smlH);
    }
    if (internP->sml_callbacks)
    {
    	free(internP->sml_callbacks);
    }
    if (internP->elem_first)
    {
        free_element_list(internP->elem_first);
    }
    if (internP->old_elem)
    {
        free_element_list(internP->old_elem);
    }
    if (internP->reply_ref)
    {
        free(internP->reply_ref);
    }
    if (internP->account)
    {
        if (internP->account->id) free(internP->account->id);
        if (internP->account->server_uri) free(internP->account->server_uri);
        if (internP->account->dmtree_uri) free(internP->account->dmtree_uri);
        prvFreeAuth(internP->account->toServerCred);
        prvFreeAuth(internP->account->toClientCred);
        free(internP->account);
    }
    memset(internP, 0, sizeof(internals_t));
}

dmclt_err_t omadmclient_get_next_packet(dmclt_session sessionH,
                                        dmclt_buffer_t * packetP)
{
    internals_t * internP = (internals_t *)sessionH;
    dmclt_err_t status;

    if (!internP || !packetP || !(internP->account))
    {
        return DMCLT_ERR_USAGE;
    }

    if (STATE_IN_SESSION != internP->state)
    {
        prvCreatePacket1(internP);
    }

    status = prvComposeMessage(internP);
	//DM_LOGI(" omadmclient_get_next_packet status = %d\n", status);

    memset(packetP, 0, sizeof(dmclt_buffer_t));
    if (status == DMCLT_ERR_NONE)
    {
        MemPtr_t dataP;
        MemSize_t size;

        PRV_CHECK_SML_CALL(smlLockReadBuffer(internP->smlH, &dataP, &size));

        packetP->data = (unsigned char *)malloc(size + 1);
        if (!packetP->data) return DMCLT_ERR_MEMORY;
        memset(packetP->data, 0, (size + 1));
        memcpy(packetP->data, dataP, size);

        packetP->length = size;
        packetP->uri = strdup(internP->account->server_uri);
        PRV_CHECK_SML_CALL(smlUnlockReadBuffer(internP->smlH, size));

        // export authentication data for non OMA-DM level authentication types
        packetP->auth_type = internP->account->toServerCred->type;
        if (0 != internP->account->toServerCred->data.len)
        {
            switch (packetP->auth_type)
            {
            case DMCLT_AUTH_TYPE_BASIC:
            case DMCLT_AUTH_TYPE_DIGEST:
                // do nothing, authentication is handled in the DM packet
                break;
            default:
                packetP->auth_data = (unsigned char*)malloc(internP->account->toServerCred->data.len);
                if (NULL == packetP->auth_data)
                {
                    omadmclient_clean_buffer(packetP);
                    status = DMCLT_ERR_MEMORY;
                }
                else
                {
                    memcpy(packetP->auth_data, internP->account->toServerCred->data.buffer, internP->account->toServerCred->data.len);
                    packetP->auth_data_length = internP->account->toServerCred->data.len;
                }
            }
        }
    }

    return status;
}

dmclt_err_t omadmclient_process_reply(dmclt_session sessionH,
                                      dmclt_buffer_t * packetP)
{
    internals_t * internP = (internals_t *)sessionH;
    MemPtr_t dataP;
    MemSize_t size;

    if (!internP || !packetP)
    {
        return DMCLT_ERR_USAGE;
    }

    PRV_CHECK_SML_CALL(smlLockWriteBuffer(internP->smlH, &dataP, &size));
    if (size >= packetP->length)
    {
        memcpy(dataP, packetP->data, packetP->length);
    }
    PRV_CHECK_SML_CALL(smlUnlockWriteBuffer(internP->smlH, packetP->length));

    PRV_CHECK_SML_CALL(smlSetUserData(internP->smlH, internP));

    PRV_CHECK_SML_CALL(smlProcessData(internP->smlH, SML_ALL_COMMANDS));

    return DMCLT_ERR_NONE;
}

dmclt_err_t omadmclient_add_alert(dmclt_session sessionH, char* alert_code)
{
    internals_t * internP = (internals_t *)sessionH;
    int size = 0;
    SmlAlertPtr_t alertP;
    if(!internP)
        return DMCLT_ERR_USAGE;
    alertP = smlAllocAlert();
    if (NULL == alertP)
        return DMCLT_ERR_MEMORY;
    alertP->data = smlString2Pcdata(alert_code);
    if (NULL == alertP->data) {
        smlFreeAlert(alertP);
        return DMCLT_ERR_MEMORY;
    }
    smlFreeItemList(alertP->itemList);
    alertP->itemList = NULL;

    add_element(internP, (basicElement_t *)alertP);
    size = strlen(alert_code);
    if( !memcmp(alert_code,"1223",size) ) { // session abort
        internP->state = STATE_ABORT;
    } else if( !memcmp(alert_code,"1222",size) ) { // asking for more messages
        internP->state = STATE_MORE_MSG;
    }
    return DMCLT_ERR_NONE;
}

dmclt_err_t omadmclient_add_generic_alert(dmclt_session sessionH,
                                          char * correlator,
                                          dmclt_item_t * itemP)
{
    internals_t * internP = (internals_t *)sessionH;
    SmlAlertPtr_t alertP;

    if (!internP || !itemP || !itemP->type || !itemP->format || !itemP->data)
    {
        return DMCLT_ERR_USAGE;
    }

    alertP = smlAllocAlert();
    if (NULL == alertP)
    {
        return DMCLT_ERR_MEMORY;
    }
    
    alertP->data = smlString2Pcdata("1226");
    if (NULL == alertP->data)
    {
        smlFreeAlert(alertP);
        return DMCLT_ERR_MEMORY;
    }

    if (correlator)
    {
        alertP->correlator = smlString2Pcdata(correlator);
        if (NULL == alertP->correlator)
        {
            smlFreeAlert(alertP);
            return DMCLT_ERR_MEMORY;
        }
    }

    if (itemP->source)
    {
        alertP->itemList->item->source = smlAllocSource();
        if (NULL == alertP->itemList->item->source)
        {
            smlFreeAlert(alertP);
            return DMCLT_ERR_MEMORY;
        }
        alertP->itemList->item->source->locURI = smlString2Pcdata(itemP->source);
    }
    
    if (itemP->target)
    {
        alertP->itemList->item->target = smlAllocTarget();
        if (NULL == alertP->itemList->item->target)
        {
            smlFreeAlert(alertP);
            return DMCLT_ERR_MEMORY;
        }
        alertP->itemList->item->target->locURI = smlString2Pcdata(itemP->target);
    }

    alertP->itemList->item->meta = convert_to_meta(itemP->format, itemP->type);
    alertP->itemList->item->data = smlString2Pcdata(itemP->data);
    if (NULL == alertP->itemList->item->meta || NULL == alertP->itemList->item->data)
    {
        smlFreeAlert(alertP);
        return DMCLT_ERR_MEMORY;
    }

    add_element(internP, (basicElement_t *)alertP);

    return DMCLT_ERR_NONE;
}

void omadmclient_clean_buffer(dmclt_buffer_t * packetP)
{
    if (packetP)
    {
        if (packetP->uri)
        {
            free(packetP->uri);
        }
        if (packetP->data)
        {
            free(packetP->data);
        }
        if (packetP->auth_data)
        {
            free(packetP->auth_data);
        }
        memset(packetP, 0, sizeof(dmclt_buffer_t));
    }
}
