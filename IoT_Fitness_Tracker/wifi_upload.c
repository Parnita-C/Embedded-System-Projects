/*
 * wifi_upload.c
 *
 * Wi-Fi Connection & HTTP POST Upload — CC3200 Smart Fitness Trainer
 * Uses SimpleLink Wi-Fi (CC3200SDK_1.4.0) + sl_Send HTTP over raw socket
 *
 * Uploads a JSON workout summary to a configurable web endpoint.
 *
 * Usage:
 *   1. Set WIFI_SSID, WIFI_PASSWORD, SERVER_HOST, SERVER_PATH below.
 *   2. Call WiFi_Connect() at startup.
 *   3. Call WiFi_UploadWorkout() after each session.
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* hw_types.h first — required by all CC3200SDK driverlib headers */
#include "hw_types.h"
#include "hw_memmap.h"
#include "rom.h"
#include "rom_map.h"
#include "utils_if.h"
#include "uart_if.h"

/* SimpleLink */
#include "simplelink.h"
#include "wlan.h"
#include "netapp.h"
#include "socket.h"

#include "wifi_upload.h"

/* -----------------------------------------------------------------------
 * Configuration — update these for your network and server
 * ----------------------------------------------------------------------- */
#define WIFI_SSID        "YourSSID"
#define WIFI_PASSWORD    "YourPassword"
#define SECURITY_TYPE    SL_SEC_TYPE_WPA_WPA2

#define SERVER_HOST      "your-server.example.com"  /* or dotted IP string */
#define SERVER_PORT      80
#define SERVER_PATH      "/api/workout"


#define UART_PRINT Report

/* Max loops × 50 ms delay = ~10 s connect timeout */
#define CONNECT_TIMEOUT_LOOPS   200

/* -----------------------------------------------------------------------
 * Internal state
 * ----------------------------------------------------------------------- */
static bool g_wifi_connected = false;

/*
 * NOTE: SimpleLinkWlanEventHandler, SimpleLinkNetAppEventHandler,
 * SimpleLinkSockEventHandler, SimpleLinkHttpServerCallback, and
 * SimpleLinkGeneralEventHandler are all defined in network_if.c
 * (CC3200SDK example/common). Do NOT redefine them here — the linker
 * will report "symbol redefined" errors if you do.
 */

/* -----------------------------------------------------------------------
 * WiFi_Connect
 *   Starts SimpleLink, connects to the AP, polls for an IP address.
 *   Uses sl_NetCfgGet(SL_IPV4_STA_P2P_CL_GET_INFO) — the correct
 *   CC3200SDK 1.4.0 API (replaces the non-existent SlIpV / SlNetApp
 *   identifiers that caused compile errors).
 *   Returns true on success.
 * ----------------------------------------------------------------------- */
bool WiFi_Connect(void)
{
    SlSecParams_t sec;
    unsigned long ip      = 0;
    unsigned long gw      = 0;
    unsigned long dns     = 0;
    unsigned char dhcp    = 0;
    unsigned char len     = sizeof(unsigned long);
    int16_t  ret;
    uint16_t loops;

    /* Start SimpleLink in Station mode */
    sl_Start(NULL, NULL, NULL);
    sl_WlanSetMode(ROLE_STA);

    /* Disconnect any existing connection */
    sl_WlanDisconnect();
    MAP_UtilsDelay(4000000);   /* ~50 ms */

    /* Set connection policy: Auto only (no SmartConfig) */
    sl_WlanPolicySet(SL_POLICY_CONNECTION,
                     SL_CONNECTION_POLICY(1, 0, 0, 0, 0),
                     NULL, 0);

    sec.Type   = SECURITY_TYPE;
    sec.Key    = (signed char *)WIFI_PASSWORD;
    sec.KeyLen = (unsigned char)strlen(WIFI_PASSWORD);

    ret = sl_WlanConnect((signed char *)WIFI_SSID,
                         (unsigned char)strlen(WIFI_SSID),
                         NULL, &sec, NULL);
    if (ret != 0)
    {
        UART_PRINT("WiFi: sl_WlanConnect failed %d\r\n", (int)ret);
        return false;
    }

    /*
     * Poll for a valid IP address using the correct CC3200SDK 1.4.0 call:
     *   sl_NetCfgGet(SL_IPV4_STA_P2P_CL_GET_INFO, ...)
     * This fills a SlNetCfgIpV4Args_t structure.
     */
    {
        SlNetCfgIpV4Args_t ip_cfg;
        unsigned char cfg_len = sizeof(ip_cfg);
        unsigned char cfg_opt = IPCONFIG_MODE_ENABLE_IPV4;

        for (loops = 0; loops < CONNECT_TIMEOUT_LOOPS; loops++)
        {
            MAP_UtilsDelay(2000000);   /* ~50 ms per loop */

            memset(&ip_cfg, 0, sizeof(ip_cfg));
            sl_NetCfgGet(SL_IPV4_STA_P2P_CL_GET_INFO,
                         &cfg_opt, &cfg_len,
                         (unsigned char *)&ip_cfg);

            if (ip_cfg.ipV4 != 0)
            {
                UART_PRINT("WiFi: Connected — IP %d.%d.%d.%d\r\n",
                    (int)((ip_cfg.ipV4 >> 24) & 0xFF),
                    (int)((ip_cfg.ipV4 >> 16) & 0xFF),
                    (int)((ip_cfg.ipV4 >>  8) & 0xFF),
                    (int)( ip_cfg.ipV4        & 0xFF));
                g_wifi_connected = true;
                return true;
            }
        }
    }

    UART_PRINT("WiFi: Timed out waiting for IP\r\n");
    return false;
}

/* -----------------------------------------------------------------------
 * WiFi_UploadWorkout
 *   Resolves SERVER_HOST via DNS, opens a TCP socket, sends an HTTP POST
 *   with a JSON body, reads the first bytes of the response to check for
 *   "200", then closes the socket.
 *
 *   Uses sl_NetAppDnsGetHostByName() — the correct CC3200SDK 1.4.0 DNS
 *   API (removes the bogus SlNetAppDnsClientTime_t that caused errors).
 *
 *   Returns true on HTTP 200.
 * ----------------------------------------------------------------------- */
bool WiFi_UploadWorkout(WorkoutMode_t mode, uint32_t count)
{
    char body[80];
    char request[256];
    int  req_len;
    int16_t sock, ret;
    unsigned long dest_ip = 0;
    SlSockAddrIn_t addr;
    char resp[64];

    if (!g_wifi_connected) return false;

    /* Build JSON body */
    if (mode == MODE_PUSHUP)
        snprintf(body, sizeof(body),
                 "{\"mode\":\"pushup\",\"reps\":%lu}", (unsigned long)count);
    else
        snprintf(body, sizeof(body),
                 "{\"mode\":\"running\",\"steps\":%lu}", (unsigned long)count);

    /* Build HTTP request */
    req_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        SERVER_PATH, SERVER_HOST, (int)strlen(body), body);

    /*
     * DNS lookup — correct CC3200SDK 1.4.0 signature:
     *   sl_NetAppDnsGetHostByName(name, namelen, &ip, family)
     * No SlNetAppDnsClientTime_t parameter exists in this SDK version.
     */
    ret = sl_NetAppDnsGetHostByName(
              (signed char *)SERVER_HOST,
              (unsigned short)strlen(SERVER_HOST),
              (_u32 *)&dest_ip,
              SL_AF_INET);
    if (ret != 0)
    {
        UART_PRINT("WiFi: DNS failed %d\r\n", (int)ret);
        return false;
    }

    /* Open TCP socket */
    sock = sl_Socket(SL_AF_INET, SL_SOCK_STREAM, SL_IPPROTO_TCP);
    if (sock < 0)
    {
        UART_PRINT("WiFi: socket failed %d\r\n", (int)sock);
        return false;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = SL_AF_INET;
    addr.sin_port        = sl_Htons((unsigned short)SERVER_PORT);
    addr.sin_addr.s_addr = sl_Htonl(dest_ip);

    ret = sl_Connect(sock, (SlSockAddr_t *)&addr, sizeof(addr));
    if (ret != 0)
    {
        UART_PRINT("WiFi: Connect failed %d\r\n", (int)ret);
        sl_Close(sock);
        return false;
    }

    /* Send HTTP POST */
    sl_Send(sock, request, req_len, 0);

    /* Read first chunk of response — look for "200" status */
    memset(resp, 0, sizeof(resp));
    sl_Recv(sock, resp, sizeof(resp) - 1, 0);
    sl_Close(sock);

    {
        bool ok = (strstr(resp, "200") != NULL);
        UART_PRINT("WiFi: Upload %s — resp: %.30s\r\n",
                   ok ? "OK" : "FAIL", resp);
        return ok;
    }
}

/* -----------------------------------------------------------------------
 * WiFi_IsConnected
 * ----------------------------------------------------------------------- */
bool WiFi_IsConnected(void)
{
    return g_wifi_connected;
}
