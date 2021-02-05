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
 * @file credentials.c
 *
 * @brief Handles server and client authentifications
 *
 ******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "internals.h"

#include "error_macros.h"
#include "dm_logger.h"

#define META_TYPE_BASIC    "syncml:auth-basic"
#define META_TYPE_DIGEST   "syncml:auth-md5"
#define META_TYPE_HMAC     "syncml:auth-MAC"
#define META_TYPE_X509     "syncml:auth-X509"
#define META_TYPE_SECURID  "syncml:auth-securid"
#define META_TYPE_SAFEWORD "syncml:auth-safeword"
#define META_TYPE_DIGIPASS "syncml:auth-digipass"

#define VALUE_TYPE_BASIC    "BASIC"
#define VALUE_TYPE_DIGEST   "DIGEST"
#define VALUE_TYPE_HMAC     "HMAC"
#define VALUE_TYPE_X509     "X509"
#define VALUE_TYPE_SECURID  "SECURID"
#define VALUE_TYPE_SAFEWORD "SAFEWORD"
#define VALUE_TYPE_DIGIPASS "DIGIPASS"

#define VALUE_TYPE_BASIC_LEN    5
#define VALUE_TYPE_DIGEST_LEN   6
#define VALUE_TYPE_HMAC_LEN     4
#define VALUE_TYPE_X509_LEN     4
#define VALUE_TYPE_SECURID_LEN  7
#define VALUE_TYPE_SAFEWORD_LEN 8
#define VALUE_TYPE_DIGIPASS_LEN 8

#define EMEI_LEN           14
#define EMEI_CHECKSUM_LEN  15
#define IMEI_BUFFER_SIZE   25

#define DMACC_MO_URN    "urn:oma:mo:oma-dm-dmacc:1.0"


static char * prv_get_digest_basic(authDesc_t * authP)
{
    char * A;
    char * digest = NULL;

    A = str_cat_3(authP->name, PRV_COLUMN_STR, authP->secret);
    if (A != NULL)
    {
        digest = encode_b64_str(A);
        free(A);
    }

    return digest;
}

static char * prv_get_digest_md5(authDesc_t * authP)
{
    char * A;
    char * AD;
    char * digest = NULL;

    A = str_cat_3(authP->name, PRV_COLUMN_STR, authP->secret);
    if (A != NULL)
    {
        AD = encode_b64_md5_str(A);
        free(A);
        if (AD != NULL)
        {
            buffer_t dataBuf;

            buf_cat_str_buf(AD, authP->data, &dataBuf);
            free(AD);
            if (dataBuf.buffer)
            {
                digest = encode_b64_md5(dataBuf);
                free(dataBuf.buffer);
            }
        }
    }

    return digest;
}

SmlCredPtr_t get_credentials(authDesc_t * authP)
{
    SmlCredPtr_t credP = NULL;

    switch (authP->type)
    {
    case DMCLT_AUTH_TYPE_BASIC:
        {
            char * digest;

            digest = prv_get_digest_basic(authP);
            if (digest == NULL) goto error;

            credP = smlAllocCred();
            if (credP)
            {
                credP->meta = convert_to_meta("b64", META_TYPE_BASIC);
                set_pcdata_string(credP->data, digest);
            }
            free(digest);
        }
        break;
    case DMCLT_AUTH_TYPE_DIGEST:
        {
            char * digest;

            digest = prv_get_digest_md5(authP);
            if (digest == NULL) goto error;

            credP = smlAllocCred();
            if (credP)
            {
                credP->meta = convert_to_meta("b64", META_TYPE_DIGEST);
                set_pcdata_string(credP->data, digest);
            }
            free(digest);
        }
        break;

    default:
        // Authentification is either done at another level or not supported
        break;
    }

error:
    return credP;
}


int check_credentials(SmlCredPtr_t credP,
                      authDesc_t * authP)
{
    int status = OMADM_SYNCML_ERROR_INVALID_CREDENTIALS;
    dmclt_authType_t credType;
    char * data = smlPcdata2String(credP->data);

    if (!data) goto error; //smlPcdata2String() returns null only in case of allocation error

    credType = get_from_chal_meta(credP->meta, NULL);

    switch (authP->type)
    {
    case DMCLT_AUTH_TYPE_BASIC:
        {
            if (credType == DMCLT_AUTH_TYPE_BASIC)
            {
                char * digest = prv_get_digest_basic(authP);
                if (digest)
                {
                    if (!strcmp(digest, data))
                    {
                        status = OMADM_SYNCML_ERROR_AUTHENTICATION_ACCEPTED;
                    }
                    free(digest);
                }
            }
        }
        break;
    case DMCLT_AUTH_TYPE_DIGEST:
        {
            if (credType == DMCLT_AUTH_TYPE_DIGEST)
            {
                char * digest = prv_get_digest_md5(authP);
                if (digest)
                {
                    if (!strcmp(digest, data))
                    {
                        status = OMADM_SYNCML_ERROR_AUTHENTICATION_ACCEPTED;
                    }
                    free(digest);
                }
            }
        }
        break;

    default:
        break;
    }

    free(data);

error:
    return status;
}

SmlChalPtr_t get_challenge(authDesc_t * authP)
{
    SmlPcdataPtr_t metaP;
    SmlChalPtr_t chalP;

    switch (authP->type)
    {
    case DMCLT_AUTH_TYPE_BASIC:
        metaP = create_chal_meta(authP->type, NULL);
        break;
    case DMCLT_AUTH_TYPE_DIGEST:
        {
            int nonce;

            srand(time(0));
            nonce = rand();
            if (authP->data.buffer) free(authP->data.buffer);
            authP->data.buffer = (uint8_t *)&nonce;
            authP->data.len = 8;
            authP->data.buffer = (uint8_t *)encode_b64(authP->data);
            authP->data.len = strlen((const char *)(authP->data.buffer));
            metaP = create_chal_meta(authP->type, &(authP->data));
        }
        break;
    default:
        metaP = NULL;
        break;
    }

    if (metaP)
    {
        chalP = (SmlChalPtr_t)malloc(sizeof(SmlChal_t));
        if(chalP)
        {
            chalP->meta = metaP;
        }
        else
        {
            smlFreePcdata(metaP);
        }
    }
    else
    {
        chalP = NULL;
    }

    return chalP;
}

dmclt_authType_t auth_string_as_type(char * string)
{
    if (!strcmp(string, META_TYPE_BASIC))
        return DMCLT_AUTH_TYPE_BASIC;
    if (!strcmp(string, META_TYPE_DIGEST))
        return DMCLT_AUTH_TYPE_DIGEST;
    if (!strcmp(string, META_TYPE_HMAC))
        return DMCLT_AUTH_TYPE_HMAC;
    if (!strcmp(string, META_TYPE_X509))
        return DMCLT_AUTH_TYPE_X509;
    if (!strcmp(string, META_TYPE_SECURID))
        return DMCLT_AUTH_TYPE_SECURID;
    if (!strcmp(string, META_TYPE_SAFEWORD))
        return DMCLT_AUTH_TYPE_SAFEWORD;
    if (!strcmp(string, META_TYPE_DIGIPASS))
        return DMCLT_AUTH_TYPE_DIGIPASS;

    return DMCLT_AUTH_TYPE_UNKNOWN;
}

char * auth_type_as_string(dmclt_authType_t type)
{
    switch (type)
    {
    case DMCLT_AUTH_TYPE_HTTP_BASIC:
        return "";
    case DMCLT_AUTH_TYPE_HTTP_DIGEST:
        return "";
    case DMCLT_AUTH_TYPE_BASIC:
        return META_TYPE_BASIC;
    case DMCLT_AUTH_TYPE_DIGEST:
        return META_TYPE_DIGEST;
    case DMCLT_AUTH_TYPE_HMAC:
        return META_TYPE_HMAC;
    case DMCLT_AUTH_TYPE_X509:
        return META_TYPE_X509;
    case DMCLT_AUTH_TYPE_SECURID:
        return META_TYPE_SECURID;
    case DMCLT_AUTH_TYPE_SAFEWORD:
        return META_TYPE_SAFEWORD;
    case DMCLT_AUTH_TYPE_DIGIPASS:
        return META_TYPE_DIGIPASS;
    case DMCLT_AUTH_TYPE_TRANSPORT:
        return "";
    case DMCLT_AUTH_TYPE_UNKNOWN:
    default:
        return "";
    }
}

static dmclt_authType_t auth_value_as_type(char * string,
                                           unsigned int length)
{
    if (length == VALUE_TYPE_BASIC_LEN && !strncmp(string, VALUE_TYPE_BASIC, length))
        return DMCLT_AUTH_TYPE_BASIC;
    if (length == VALUE_TYPE_DIGEST_LEN && !strncmp(string, VALUE_TYPE_DIGEST, length))
        return DMCLT_AUTH_TYPE_DIGEST;
    if (length == VALUE_TYPE_HMAC_LEN && !strncmp(string, VALUE_TYPE_HMAC, length))
        return DMCLT_AUTH_TYPE_HMAC;
    if (length == VALUE_TYPE_X509_LEN && !strncmp(string, VALUE_TYPE_X509, length))
        return DMCLT_AUTH_TYPE_X509;
    if (length == VALUE_TYPE_SECURID_LEN && !strncmp(string, VALUE_TYPE_SECURID, length))
        return DMCLT_AUTH_TYPE_SECURID;
    if (length == VALUE_TYPE_SAFEWORD_LEN && !strncmp(string, VALUE_TYPE_SAFEWORD, length))
        return DMCLT_AUTH_TYPE_SAFEWORD;
    if (length == VALUE_TYPE_DIGIPASS_LEN && !strncmp(string, VALUE_TYPE_DIGIPASS, length))
        return DMCLT_AUTH_TYPE_DIGIPASS;

    return DMCLT_AUTH_TYPE_UNKNOWN;
}

static int prv_fill_credentials(mo_mgr_t * iMgr,
                                char * uri,
                                authDesc_t * authP)
{
    dmtree_node_t node;
    int code;

    memset(&node, 0, sizeof(dmtree_node_t));

    node.uri = str_cat_2(uri, "/AAuthType");
    if (!node.uri) return OMADM_SYNCML_ERROR_DEVICE_FULL;
    code = momgr_get_value(iMgr, &node);
    if (OMADM_SYNCML_ERROR_NONE == code)
    {
        authP->type = auth_value_as_type(node.data_buffer, node.data_size);
    }
    else if (OMADM_SYNCML_ERROR_NOT_FOUND != code)
    {
        dmtree_node_clean(&node, true);
        return code;
    }
    dmtree_node_clean(&node, true);

    node.uri = str_cat_2(uri, "/AAuthName");
    if (!node.uri) return OMADM_SYNCML_ERROR_DEVICE_FULL;
    code = momgr_get_value(iMgr, &node);
    if (OMADM_SYNCML_ERROR_NONE == code)
    {
        authP->name = dmtree_node_as_string(&node);
    }
    else if (OMADM_SYNCML_ERROR_NOT_FOUND != code)
    {
        return code;
    }
    dmtree_node_clean(&node, true);

    node.uri = str_cat_2(uri, "/AAuthSecret");
    if (!node.uri) return OMADM_SYNCML_ERROR_DEVICE_FULL;
    code = momgr_get_value(iMgr, &node);
    if (OMADM_SYNCML_ERROR_NONE == code)
    {
        authP->secret = dmtree_node_as_string(&node);
    }
    else if (OMADM_SYNCML_ERROR_NOT_FOUND != code)
    {
        return code;
    }
    dmtree_node_clean(&node, true);

    node.uri = str_cat_2(uri, "/AAuthData");
    if (!node.uri) return OMADM_SYNCML_ERROR_DEVICE_FULL;
    code = momgr_get_value(iMgr, &node);
    if (OMADM_SYNCML_ERROR_NONE == code)
    {
        authP->data.buffer = (uint8_t *)node.data_buffer;
        authP->data.len = node.data_size;
    }
    else if (OMADM_SYNCML_ERROR_NOT_FOUND != code)
    {
        dmtree_node_clean(&node, true);
        return code;
    }
    dmtree_node_clean(&node, false);

    if (NULL == authP->name)
    {
        authP->name = strdup("");
        if (NULL == authP->name) return OMADM_SYNCML_ERROR_DEVICE_FULL;
    }
    if (NULL == authP->secret)
    {
        authP->secret = strdup("");
        if (NULL == authP->secret) return OMADM_SYNCML_ERROR_DEVICE_FULL;
    }

    return code;
}

int get_server_account(mo_mgr_t * iMgr,
                       char * serverID,
                       accountDesc_t ** accountP)
{
    DMC_ERR_MANAGE;

    char * accMoUri = NULL;
    char * accountUri = NULL;
    char * uri = NULL;
    char * subUri = NULL;
    dmtree_node_t node;
    int code;

    memset(&node, 0, sizeof(dmtree_node_t));

    DMC_FAIL(momgr_find_subtree(iMgr, NULL, DMACC_MO_URN, "ServerID", serverID, &accountUri));
	DM_LOGI("abing momgr_find_subtree SERVER ID start\n");

    DMC_FAIL_NULL(*accountP, malloc(sizeof(accountDesc_t)), OMADM_SYNCML_ERROR_DEVICE_FULL);
    memset(*accountP, 0, sizeof(accountDesc_t));
    (*accountP)->dmtree_uri = accountUri;
    accountUri = NULL;

    DMC_FAIL_NULL(node.uri, strdup("./DevInfo/DevId"), OMADM_SYNCML_ERROR_DEVICE_FULL);
    DMC_FAIL(momgr_get_value(iMgr, &node));
    (*accountP)->id = dmtree_node_as_string(&node);
    dmtree_node_clean(&node, true);
	DM_LOGI("abing momgr_find_subtree Dev ID start\n");

    DMC_FAIL_NULL(uri, str_cat_2((*accountP)->dmtree_uri, "/AppAddr"), OMADM_SYNCML_ERROR_DEVICE_FULL);
    // DMC_FAIL_NULL(node.uri, str_cat_2(uri, "/AddrType"), OMADM_SYNCML_ERROR_DEVICE_FULL);
    // DMC_FAIL(momgr_get_value(iMgr, &node));
    // TODO: handle IPv4 and IPv6 cases here
	DM_LOGI("abing momgr_find_subtree app addr start\n");
    DMC_FAIL_NULL(node.uri, str_cat_2(uri, "/Addr"), OMADM_SYNCML_ERROR_DEVICE_FULL);
    DMC_FAIL(momgr_get_value(iMgr, &node));
    (*accountP)->server_uri = dmtree_node_as_string(&node);
    dmtree_node_clean(&node, true);
    free(uri);
    uri = NULL;
	DM_LOGI("abing momgr_find_subtree addr start\n");

    // TODO handle OBEX and HTTP authentification levels
    DMC_FAIL_NULL(uri, str_cat_2((*accountP)->dmtree_uri, "/AppAuth"), OMADM_SYNCML_ERROR_DEVICE_FULL);
    code = momgr_find_subtree(iMgr, uri, NULL, "AAuthLevel", "CLCRED", &subUri);
    switch (code)
    {
    case OMADM_SYNCML_ERROR_NONE:
        DMC_FAIL_NULL((*accountP)->toServerCred, malloc(sizeof(authDesc_t)), OMADM_SYNCML_ERROR_DEVICE_FULL);
        DMC_FAIL(prv_fill_credentials(iMgr, subUri, (*accountP)->toServerCred));
		DM_LOGI("abing momgr_find_subtree AAuthLevel \n");
        break;
    case OMADM_SYNCML_ERROR_NOT_FOUND:
		DM_LOGI("abing momgr_find_subtree not found \n");
        break;
    default:
        DMC_FAIL(code);
    }
	if ((*accountP)->toServerCred == NULL)
		DM_LOGI("abing momgr_find_subtree AAuthLevelllll start \n");
    if(!strcmp((*accountP)->toServerCred->secret,"" )) {

        if((*accountP)->toServerCred->name) {
             // Verizon_OTADM_Reference_Client, section 13.10 Authentication Key:
             // the IMEI is a 14 digit value. It uses as AAuthName.
             // AAuthSecret = hash of the IMEI + checksum(15 digit value,).
             // The checksum is a 1 digit value computed as per the 3GPP TS 23.003.
             int size = strlen((*accountP)->toServerCred->name);
             free((*accountP)->toServerCred->secret);
             if(size == EMEI_LEN) {
                 char imei_checksum[IMEI_BUFFER_SIZE];
                 memset(imei_checksum,0,IMEI_BUFFER_SIZE);
                 char tmp[IMEI_BUFFER_SIZE];
                 memset(tmp, 0, IMEI_BUFFER_SIZE);
                 memcpy(tmp,(*accountP)->toServerCred->name,size);
                 // calculate checksum
                 // The last number of the IMEI is a check digit.
                 // The Check Digit is calculated according to Luhn formula:
                 int sum = 0; // check sum
                 int i;
                 for (i = 0; i < EMEI_LEN; i++) {
                     int p = tmp[EMEI_LEN - 1 - i] - '0';
                     if (i % 2 == 0) {
                         p = 2 * p;
                         if (p > 9)
                             p = p - 9;
                     }
                     sum = sum + p;
                 }
                 sum = 10 - (sum % 10);
                 if (sum == 10)
                   sum = 0;
                 sprintf(imei_checksum, "%s%1d", tmp,sum);
                 // calculate AAuthSecret
                 (*accountP)->toServerCred->secret =
                         encode_md5_str(imei_checksum);
             } else if (size == EMEI_CHECKSUM_LEN) {
                 (*accountP)->toServerCred->secret =
                         encode_md5_str( (*accountP)->toServerCred->name );
             } else {
                 DM_LOGI("invalid IMEI");
                 DMC_FAIL(OMADM_SYNCML_ERROR_COMMAND_FAILED);
             }
        }
    }
    DM_LOGI("DMCORE: AAuthName = %s",(*accountP)->toServerCred->name);
    DM_LOGI("DMCORE: AAuthSecret = %s",(*accountP)->toServerCred->secret);
    free(subUri);
    subUri = NULL;

    code = momgr_find_subtree(iMgr, uri, NULL, "AAuthLevel", "SRVCRED", &subUri);
	DM_LOGI("abing momgr_find_subtree find AAuthLevel start\n");
    switch (code)
    {
    case OMADM_SYNCML_ERROR_NONE:
        DMC_FAIL_NULL((*accountP)->toClientCred, malloc(sizeof(authDesc_t)), OMADM_SYNCML_ERROR_DEVICE_FULL);
        DMC_FAIL(prv_fill_credentials(iMgr, subUri, (*accountP)->toClientCred));
        break;
    case OMADM_SYNCML_ERROR_NOT_FOUND:
        break;
    default:
        DMC_FAIL(code);
    }
    free(subUri);
    subUri = NULL;

DMC_ON_ERR:

    if (accMoUri) free(accMoUri);
    if (accountUri) free(accountUri);
    if (uri) free(uri);
    if (subUri) free(subUri);
    dmtree_node_clean(&node, true);

    return DMC_ERR;
}

void store_nonce(mo_mgr_t * iMgr,
                 const accountDesc_t * accountP,
                 bool server)
{
    char * subUri = NULL;
    char * searchUri;

    searchUri = str_cat_2(accountP->dmtree_uri, "/AppAuth");
    if (searchUri == NULL) return;

    if (OMADM_SYNCML_ERROR_NONE == momgr_find_subtree(iMgr, searchUri, NULL, "AAuthLevel", server?"CLCRED":"SRVCRED", &subUri))
    {
        dmtree_node_t node;

        memset(&node, 0, sizeof(dmtree_node_t));
        node.uri = str_cat_2(subUri, "/AAuthData");
        if (node.uri)
        {
            node.data_buffer = (char *)(server?accountP->toServerCred->data.buffer:accountP->toClientCred->data.buffer);
            node.data_size = server?accountP->toServerCred->data.len:accountP->toClientCred->data.len;
            momgr_set_value(iMgr, &node);
            free(node.uri);
        }
        free(subUri);
    }
    free(searchUri);
}
