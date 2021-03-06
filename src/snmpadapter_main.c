/**
 * Copyright 2017 Comcast Cable Communications Management, LLC
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
 */
/*
 * snmpadapter_main.c
 *
 *  Created on: Apr 20, 2017
 *      Author: Murugan Viswanathan
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <string.h>
#include <math.h>
#include <pthread.h>

#include <libparodus.h>
#include <wdmp-c.h>
#include "snmpadapter_main.h"
#include "snmpadapter_common.h"
#include "snmpadapter_parser.h"

/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/

#define COMCAST_COMMUNITY_TOKEN "hDaFHJG7"
#define COMCAST_COMMUNITY_CMD "-c"
#define SNMPADAPTER_SUPPORTED_VERSION "-v2c"

#ifdef SNMPADAPTER_TEST_STANDALONE /* for unit testing on standalone machine*/
#define TARGET_AGENT "10.255.244.168"
#else
#define TARGET_AGENT "127.0.0.1"
#endif

static char parodus_url[URL_SIZE] = { '\0' };
static char client_url[URL_SIZE] = { '\0' };

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
libpd_instance_t g_current_instance;

/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/
const char *rdk_logger_module_fetch(void)
{
    return "LOG.RDK.PARODUS2SNMP";
}

/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/

/*
 * get_parodus_url
 */
#ifndef SNMPADAPTER_TEST_STANDALONE
static
#endif
void get_parodus_url(char *parodus_url, char *client_url)
{

    FILE *fp = fopen(DEVICE_PROPS_FILE, "r");

    if (NULL != fp)
    {
        char str[255] = { '\0' };
        while (fscanf(fp, "%s", str) != EOF)
        {
            char *value = NULL;

            if ((value = strstr(str, "PARODUS_URL=")))
            {
                value = value + strlen("PARODUS_URL=");
                strncpy(parodus_url, value, (strlen(str) - strlen("PARODUS_URL=")));
            }

            if ((value = strstr(str, "PARODUS2SNMP_CLIENT_URL=")))
            {
                value = value + strlen("PARODUS2SNMP_CLIENT_URL=");
                strncpy(client_url, value, (strlen(str) - strlen("PARODUS2SNMP_CLIENT_URL=")));
            }

        }
	fclose(fp);
    }
    else
    {
        SnmpAdapterPrint("[PARODUS2SNMP] Failed to open \"%s\" file!!\n", DEVICE_PROPS_FILE);
        return;
    }

    if (0 == parodus_url[0])
    {
        SnmpAdapterPrint("[PARODUS2SNMP] Error : parodus_url is not present in %s file! \n", DEVICE_PROPS_FILE);
        return;
    }

    if (0 == client_url[0])
    {
        SnmpAdapterPrint("[PARODUS2SNMP] Error : client_url is not present in %s file! \n", DEVICE_PROPS_FILE);
        return;
    }

    SnmpAdapterPrint("[PARODUS2SNMP] Parodus URL formed is : %s\n", parodus_url);
    SnmpAdapterPrint("[PARODUS2SNMP] Client URL formed is  : %s\n", client_url);

    return;
}

/*
 * connect_to_parodus
 */
static void connect_to_parodus()
{
    int backoffRetryTime = 0;
    int backoff_max_time = 9;
    int max_retry_sleep;
    //Retry Backoff count shall start at c=2 & calculate 2^c - 1.
    int c = 2;
    int retval = -1;

    pthread_detach(pthread_self());

    max_retry_sleep = (int) pow(2, backoff_max_time) - 1;
    SnmpAdapterPrint("[PARODUS2SNMP] max_retry_sleep is %d\n", max_retry_sleep);

    if ( (0 == parodus_url[0]) || ((0 == client_url[0])) )
    {
        get_parodus_url(parodus_url, client_url);
    }

    libpd_cfg_t cfg1 = { .service_name = "snmp", .receive = true, .keepalive_timeout_secs = 64, .parodus_url = parodus_url, .client_url = client_url };
   // libpd_cfg_t cfg1 = { .service_name = "config", .receive = true, .keepalive_timeout_secs = 64, .parodus_url = parodus_url, .client_url = client_url };

    SnmpAdapterPrint("[PARODUS2SNMP] Calling libparodus_init() with parodus url \"%s\" and client url \"%s\"\n", cfg1.parodus_url, cfg1.client_url);

    while (1)
    {
        if (backoffRetryTime < max_retry_sleep)
        {
            backoffRetryTime = (int) pow(2, c) - 1;
        }

        int ret = libparodus_init(&g_current_instance, &cfg1);
        if (ret == 0)
        {
            SnmpAdapterPrint("[PARODUS2SNMP] Init for parodus Success..!\n");
            break;
        }
        else
        {
            SnmpAdapterPrint("[PARODUS2SNMP] Initialization of libparodus failed: '%s' ! \n", libparodus_strerror(ret));
            SnmpAdapterPrint("[PARODUS2SNMP] Going to retry again in %d seconds...\n", backoffRetryTime);
            sleep(backoffRetryTime);
            c++;
        }
        retval = libparodus_shutdown(&g_current_instance);
        SnmpAdapterPrint("[PARODUS2SNMP] libparodus_shutdown retval %d\n", retval);
    }

}

/*
 * getargs
 */
#ifndef SNMPADAPTER_TEST_STANDALONE
static
#endif
int getargs(char* str, int* pargc, char** pargv)
{
    if (!str || !pargc || !pargv)
    {
        SnmpAdapterPrint("[PARODUS2SNMP] getargs() : error params!\n");
        return 1;
    }

    *pargc = 0;

    char *token = strtok(str, " \t\n\r");
    if (!token)
    {
        SnmpAdapterPrint("[PARODUS2SNMP] getargs() : Error parsing getargs param !\n");
        return 1;
    }

    pargv[(*pargc)++] = token;

    while ((token = strtok(NULL, " \t\n\r")) && (*pargc < SNMPADAPTER_MAX_ARGS))
    {
        pargv[(*pargc)++] = token;
    }

    if (token)
    {
        SnmpAdapterPrint("[PARODUS2SNMP] getargs() : Exceeded Max allowed tokens : %d \n", SNMPADAPTER_MAX_ARGS);
    }

    return 0; // success
}

/**
 * @brief Creates the SNMP message using data form req_struct
 * This method allocates memory for param 'command'
 * The called should free memory by calling snmpadapter_delete_command(command)
 *
 *  This method returns the size of allocated command if success.
 *  otherwise returns 0 for failure.
 */
#ifndef SNMPADAPTER_TEST_STANDALONE
static
#endif
int snmpadapter_create_command(req_struct* snmpdata, char** command)
{
    if (!snmpdata || !command)
        return 0; //failed

    char* pstr = NULL;
    int cmdsize = 0;

    /* calculate command size, malloc and copy data*/
    if (snmpdata->reqType == GET)
    {
        if (snmpdata->u.getReq == NULL)
            return 0; //failed

        int statsize = cmdsize = strlen(SNMPADAPTER_GET) + 1 + strlen(SNMPADAPTER_SUPPORTED_VERSION) + 1 + strlen(COMCAST_COMMUNITY_CMD) + 1 + strlen(COMCAST_COMMUNITY_TOKEN) + 1 + strlen(TARGET_AGENT) + 1;

        for (unsigned int c = 0; c < snmpdata->u.getReq->paramCnt; c++)
        {
            cmdsize += strlen(snmpdata->u.getReq->paramNames[c]) + 1;
        }

        pstr = *command = (char*) malloc(cmdsize + 1);
        if (pstr == NULL)
            return 0;

        snprintf(pstr, statsize + 1, "%s %s %s %s %s ",
        SNMPADAPTER_GET, SNMPADAPTER_SUPPORTED_VERSION, COMCAST_COMMUNITY_CMD, COMCAST_COMMUNITY_TOKEN, TARGET_AGENT);
        pstr += statsize;

        for (unsigned int c = 0; c < snmpdata->u.getReq->paramCnt; c++)
        {
            size_t n = strlen(snmpdata->u.getReq->paramNames[c]) + 1;
            snprintf(pstr, n + 1, "%s ", snmpdata->u.getReq->paramNames[c]);
            pstr += n;
        }

        *(--pstr) = '\0';
    }
    else if (snmpdata->reqType == SET)
    {
        if (snmpdata->u.setReq == NULL)
            return 0; //failed

        int statsize = cmdsize = strlen(SNMPADAPTER_SET) + 1 + strlen(SNMPADAPTER_SUPPORTED_VERSION) + 1 + strlen(COMCAST_COMMUNITY_CMD) + 1 + strlen(COMCAST_COMMUNITY_TOKEN) + 1 + strlen(TARGET_AGENT) + 1;

        SnmpAdapterPrint("snmpdata->u.set->count = %d\n", (int )snmpdata->u.setReq->paramCnt);
        for (unsigned int c = 0; c < snmpdata->u.setReq->paramCnt; c++)
        {
            cmdsize += strlen(snmpdata->u.setReq->param[c].name) + 1; //oid name
            cmdsize += sizeof(char) + 1; //oid type
            cmdsize += strlen(snmpdata->u.setReq->param[c].value) + 1; //oid value
        }

        pstr = *command = (char*) malloc(cmdsize + 1);
        if (pstr == NULL)
            return 0;

        snprintf(pstr, statsize + 1, "%s %s %s %s %s ",
        SNMPADAPTER_SET, SNMPADAPTER_SUPPORTED_VERSION, COMCAST_COMMUNITY_CMD, COMCAST_COMMUNITY_TOKEN, TARGET_AGENT);
        pstr += statsize;

        for (unsigned int c = 0; c < snmpdata->u.setReq->paramCnt; c++)
        {
            size_t n = strlen(snmpdata->u.setReq->param[c].name) + 1 + sizeof(char) + 1 + strlen(snmpdata->u.setReq->param[c].value) + 1;

            // get type
            char tc = snmpadapter_get_snmp_type(snmpdata->u.setReq->param[c].type);

            SnmpAdapterPrint("%d - oid  [%s]\n", c, snmpdata->u.setReq->param[c].name);
            SnmpAdapterPrint("%d - type [%c]\n", c, snmpdata->u.setReq->param[c].type);
            SnmpAdapterPrint("%d - value[%s]\n", c, snmpdata->u.setReq->param[c].value);

            //snprintf(pstr, n+1, "%s %c %s ", snmpdata->u.setReq->param[c].name, *(char*)&(snmpdata->u.setReq->param[c].type), snmpdata->u.setReq->param[c].value);
            snprintf(pstr, n + 1, "%s %c %s ", snmpdata->u.setReq->param[c].name, tc, snmpdata->u.setReq->param[c].value);
            pstr += n;
        }

        *(--pstr) = '\0';
    }

    return (int) (pstr - *command);
}

static void snmpadapter_delete_command(char* command)
{
    if (command != NULL)
        free(command);

    return;
}


/*
 * snmpadapter_handle_request
 */
static int snmpadapter_handle_request(char* request, char *transactionId, char **response)
{
    int ret = 0;
    char* snmpcommand = NULL;
    SnmpAdapterPrint("[PARODUS2SNMP] snmpadapter_handle_request() : request: %s\n, transactionId = %s\n", request, transactionId);

    req_struct* snmpdata = NULL;
    wdmp_parse_request(request, &snmpdata);
    if (snmpdata == NULL)
    {
        ret = 1; //failed
        goto exit1;
    }

    int len = snmpadapter_create_command(snmpdata, &snmpcommand);
    if (snmpcommand == NULL || strlen(snmpcommand) == 0 || len == 0)
    {
        ret = 1; //failed
        goto exit1;
    }

    SnmpAdapterPrint("[PARODUS2SNMP] snmpadapter_handle_request() : command [%s]\nlength=%d\n", snmpcommand, len);

    int argc = 0;
    char* argv[SNMPADAPTER_MAX_ARGS] = { };
    getargs(snmpcommand, &argc, argv);
    if (argc == 0 || argv[0] == NULL)
    {
        SnmpAdapterPrint("[PARODUS2SNMP] snmpadapter_handle_request() : could't parse arguments !\n");
        ret = 1; //failed
        goto exit1;
    }

#ifdef SNMPADAPTER_TEST_STANDALONE

    SnmpAdapterPrint("[PARODUS2SNMP] snmpadapter_handle_request() : argc: %d\n", argc);
    int cnt = argc, i = 0;
    while (cnt--)
    {
        SnmpAdapterPrint("[PARODUS2SNMP] snmpadapter_handle_request() : argv[%d] : %s\n", i, argv[i]);
        i++;
    }

#endif

    if (strstr(argv[0], SNMPADAPTER_GET) != NULL)
    {
        // call snmp adapter get, return back response
        SnmpAdapterPrint("call snmp_adapter_send_receive_get...\n");
        ret = snmp_adapter_send_receive_get(argc, argv, response);
    }
    else if (strstr(argv[0], SNMPADAPTER_SET) != NULL)
    {
        // call snmp adapter set. return back success/error response
        SnmpAdapterPrint("call snmp_adapter_send_receive_set...\n");
        ret = snmp_adapter_send_receive_set(argc, argv, response);
    }

    exit1:
    if (snmpcommand != NULL)
    {
        snmpadapter_delete_command(snmpcommand);
    }
    if (snmpdata != NULL)
    {
        wdmp_free_req_struct(snmpdata);
    }

    return ret;
}

/*
 * diff_time
 */
static long diff_time(struct timespec *starttime, struct timespec *finishtime)
{
    long msec;
    msec = (finishtime->tv_sec - starttime->tv_sec) * 1000;
    msec += (finishtime->tv_nsec - starttime->tv_nsec) / 1000000;
    return msec;
}

/*
 * send_receive_from_parodus
 */
static void send_receive_from_parodus()
{
    int rtn;
    wrp_msg_t *wrp_msg;
    wrp_msg_t *res_wrp_msg;

    struct timespec start, end, *startPtr, *endPtr;
    startPtr = &start;
    endPtr = &end;
    char *contentType = NULL;

    while (1)
    {
        rtn = libparodus_receive(g_current_instance, &wrp_msg, 2000);
        if (rtn == 1)
        {
            continue;
        }

        if (rtn != 0)
        {
            SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : Libparodus failed to recieve message: '%s'\n", libparodus_strerror(rtn));
            sleep(5);
            continue;
        }

        switch (wrp_msg->msg_type)
        {
            case WRP_MSG_TYPE__REQ:

                SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : received WRP_MSG_TYPE__REQ \n");
                res_wrp_msg = (wrp_msg_t *) malloc(sizeof(wrp_msg_t));
                if(NULL == res_wrp_msg)
                {
                    SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : malloc FAILED !!! \n");
                    return;
                }
                memset(res_wrp_msg, 0, sizeof(wrp_msg_t));

                clock_gettime(CLOCK_REALTIME, startPtr);

                // Process request message and get the response payload to send back
                snmpadapter_handle_request((char *) wrp_msg->u.req.payload, wrp_msg->u.req.transaction_uuid, ((char **) (&(res_wrp_msg->u.req.payload))));

                if (res_wrp_msg->u.req.payload == NULL)
                {
                    SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : Response payload is NULL !!\n");

                    //MURUGAN: TODO : prepare and send appropriate error message in response
                    res_wrp_msg->u.req.payload_size = 0;
                    continue;
                }
                else
                {
                    SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : Response payload is %s\n", (char * ) (res_wrp_msg->u.req.payload));
                    res_wrp_msg->u.req.payload_size = strlen(res_wrp_msg->u.req.payload);
                }

                // Send Response back

                res_wrp_msg->msg_type = wrp_msg->msg_type;
                res_wrp_msg->u.req.source = wrp_msg->u.req.dest;
                res_wrp_msg->u.req.dest = wrp_msg->u.req.source;
                res_wrp_msg->u.req.transaction_uuid = wrp_msg->u.req.transaction_uuid;
                contentType = (char *) malloc(sizeof(char) * (strlen(CONTENT_TYPE_ASCII) + 1));
                strncpy(contentType, CONTENT_TYPE_ASCII, strlen(CONTENT_TYPE_ASCII) + 1);
                res_wrp_msg->u.req.content_type = contentType;

                int sendStatus = libparodus_send(g_current_instance, res_wrp_msg);
                if (sendStatus == 0)
                {
                    SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : Sent message successfully to parodus\n");
                }
                else
                {
                    SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : Failed to send message!! : '%s'\n", libparodus_strerror(sendStatus));
                }

                clock_gettime(CLOCK_REALTIME, endPtr);
                SnmpAdapterPrint("[PARODUS2SNMP] send_receive_from_parodus() : Elapsed time : %ld ms\n", diff_time(startPtr, endPtr));

                //free response data structures
                wrp_free_struct(res_wrp_msg);

                break;

            case WRP_MSG_TYPE__CREATE:
            case WRP_MSG_TYPE__RETREIVE:
            case WRP_MSG_TYPE__UPDATE:
            case WRP_MSG_TYPE__DELETE:
                SnmpAdapterPrint("SNMPADAPTER: send_receive_from_parodus() - received CRUD \n");
                break;

            default:
                SnmpAdapterPrint("SNMPADAPTER: send_receive_from_parodus() - received unknown type \n");
                break;
        }

        free(wrp_msg);

    } //while (1)

    libparodus_shutdown(&g_current_instance);

    SnmpAdapterPrint("End of parodus_upstream\n");
    return;
}

/* ------------------------------------------------------------------------------------------------
 * main()
 */
int snmpadapter_main()
{
    connect_to_parodus();
    send_receive_from_parodus();

    return 1;
}

