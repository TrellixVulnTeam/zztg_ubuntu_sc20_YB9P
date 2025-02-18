/*
 * Copyright (C) 2016 Verizon. All Rights Reserved.
 */

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "dm_logger.h"
#include "mo_omadmtree.h"
#include "mo_error.h"
#include "pal.h"
#include "plugin_utils.h"

#define PRV_BASE_URI "./ManagedObjects/DCMO"
#define PRV_BASE_ACL "Get=*"
#define PRV_URN      "urn:oma:mo:oma-dcmo:1.0"

//#define PAL_VOLTE_CTRL

static void* palHandle = NULL;

static plugin_tree_node_t gNodes[] =
{
#ifdef PAL_VOLTE_CTRL
    {PRV_BASE_URI, PRV_URN,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/VocoderMode", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VLT", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VLT/Setting", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VLT/Setting/Enabled", NULL,
        OMADM_LEAF_FORMAT_BOOL, OMADM_LEAF_TYPE,
        "Get=*", "",
        "pal_mobile_vlt_state_get", NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VLT/Setting/Operations", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VLT/Setting/Operations/Enable", NULL,
        OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_vlt_enable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VLT/Setting/Operations/Disable", NULL,
        OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_vlt_disable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/EAB", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/EAB/Setting", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/EAB/Setting/Enabled",
        NULL,OMADM_LEAF_FORMAT_BOOL, OMADM_LEAF_TYPE,
        "Get=*", "",
        "pal_mobile_eab_state_get",
        NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/EAB/Setting/Operations", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/EAB/Setting/Operations/Enable", NULL,
        OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_eab_enable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/EAB/Setting/Operations/Disable",
        NULL,OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_eab_disable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/LVC", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/LVC/Setting", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/LVC/Setting/Enabled",
        NULL,OMADM_LEAF_FORMAT_BOOL, OMADM_LEAF_TYPE,
        "Get=*", "",
        "pal_mobile_lvc_state_get",
        NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/LVC/Setting/Operations", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/LVC/Setting/Operations/Enable", NULL,
        OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_lvc_enable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/LVC/Setting/Operations/Disable",
        NULL,OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_lvc_disable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VCE", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VCE/Setting", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VCE/Setting/Enabled",
        NULL,OMADM_LEAF_FORMAT_BOOL, OMADM_LEAF_TYPE,
        "Get=*", "",
        "pal_mobile_vce_state_get",
        NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VCE/Setting/Operations", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VCE/Setting/Operations/Enable", NULL,
        OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_vce_enable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VCE/Setting/Operations/Disable",
        NULL,OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_vce_disable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VWF", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VWF/Setting", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VWF/Setting/Enabled",
        NULL,OMADM_LEAF_FORMAT_BOOL, OMADM_LEAF_TYPE,
        "Get=*", "",
        "pal_mobile_vwf_state_get",
        NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VWF/Setting/Operations", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VWF/Setting/Operations/Enable", NULL,
        OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_vwf_enable"},
    {PRV_BASE_URI"/VOLTE/FeatureStatus/VWF/Setting/Operations/Disable",
        NULL,OMADM_LEAF_FORMAT_CHAR, OMADM_LEAF_TYPE,
        "Exec=*", "",
        NULL, NULL,
        "pal_mobile_vwf_disable"},
    {PRV_BASE_URI"/VoWIFI", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
    {PRV_BASE_URI"/LTE_Band", NULL,
        OMADM_NODE_FORMAT, OMADM_NODE_TYPE,
        "Get=*", "",
        NULL, NULL, NULL},
#endif

     {"NULL", NULL, NULL, OMADM_NOT_EXIST, NULL, NULL, NULL, NULL, NULL},
};

static int prv_init_fn(void **oData)
{
    palHandle = dlopen(PAL_INSTALL_DIR "/" PAL_LIB_NAME, RTLD_LAZY);

    if (!palHandle){
        DM_LOGE( "palHandle not initialised %s", dlerror());
        return MO_ERROR_COMMAND_FAILED;
        /**
         * \todo !!! function description and actual behaviour MUST be consistent.
         * according to "omadmtree_mo.h" this function shall return:
         * "SyncML error code"
         * but now we a returning something different.
         */
    }

    *oData = gNodes;
    return MO_ERROR_NONE;
}

static void prv_close_fn(void *iData)
{
    (void)iData;

    if (palHandle) {
        // Not sure if we can do anything with error code provided by dlclose(),
        // so just print error and exit.
        int res = dlclose(palHandle);
        palHandle = NULL;
        if (0 != res)
           DM_LOGE("%s: palHandle not closed %s", __FILE__, dlerror());
    }
}

static int prv_get_fn(dmtree_node_t * nodeP,
                     void * iData)
{
    if (!palHandle){
        DM_LOGE( "ERROR! PAL isn't loaded");
        return MO_ERROR_COMMAND_FAILED; /// seems that we can't do anything if PAL not loaded
    }

    int i = 0;
    int err = MO_ERROR_COMMAND_FAILED;
    int value = 0;

    int int_buffer = 0;

    int (*getLeafFunc_int)(int *);

    plugin_tree_node_t * nodes = (plugin_tree_node_t *)iData;

    if(nodeP) {
        i = prv_find_node(nodes, nodeP->uri);
        if (i == -1) {
            return MO_ERROR_NOT_FOUND;
        }

        if( !(palHandle && nodes[i].getLeafFuncName)) {
            char * child_list = NULL;
            child_list = get_child_list(nodes, nodeP->uri);
            if (child_list) {
                nodeP->data_buffer = strdup(child_list);
            } else {
                nodeP->data_buffer = strdup(nodes[i].value);
            }

            if (nodeP->data_buffer) {
                nodeP->data_size = strlen(nodeP->data_buffer);
                if (nodeP->format) free(nodeP->format);
                nodeP->format = strdup(nodes[i].format);
                if(nodeP->format){
                    if (nodeP->type) free(nodeP->type);
                    nodeP->type = strdup(nodes[i].type);
                    if(nodeP->type != NULL){
                        err = MO_ERROR_NONE;
                    } else err = MO_ERROR_DEVICE_FULL;
                } else {
                    err = MO_ERROR_DEVICE_FULL;
                }
            }
        } else {
            if (nodeP->format) free(nodeP->format);
            nodeP->format = strdup(nodes[i].format);
            if(!nodeP->format) {
                err = MO_ERROR_DEVICE_FULL;
                return err;
            }

            if (nodeP->type) free(nodeP->type);
            nodeP->type = strdup(nodes[i].type);
            if(!nodeP->type) {
                err = MO_ERROR_DEVICE_FULL;
                return err;
            }
            err = MO_ERROR_NONE;

            //for OMADM_LEAF_FORMAT_BOOL
            getLeafFunc_int = dlsym(palHandle, nodes[i].getLeafFuncName);
            if (getLeafFunc_int) {
                value = getLeafFunc_int(&int_buffer);
                DM_LOGI("DCMO_get_code_from_pal = %d", value);
                if (value == MO_ERROR_NONE) {
                    free(nodeP->data_buffer);
                    if (int_buffer == 1){
                        nodeP->data_buffer = strdup("True");
                    } else nodeP->data_buffer = strdup("False");
                    if(nodeP->data_buffer) {
                        nodeP->data_size = strlen(nodeP->data_buffer);
                        free(nodeP->format);
                        nodeP->format = strdup(nodes[i].format);
                        if(nodeP->format != NULL){
                            if (nodeP->type) free(nodeP->type);
                            nodeP->type = strdup(nodes[i].type);
                            if(!nodeP->type) {
                                err = MO_ERROR_DEVICE_FULL;
                            } else err = MO_ERROR_NONE;
                        } else err = MO_ERROR_DEVICE_FULL;
                    }
                }
            } else {
                err = MO_ERROR_COMMAND_NOT_IMPLEMENTED;
            }
                if (value != MO_ERROR_NONE) {
                switch(value){
                case RESULT_MEMORY_ERROR:
                    err = MO_ERROR_DEVICE_FULL;
                    break;
                case RESULT_ERROR_INVALID_STATE:
                    err = MO_ERROR_OPTIONAL_FEATURE_NOT_SUPPORTED;
                    break;
                default:
                    err = MO_ERROR_COMMAND_FAILED;
                }
            }
        }
    }
    return err;
}

static int prv_set_fn(const dmtree_node_t * nodeP,
                     void * iData)
{
    return MO_ERROR_NOT_ALLOWED;
}

static int prv_exec_fn (const char *iURI,
                       const char *cmdData,
                       const char *correlator,
                       void *iData)
{
    DM_LOGI("DCMO prv_exec_fn {\n");
    if (!palHandle){
       DM_LOGD( "ERROR! PAL isn't loaded. \n");
        return MO_ERROR_COMMAND_FAILED; /// seems that we can't do anything if PAL not loaded
    }
    if(iData == NULL) {
        DM_LOGD(" ERROR: invalid paraneters\n");
        return MO_ERROR_DEVICE_FULL;
    }

    int i = 0;
    int err = MO_ERROR_NONE;
    int value = RESULT_SUCCESS;
    int (*execLeafFunc)();
    plugin_tree_node_t * nodes = (plugin_tree_node_t *)iData;

    if(iURI) {
        i = prv_find_node(nodes, iURI);
        if (i == -1) {
            return MO_ERROR_NOT_FOUND;
        }
                if(palHandle && nodes[i].execLeafFuncName) {
                    execLeafFunc = dlsym(palHandle, nodes[i].execLeafFuncName);
                    if (execLeafFunc) {
                        value = execLeafFunc();
                        DM_LOGI("DCMO_exec_code_from_pal = %d", value);
                    } else return MO_ERROR_OPTIONAL_FEATURE_NOT_SUPPORTED;
                } else return MO_ERROR_NOT_ALLOWED;
            }

    if (value != MO_ERROR_NONE) {
        switch(value){
        case RESULT_MEMORY_ERROR:
            err = MO_ERROR_DEVICE_FULL;
            break;
        case RESULT_ERROR_INVALID_STATE:
            err = MO_ERROR_OPTIONAL_FEATURE_NOT_SUPPORTED;
            break;
        case RESULT_ERROR_UNDEFINED:
            err = MO_ERROR_COMMAND_FAILED;
        break;
        default:
            err = MO_ERROR_COMMAND_FAILED;
        }
    }
    return err;
}

omadm_mo_interface_t * omadm_get_mo_interface()
{
    omadm_mo_interface_t *retVal = NULL;

    retVal = malloc(sizeof(*retVal));
    if (retVal) {
        memset(retVal, 0, sizeof(*retVal));
        retVal->base_uri = strdup(PRV_BASE_URI);
        retVal->isNodeFunc = prv_mo_is_node;
        retVal->initFunc = prv_init_fn;
        retVal->closeFunc = prv_close_fn;
        retVal->findURNFunc = prv_find_urn;
        retVal->getFunc = prv_get_fn;
        retVal->setFunc = prv_set_fn;
        retVal->getACLFunc = prv_get_acl_fn;
        retVal->execFunc = prv_exec_fn;
    }

    return retVal;
}
