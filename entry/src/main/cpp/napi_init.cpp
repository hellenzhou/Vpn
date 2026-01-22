/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
 */
// [Start vpn_control_case_c++]
#include "napi/native_api.h"
#include "hilog/log.h"
 
#include <cstring>
#include <thread>
#include <js_native_api.h>
#include <js_native_api_types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <sys/time.h>
 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
 
#define BUFFER_SIZE 2048
 
#define VPN_LOG_TAG "NetMgrVpn"
#define VPN_LOG_DOMAIN 0x15b0
#define MAKE_FILE_NAME (strrchr(__FILE__, '/') + 1)

#define NETMANAGER_VPN_LOGE(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, VPN_LOG_DOMAIN, VPN_LOG_TAG, "ZBQ vpn [%{public}s %{public}d] " fmt, MAKE_FILE_NAME, __LINE__, ##__VA_ARGS__)

#define NETMANAGER_VPN_LOGI(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, VPN_LOG_DOMAIN, VPN_LOG_TAG, "ZBQ vpn [%{public}s %{public}d] " fmt, MAKE_FILE_NAME, __LINE__, ##__VA_ARGS__)

#define NETMANAGER_VPN_LOGD(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, VPN_LOG_DOMAIN, VPN_LOG_TAG, "ZBQ vpn [%{public}s %{public}d] " fmt, MAKE_FILE_NAME, __LINE__, ##__VA_ARGS__)

struct FdInfo {
    int32_t tunFd = 0;
    int32_t tunnelFd = 0;
    struct sockaddr_in serverAddr;
};

static FdInfo g_fdInfo;
static bool g_threadRunF = false;
static std::thread g_threadT1;
static std::thread g_threadT2;
static int g_packetsSent = 0;  // 发送的数据包计数
static int g_responsesReceived = 0;  // 接收的响应计数
static time_t g_lastResponseTime = 0;  // 最后一次收到响应的时间
static int g_ipv4Packets = 0;  // IPv4数据包计数
static int g_ipv6Packets = 0;  // IPv6数据包计数
static int g_ipv4TcpPackets = 0;  // IPv4 TCP数据包计数
static int g_ipv6TcpPackets = 0;  // IPv6 TCP数据包计数
static int g_httpPackets = 0;  // HTTP数据包计数 (端口80)
static int g_httpsPackets = 0;  // HTTPS数据包计数 (端口443)
static int g_detailedLogCount = 0;  // 详细日志计数器（仅记录前20个HTTP/HTTPS连接）
static int g_packetsReadFromTun = 0;  // 从TUN读取的数据包总数
static int g_packetsForwarded = 0;  // 成功转发的数据包总数
static int g_packetsDropped = 0;  // 被丢弃的数据包总数（读取失败、发送失败等）
static int g_packetsSendFailed = 0;  // 发送失败的数据包数
static time_t g_vpnStartTime = 0;  // VPN启动时间
static int g_trafficCheckInterval = 0;  // 流量检查间隔计数器
// 获取对应字符串数据, 用于获取udp server 的IP地址
static constexpr const int MAX_STRING_LENGTH = 1024;

std::string GetStringFromValueUtf8(napi_env env, napi_value value)
{
    std::string result;
    char str[MAX_STRING_LENGTH] = {0};
    size_t length = 0;
    napi_get_value_string_utf8(env, value, str, MAX_STRING_LENGTH, &length);
    if (length > 0) {
        return result.append(str, length);
    }
    return result;
}

void HandleReadTunfd(FdInfo fdInfo)
{
    NETMANAGER_VPN_LOGI("=== TUN READ THREAD STARTED ===");
    NETMANAGER_VPN_LOGI("tunFd: %{public}d, tunnelFd: %{public}d, server: %{public}s:%{public}d",
                        fdInfo.tunFd, fdInfo.tunnelFd,
                        inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] TUN读取线程已启动 tunFd=%{public}d tunnelFd=%{public}d 服务器=%{public}s:%{public}d",
                 fdInfo.tunFd, fdInfo.tunnelFd,
                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
    
    // 🔥 记录VPN启动时间，用于流量劫持检查
    g_vpnStartTime = time(nullptr);
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                 "🔍 [流量劫持检查] VPN启动时间: %{public}ld, 开始监控流量劫持情况", g_vpnStartTime);

    uint8_t buffer[BUFFER_SIZE] = {0};
    int packetCount = 0;

    while (g_threadRunF) {
        // 检查文件描述符有效性
        if (fdInfo.tunFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunFd: %{public}d, stopping read loop", fdInfo.tunFd);
            break;
        }

        int readResult = read(fdInfo.tunFd, buffer, sizeof(buffer));
        if (readResult <= 0) {
            if (errno != EAGAIN) {
                NETMANAGER_VPN_LOGE("read tun device error: %{public}d, tunfd: %{public}d", errno, fdInfo.tunFd);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ TUN读取失败: errno=%{public}d, 数据包被丢弃", errno);
                g_packetsDropped++;
            }
            continue;
        }

        packetCount++;
        g_packetsReadFromTun++;  // 统计从TUN读取的数据包
        
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                     "📥 从TUN读取数据包 #%{public}d: 大小=%{public}d字节 (总计读取: %{public}d, 已转发: %{public}d, 已丢弃: %{public}d)",
                     packetCount, readResult, g_packetsReadFromTun, g_packetsForwarded, g_packetsDropped);
        
        // 解析IP版本
        if (readResult >= 1) {
            uint8_t version = (buffer[0] >> 4) & 0x0F;
            
            if (version == 4 && readResult >= 20) {  // IPv4
                g_ipv4Packets++;
                uint8_t protocol = buffer[9];
                
                // 提取源IP和目标IP
                char srcIP[16], dstIP[16];
                snprintf(srcIP, sizeof(srcIP), "%d.%d.%d.%d", buffer[12], buffer[13], buffer[14], buffer[15]);
                snprintf(dstIP, sizeof(dstIP), "%d.%d.%d.%d", buffer[16], buffer[17], buffer[18], buffer[19]);
                
                if (protocol == 6) {  // TCP
                    g_ipv4TcpPackets++;
                    if (readResult >= 40) {
                        uint16_t srcPort = (buffer[20] << 8) | buffer[21];
                        uint16_t dstPort = (buffer[22] << 8) | buffer[23];
                        
                        // 追踪HTTP/HTTPS流量
                        const char* serviceLabel = "";
                        bool isHttpTraffic = false;
                        if (dstPort == 80) {
                            g_httpPackets++;
                            serviceLabel = " [HTTP浏览器流量]";
                            isHttpTraffic = true;
                        } else if (dstPort == 443) {
                            g_httpsPackets++;
                            serviceLabel = " [HTTPS浏览器流量]";
                            isHttpTraffic = true;
                        }
                        
                        // 🔥 详细记录前20个HTTP/HTTPS连接
                        if (isHttpTraffic && g_detailedLogCount < 20) {
                            g_detailedLogCount++;
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                         "🔥🔥🔥 [关键] HTTP/HTTPS连接 #%{public}d: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s (大小=%{public}d字节)",
                                         g_detailedLogCount, srcIP, srcPort, dstIP, dstPort, serviceLabel, readResult);
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                         "🔥 [关键] 此连接已被VPN捕获并发送到服务器127.0.0.1:8888");
                        }
                        
                        // 🔥 记录所有TCP连接（包括HTTP/HTTPS）
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                     "📊 TCP数据包 #%{public}d: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s (大小=%{public}d字节)",
                                     packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel, readResult);
                        
                        NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 TCP %s:%d -> %s:%d%s", 
                                           packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel);
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                     "[VPN客户端] 📊 数据包 #%{public}d: IPv4 TCP %{public}s:%{public}d -> %{public}s:%{public}d%{public}s",
                                     packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel);
                    }
                } else if (protocol == 17) {  // UDP
                    if (readResult >= 28) {
                        uint16_t srcPort = (buffer[20] << 8) | buffer[21];
                        uint16_t dstPort = (buffer[22] << 8) | buffer[23];
                        
                        // 🔥 详细记录DNS查询（UDP端口53）
                        if (dstPort == 53) {
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                         "🔍 DNS查询请求: %{public}s:%{public}d -> %{public}s:%{public}d (UDP, 大小=%{public}d字节)",
                                         srcIP, srcPort, dstIP, dstPort, readResult);
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                         "📤 DNS查询将通过UDP隧道转发到VPN服务器");
                        } else {
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                         "📦 UDP数据包: %{public}s:%{public}d -> %{public}s:%{public}d (大小=%{public}d字节)",
                                         srcIP, srcPort, dstIP, dstPort, readResult);
                        }
                    }
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 UDP %s -> %s", packetCount, srcIP, dstIP);
                } else if (protocol == 1) {  // ICMP
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "🏓 ICMP数据包: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 ICMP %s -> %s", packetCount, srcIP, dstIP);
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📦 其他协议数据包: 协议=%{public}d %{public}s -> %{public}s (大小=%{public}d字节)",
                                 protocol, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 protocol=%d %s -> %s", packetCount, protocol, srcIP, dstIP);
                }
            } else if (version == 6 && readResult >= 40) {  // IPv6
                g_ipv6Packets++;
                uint8_t nextHeader = buffer[6];
                
                char srcIP[40], dstIP[40];
                snprintf(srcIP, sizeof(srcIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15],
                         buffer[16], buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22], buffer[23]);
                snprintf(dstIP, sizeof(dstIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[24], buffer[25], buffer[26], buffer[27], buffer[28], buffer[29], buffer[30], buffer[31],
                         buffer[32], buffer[33], buffer[34], buffer[35], buffer[36], buffer[37], buffer[38], buffer[39]);
                
                if (nextHeader == 6) {  // TCP
                    g_ipv6TcpPackets++;
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📊 IPv6 TCP数据包 #%{public}d: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 TCP %s -> %s", packetCount, srcIP, dstIP);
                } else if (nextHeader == 17) {  // UDP
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📊 IPv6 UDP数据包 #%{public}d: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 UDP %s -> %s", packetCount, srcIP, dstIP);
                } else if (nextHeader == 58) {  // ICMPv6
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📊 IPv6 ICMPv6数据包 #%{public}d: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 ICMPv6 %s -> %s", packetCount, srcIP, dstIP);
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📊 IPv6其他协议数据包 #%{public}d: 协议=%{public}d %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, nextHeader, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 nextHeader=%d %s -> %s", packetCount, nextHeader, srcIP, dstIP);
                }
            } else {
                // 数据包太小或版本未知，但仍然会被转发
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ 未知/异常数据包 #%{public}d: IP版本=%{public}d (大小=%{public}d字节) - 仍将转发",
                             packetCount, version, readResult);
                NETMANAGER_VPN_LOGI("📊 PACKET #%d: Unknown IP version %d", packetCount, version);
            }
        } else {
            // readResult < 1 的情况（理论上不会到这里，因为上面已经检查了readResult <= 0）
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "⚠️ 异常数据包 #%{public}d: 大小=%{public}d字节 (< 1字节) - 仍将转发",
                         packetCount, readResult);
        }

        // 读取到虚拟网卡的数据，通过udp隧道，发送给服务器
        if (fdInfo.tunnelFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunnelFd: %{public}d, stopping send loop", fdInfo.tunnelFd);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "❌ 隧道FD无效: tunnelFd=%{public}d, 数据包 #%{public}d 无法转发，线程退出",
                         fdInfo.tunnelFd, packetCount);
            g_packetsDropped++;
            break;
        }
        
        // 🔥 确保所有数据包都被转发（无论大小、版本、协议）
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                     "🔄 准备转发数据包 #%{public}d: 大小=%{public}d字节 -> VPN服务器 %{public}s:%{public}d",
                     packetCount, readResult, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
        
        // 🔥 转发前检查：隧道FD和服务器地址有效性
        if (fdInfo.tunnelFd < 0) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "❌ [转发验证] 隧道FD无效: %{public}d，无法转发数据包", fdInfo.tunnelFd);
        }
        if (fdInfo.serverAddr.sin_addr.s_addr == 0) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "❌ [转发验证] 服务器地址无效: 0.0.0.0，无法转发数据包");
        }

        // 🔥 发送前记录详细信息（IPv4和IPv6）
        // 注意：即使数据包太小或格式异常，也会被转发（因为sendto不检查内容）
        if (readResult >= 20) {
            uint8_t version = (buffer[0] >> 4) & 0x0F;
            if (version == 4) {
                uint8_t protocol = buffer[9];
                char srcIP[16], dstIP[16];
                snprintf(srcIP, sizeof(srcIP), "%d.%d.%d.%d", buffer[12], buffer[13], buffer[14], buffer[15]);
                snprintf(dstIP, sizeof(dstIP), "%d.%d.%d.%d", buffer[16], buffer[17], buffer[18], buffer[19]);
                
                if (protocol == 17 && readResult >= 28) {  // UDP
                    uint16_t srcPort = (buffer[20] << 8) | buffer[21];
                    uint16_t dstPort = (buffer[22] << 8) | buffer[23];
                    if (dstPort == 53) {
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                     "🚀 转发DNS查询: %{public}s:%{public}d -> %{public}s:%{public}d (UDP, %{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                     srcIP, srcPort, dstIP, dstPort, readResult,
                                     inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                    } else {
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                     "🚀 转发UDP请求: %{public}s:%{public}d -> %{public}s:%{public}d (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                     srcIP, srcPort, dstIP, dstPort, readResult,
                                     inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                    }
                } else if (protocol == 6 && readResult >= 40) {  // TCP
                    uint16_t srcPort = (buffer[20] << 8) | buffer[21];
                    uint16_t dstPort = (buffer[22] << 8) | buffer[23];
                    const char* serviceType = "";
                    if (dstPort == 80) serviceType = " [HTTP]";
                    else if (dstPort == 443) serviceType = " [HTTPS]";
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "🚀 转发TCP请求: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                 srcIP, srcPort, dstIP, dstPort, serviceType, readResult,
                                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                } else if (protocol == 1) {  // ICMP
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "🚀 转发ICMP数据包: %{public}s -> %{public}s (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                 srcIP, dstIP, readResult,
                                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "🚀 转发数据包: 协议=%{public}d %{public}s -> %{public}s (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                 protocol, srcIP, dstIP, readResult,
                                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                }
            } else if (version == 6 && readResult >= 40) {  // IPv6
                uint8_t nextHeader = buffer[6];
                char srcIP[40], dstIP[40];
                snprintf(srcIP, sizeof(srcIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15],
                         buffer[16], buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22], buffer[23]);
                snprintf(dstIP, sizeof(dstIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[24], buffer[25], buffer[26], buffer[27], buffer[28], buffer[29], buffer[30], buffer[31],
                         buffer[32], buffer[33], buffer[34], buffer[35], buffer[36], buffer[37], buffer[38], buffer[39]);
                
                const char* protocolName = "";
                if (nextHeader == 6) protocolName = "TCP";
                else if (nextHeader == 17) protocolName = "UDP";
                else if (nextHeader == 58) protocolName = "ICMPv6";
                else protocolName = "其他";
                
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "🚀 转发IPv6数据包: %{public}s -> %{public}s (协议=%{public}s, %{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                             srcIP, dstIP, protocolName, readResult,
                             inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
            } else {
                // 数据包太小（< 20字节），无法解析，但仍会转发
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "🚀 转发异常数据包: 大小=%{public}d字节 (< 20字节，无法解析) -> VPN服务器 %{public}s:%{public}d",
                             readResult, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
            }
        } else {
            // readResult < 20，数据包太小，但仍会转发
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🚀 转发异常数据包: 大小=%{public}d字节 (< 20字节) -> VPN服务器 %{public}s:%{public}d",
                         readResult, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
        }
        
        int sendResult = sendto(fdInfo.tunnelFd, buffer, readResult, 0,
            reinterpret_cast<struct sockaddr*>(&fdInfo.serverAddr), sizeof(fdInfo.serverAddr));
        if (sendResult <= 0) {
            int errno_save = errno;
            NETMANAGER_VPN_LOGE("❌ Failed to send packet #%d to server[%{public}s:%{public}d], ret: %{public}d, error: %{public}s",
                                packetCount, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port),
                                sendResult, strerror(errno_save));
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "❌ [转发失败] 数据包 #%{public}d 发送到服务器 %{public}s:%{public}d 失败",
                         packetCount, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "❌ [转发失败] 错误详情: ret=%{public}d, errno=%{public}d (%{public}s)",
                         sendResult, errno_save, strerror(errno_save));
            
            // 🔥 详细的错误诊断
            if (errno_save == ECONNREFUSED) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ [转发失败] 诊断: 连接被拒绝 - 代理服务器可能未运行或未监听端口");
            } else if (errno_save == ENETUNREACH) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ [转发失败] 诊断: 网络不可达 - 无法连接到代理服务器");
            } else if (errno_save == EHOSTUNREACH) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ [转发失败] 诊断: 主机不可达 - 代理服务器地址可能错误");
            } else if (errno_save == EAGAIN || errno_save == EWOULDBLOCK) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发失败] 诊断: 资源暂时不可用 - 可能是UDP缓冲区满，稍后重试");
            } else if (errno_save == EBADF) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ [转发失败] 诊断: 文件描述符无效 - tunnelFd=%{public}d 可能已关闭",
                             fdInfo.tunnelFd);
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ [转发失败] 诊断: 未知错误 - 请检查网络连接和代理服务器状态");
            }
            
            g_packetsSendFailed++;
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "📊 [转发统计] 读取=%{public}d, 转发成功=%{public}d, 转发失败=%{public}d, 丢弃=%{public}d",
                         g_packetsReadFromTun, g_packetsForwarded, g_packetsSendFailed, g_packetsDropped);
            continue;
        }

        g_packetsSent++;
        g_packetsForwarded++;  // 统计成功转发的数据包
        NETMANAGER_VPN_LOGI("✅ PACKET #%d: Sent %{public}d bytes to server (total sent: %{public}d, responses: %{public}d)", 
                           packetCount, sendResult, g_packetsSent, g_responsesReceived);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                     "✅ 转发成功: 数据包 #%{public}d 已发送 %{public}d 字节到VPN服务器 (总计发送: %{public}d, 收到响应: %{public}d)",
                     packetCount, sendResult, g_packetsSent, g_responsesReceived);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                     "📊 转发统计: 读取=%{public}d, 转发成功=%{public}d, 转发失败=%{public}d, 丢弃=%{public}d",
                     g_packetsReadFromTun, g_packetsForwarded, g_packetsSendFailed, g_packetsDropped);
        
        // 每10个数据包输出一次统计信息
        if (g_packetsSent % 10 == 0) {
            g_trafficCheckInterval++;
            time_t currentTime = time(nullptr);
            time_t vpnUptime = currentTime - g_vpnStartTime;
            
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] 📊 数据包统计: IPv4总数=%{public}d IPv4 TCP=%{public}d IPv6总数=%{public}d IPv6 TCP=%{public}d 发送=%{public}d 响应=%{public}d",
                         g_ipv4Packets, g_ipv4TcpPackets, g_ipv6Packets, g_ipv6TcpPackets, g_packetsSent, g_responsesReceived);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] 🌐 浏览器流量统计: HTTP(端口80)=%{public}d HTTPS(端口443)=%{public}d",
                         g_httpPackets, g_httpsPackets);
            
            // 🔥 流量劫持完整性检查
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [流量劫持检查] ========== 第%{public}d次检查 (VPN运行时间: %{public}ld秒) ==========",
                         g_trafficCheckInterval, vpnUptime);
            
            // 检查1: 是否有TCP流量（浏览器等应用）
            if (g_ipv4TcpPackets == 0 && g_ipv6TcpPackets == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️⚠️⚠️ [流量劫持检查] 警告：没有检测到TCP数据包！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [流量劫持检查] 可能原因：");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   1. VPN路由表未生效 - 流量未进入TUN设备");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   2. 应用流量绕过VPN - 可能使用了trustedApplications");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   3. 系统路由表配置错误 - 默认路由未指向vpn-tun");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   4. VPN连接未正确建立 - vpnConnection.create()可能失败");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "🔍 [流量劫持检查] 建议：检查VPN扩展能力日志，确认vpnConnection.create()是否成功");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [流量劫持检查] 检测到TCP流量：IPv4 TCP=%{public}d, IPv6 TCP=%{public}d",
                             g_ipv4TcpPackets, g_ipv6TcpPackets);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [流量劫持检查] 说明：VPN路由表已生效，部分流量已进入TUN设备");
            }
            
            // 检查2: 是否有UDP流量（DNS等）
            int totalUdpPackets = g_ipv4Packets - g_ipv4TcpPackets + (g_ipv6Packets - g_ipv6TcpPackets);
            if (totalUdpPackets == 0 && g_ipv4TcpPackets == 0 && g_ipv6TcpPackets == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️⚠️⚠️ [流量劫持检查] 严重警告：完全没有检测到任何流量！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [流量劫持检查] 这意味着：");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   - 所有应用流量都绕过了VPN");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   - 或者VPN路由表完全未生效");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   - 或者TUN设备未正确创建");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [流量劫持检查] 检测到UDP流量：总计约%{public}d个UDP数据包",
                             totalUdpPackets);
            }
            
            // 检查3: HTTP/HTTPS流量检查
            if (g_httpPackets == 0 && g_httpsPackets == 0 && (g_ipv4TcpPackets > 0 || g_ipv6TcpPackets > 0)) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [流量劫持检查] 有TCP流量但无HTTP/HTTPS流量（可能使用其他端口）");
            } else if (g_httpPackets > 0 || g_httpsPackets > 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [流量劫持检查] 检测到浏览器流量：HTTP=%{public}d, HTTPS=%{public}d",
                             g_httpPackets, g_httpsPackets);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [流量劫持检查] 说明：浏览器流量已被VPN成功劫持到TUN设备");
            }
            
            // 检查4: 流量劫持完整性评估
            if (vpnUptime > 10) {  // VPN运行超过10秒
                if (g_ipv4TcpPackets == 0 && g_ipv6TcpPackets == 0) {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "❌ [流量劫持检查] 结论：VPN运行%{public}ld秒，但未检测到TCP流量，流量劫持可能失败",
                                 vpnUptime);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "❌ [流量劫持检查] 建议：检查VPN配置，确认trustedApplications为空，blockedApplications为空");
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "✅ [流量劫持检查] 结论：VPN运行%{public}ld秒，已检测到流量，流量劫持正常工作",
                                 vpnUptime);
                }
            }
            
            // 🔥 检查5: 转发到代理服务器的完整性验证
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] ========== 转发到代理服务器验证 ==========");
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] 代理服务器: %{public}s:%{public}d",
                         inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] 隧道FD: %{public}d (有效=%{public}s)",
                         fdInfo.tunnelFd, fdInfo.tunnelFd >= 0 ? "是" : "否");
            
            // 转发统计
            int totalProcessed = g_packetsForwarded + g_packetsSendFailed;
            double forwardSuccessRate = 0.0;
            if (g_packetsReadFromTun > 0) {
                forwardSuccessRate = (double)g_packetsForwarded * 100.0 / (double)g_packetsReadFromTun;
            }
            
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] 从TUN读取: %{public}d 个数据包", g_packetsReadFromTun);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] 成功转发: %{public}d 个数据包", g_packetsForwarded);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] 转发失败: %{public}d 个数据包", g_packetsSendFailed);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] 转发成功率: %.2f%%", forwardSuccessRate);
            
            // 转发完整性检查
            if (g_packetsReadFromTun == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 警告：没有从TUN读取任何数据包，无法验证转发");
            } else if (g_packetsForwarded == 0 && g_packetsSendFailed > 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ [转发验证] 严重错误：所有数据包转发都失败！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "❌ [转发验证] 可能原因：");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   1. 代理服务器未运行或未监听 %{public}s:%{public}d",
                             inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   2. 网络连接问题 - 无法连接到代理服务器");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   3. 隧道FD无效 - tunnelFd=%{public}d", fdInfo.tunnelFd);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "   4. UDP socket发送失败 - 检查errno错误码");
            } else if (g_packetsForwarded == 0 && g_packetsSendFailed == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 警告：读取了%{public}d个数据包，但没有任何转发尝试",
                             g_packetsReadFromTun);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 可能原因：转发逻辑未执行或提前退出");
            } else if (forwardSuccessRate < 50.0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 警告：转发成功率较低 (%.2f%%)，可能存在问题",
                             forwardSuccessRate);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 建议：检查网络连接和代理服务器状态");
            } else if (forwardSuccessRate >= 99.0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [转发验证] 转发成功率优秀 (%.2f%%)，转发工作正常",
                             forwardSuccessRate);
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [转发验证] 转发成功率良好 (%.2f%%)，转发基本正常",
                             forwardSuccessRate);
            }
            
            // 检查是否有数据包遗漏
            if (g_packetsReadFromTun != totalProcessed) {
                int missing = g_packetsReadFromTun - totalProcessed;
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️⚠️⚠️ [转发验证] 警告：有%{public}d个数据包未被处理！",
                             missing);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 读取=%{public}d, 已处理=%{public}d (成功=%{public}d + 失败=%{public}d)",
                             g_packetsReadFromTun, totalProcessed, g_packetsForwarded, g_packetsSendFailed);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 这些数据包可能被遗漏，未转发到代理服务器");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [转发验证] 完整性验证通过：所有读取的数据包都已尝试转发");
            }
            
            // 检查响应情况
            if (g_packetsForwarded > 0 && g_responsesReceived == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 警告：已转发%{public}d个数据包，但未收到任何响应",
                             g_packetsForwarded);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "⚠️ [转发验证] 可能原因：代理服务器未响应或响应处理有问题");
            } else if (g_packetsForwarded > 0 && g_responsesReceived > 0) {
                double responseRate = (double)g_responsesReceived * 100.0 / (double)g_packetsForwarded;
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [转发验证] 收到响应: %{public}d个响应 (响应率: %.2f%%)",
                             g_responsesReceived, responseRate);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "✅ [转发验证] 说明：代理服务器正常工作，双向通信正常");
            }
            
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [转发验证] ==========================================");
            
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "🔍 [流量劫持检查] ==========================================");
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] 🔍 转发完整性检查: 从TUN读取=%{public}d, 成功转发=%{public}d, 转发失败=%{public}d, 丢弃=%{public}d",
                         g_packetsReadFromTun, g_packetsForwarded, g_packetsSendFailed, g_packetsDropped);
            
            // 🔥 关键检查：确保所有读取的数据包都被转发
            // 关系：g_packetsReadFromTun = g_packetsForwarded + g_packetsSendFailed + g_packetsDropped
            // 注意：totalProcessed已在上面定义，这里直接使用
            if (g_packetsReadFromTun != totalProcessed) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "[VPN客户端] ⚠️⚠️⚠️ 警告：转发不完整！读取=%{public}d, 已处理=%{public}d (成功=%{public}d + 失败=%{public}d), 差异=%{public}d",
                             g_packetsReadFromTun, totalProcessed, g_packetsForwarded, g_packetsSendFailed, 
                             g_packetsReadFromTun - totalProcessed);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "[VPN客户端] ⚠️ 可能有数据包在读取后、发送前被丢弃或遗漏！");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "[VPN客户端] ✅ 转发完整性验证通过：所有从TUN读取的数据包都已处理（成功转发或发送失败）");
            }
            
            if (g_ipv4TcpPackets == 0 && g_ipv6TcpPackets == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ⚠️ ⚠️ ⚠️ 警告：没有检测到TCP数据包（浏览器流量）！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ⚠️ 说明：VPN路由表未生效，浏览器流量未进入VPN隧道");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ⚠️ 此时浏览器能访问网站 = 正常（流量走物理网络）");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ✅ 检测到TCP数据包，VPN路由表已生效，流量进入VPN隧道！");
                
                if (g_httpPackets == 0 && g_httpsPackets == 0) {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ⚠️ 但是没有检测到HTTP/HTTPS流量（可能使用其他端口或协议）");
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ✅✅✅ 已捕获 %{public}d 个HTTP/HTTPS连接！", g_httpPackets + g_httpsPackets);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] 🔥 关键测试结论：");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ✅ 如果此时浏览器无法访问网站 = VPN工作正常（流量被捕获且服务器未响应）");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ❌ 如果此时浏览器仍能访问网站 = 浏览器有双路径（部分流量绕过VPN）");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ");
                }
            }
        }
        
        // 警告：如果发送了很多数据包但没有收到响应
        if (g_packetsSent > 10 && g_responsesReceived == 0) {
            time_t now = time(nullptr);
            if (g_lastResponseTime == 0 || (now - g_lastResponseTime) > 5) {
                NETMANAGER_VPN_LOGE("⚠️ ⚠️ ⚠️ 警告：已发送 %{public}d 个数据包，但未收到任何响应！", g_packetsSent);
                NETMANAGER_VPN_LOGE("⚠️ 可能原因：VPN服务器(127.0.0.1:8888)未运行或未响应");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ⚠️ ⚠️ ⚠️ 警告：已发送 %{public}d 个数据包，但未收到任何响应！", g_packetsSent);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ⚠️ 可能原因：VPN服务器(127.0.0.1:8888)未运行或未响应");
                g_lastResponseTime = now;
            }
        }
    }

    NETMANAGER_VPN_LOGI("=== TUN READ THREAD STOPPED ===");
}
 
void HandleTcpReceived(FdInfo fdInfo)
{
    NETMANAGER_VPN_LOGI("=== UDP RECEIVE THREAD STARTED ===");
    NETMANAGER_VPN_LOGI("tunnelFd: %{public}d, tunFd: %{public}d, server: %{public}s:%{public}d",
                        fdInfo.tunnelFd, fdInfo.tunFd,
                        inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));

    int addrlen = sizeof(struct sockaddr_in);
    uint8_t buffer[BUFFER_SIZE] = {0};
    int responseCount = 0;

    while (g_threadRunF) {
        // 检查文件描述符有效性
        if (fdInfo.tunnelFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunnelFd: %{public}d, stopping receive loop", fdInfo.tunnelFd);
            break;
        }

        int length = recvfrom(fdInfo.tunnelFd, buffer, sizeof(buffer),
            0, reinterpret_cast<struct sockaddr *>(&fdInfo.serverAddr), reinterpret_cast<socklen_t *>(&addrlen));
        if (length < 0) {
            if (errno != EAGAIN) {
                NETMANAGER_VPN_LOGE("read tun device error: %{public}d，tunnelfd: %{public}d", errno, fdInfo.tunnelFd);
            }
            continue;
        }

        responseCount++;
        g_responsesReceived++;
        
        // 🔥 详细记录接收到的响应（IPv4和IPv6）
        if (length >= 20) {
            uint8_t version = (buffer[0] >> 4) & 0x0F;
            if (version == 4) {
                uint8_t protocol = buffer[9];
                char srcIP[16], dstIP[16];
                snprintf(srcIP, sizeof(srcIP), "%d.%d.%d.%d", buffer[12], buffer[13], buffer[14], buffer[15]);
                snprintf(dstIP, sizeof(dstIP), "%d.%d.%d.%d", buffer[16], buffer[17], buffer[18], buffer[19]);
                
                if (protocol == 17 && length >= 28) {  // UDP
                    uint16_t srcPort = (buffer[20] << 8) | buffer[21];
                    uint16_t dstPort = (buffer[22] << 8) | buffer[23];
                    if (srcPort == 53) {
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                     "📥 收到DNS响应: %{public}s:%{public}d -> %{public}s:%{public}d (UDP, %{public}d字节) <- VPN服务器",
                                     srcIP, srcPort, dstIP, dstPort, length);
                    } else {
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                     "📥 收到UDP响应: %{public}s:%{public}d -> %{public}s:%{public}d (%{public}d字节) <- VPN服务器",
                                     srcIP, srcPort, dstIP, dstPort, length);
                    }
                } else if (protocol == 6 && length >= 40) {  // TCP
                    uint16_t srcPort = (buffer[20] << 8) | buffer[21];
                    uint16_t dstPort = (buffer[22] << 8) | buffer[23];
                    const char* serviceType = "";
                    if (srcPort == 80) serviceType = " [HTTP响应]";
                    else if (srcPort == 443) serviceType = " [HTTPS响应]";
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📥 收到TCP响应: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s (%{public}d字节) <- VPN服务器",
                                 srcIP, srcPort, dstIP, dstPort, serviceType, length);
                } else if (protocol == 1) {  // ICMP
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📥 收到ICMP响应: %{public}s -> %{public}s (%{public}d字节) <- VPN服务器",
                                 srcIP, dstIP, length);
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                 "📥 收到响应: 协议=%{public}d %{public}s -> %{public}s (%{public}d字节) <- VPN服务器",
                                 protocol, srcIP, dstIP, length);
                }
            } else if (version == 6 && length >= 40) {  // IPv6
                uint8_t nextHeader = buffer[6];
                char srcIP[40], dstIP[40];
                snprintf(srcIP, sizeof(srcIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15],
                         buffer[16], buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22], buffer[23]);
                snprintf(dstIP, sizeof(dstIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[24], buffer[25], buffer[26], buffer[27], buffer[28], buffer[29], buffer[30], buffer[31],
                         buffer[32], buffer[33], buffer[34], buffer[35], buffer[36], buffer[37], buffer[38], buffer[39]);
                
                const char* protocolName = "";
                if (nextHeader == 6) protocolName = "TCP";
                else if (nextHeader == 17) protocolName = "UDP";
                else if (nextHeader == 58) protocolName = "ICMPv6";
                else protocolName = "其他";
                
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                             "📥 收到IPv6响应: %{public}s -> %{public}s (协议=%{public}s, %{public}d字节) <- VPN服务器",
                             srcIP, dstIP, protocolName, length);
            }
        }
        
        NETMANAGER_VPN_LOGI("📥 RESPONSE #%d: Received %{public}d bytes from server (total responses: %{public}d)", 
                           responseCount, length, g_responsesReceived);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                     "📥 响应统计: 响应 #%{public}d 从VPN服务器收到 %{public}d 字节 (总计响应: %{public}d)",
                     responseCount, length, g_responsesReceived);

        // 接收到udp server的数据，写入到虚拟网卡中
        if (fdInfo.tunFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunFd: %{public}d, stopping write loop", fdInfo.tunFd);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "❌ TUN设备无效: tunFd=%{public}d, 无法写入响应", fdInfo.tunFd);
            break;
        }

        int ret = write(fdInfo.tunFd, buffer, length);
        if (ret <= 0) {
            NETMANAGER_VPN_LOGE("error Write To Tunfd, errno: %{public}d", errno);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "❌ 写入TUN设备失败: 响应 #%{public}d (%{public}d字节), errno=%{public}d",
                         responseCount, length, errno);
        } else {
            NETMANAGER_VPN_LOGI("✅ Wrote %{public}d bytes to TUN device", ret);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                         "✅ 写入TUN成功: 响应 #%{public}d 已写入 %{public}d 字节到TUN设备",
                         responseCount, ret);
        }
    }

    NETMANAGER_VPN_LOGI("=== UDP RECEIVE THREAD STOPPED ===");
}
 
static napi_value UdpConnect(napi_env env, napi_callback_info info)
{
    NETMANAGER_VPN_LOGI("========== UdpConnect() 开始执行 ==========");
    
    size_t argc = 2;
    napi_value args[2] = { nullptr };
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    NETMANAGER_VPN_LOGI("📋 参数检查: argc=%{public}zu", argc);
    if (argc < 2) {
        NETMANAGER_VPN_LOGE("❌ 参数不足: 需要2个参数(IP地址和端口)，实际收到%{public}zu个", argc);
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }

    // 获取IP地址参数
    std::string ipAddr = GetStringFromValueUtf8(env, args[0]);
    NETMANAGER_VPN_LOGI("📋 解析IP地址参数: 原始字符串长度=%{public}zu", ipAddr.length());
    if (ipAddr.empty()) {
        NETMANAGER_VPN_LOGE("❌ IP地址参数为空或无效");
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }
    
    // 获取端口参数
    int32_t port = 0;
    napi_status status = napi_get_value_int32(env, args[1], &port);
    if (status != napi_ok) {
        NETMANAGER_VPN_LOGE("❌ 解析端口参数失败: napi_status=%{public}d", status);
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }

    NETMANAGER_VPN_LOGI("🚀 准备创建UDP连接");
    NETMANAGER_VPN_LOGI("🔍 VPN服务器配置: IP=%{public}s, Port=%{public}d", ipAddr.c_str(), port);
    
    // 验证端口范围
    if (port <= 0 || port > 65535) {
        NETMANAGER_VPN_LOGE("❌ 端口号无效: %{public}d (有效范围: 1-65535)", port);
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }

    // 建立udp隧道 - 步骤1: 创建socket
    NETMANAGER_VPN_LOGI("📡 步骤1: 调用 socket(AF_INET, SOCK_DGRAM, 0)...");
    int32_t sockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd == -1) {
        int errno_save = errno;
        NETMANAGER_VPN_LOGE("❌ socket() 创建失败");
        NETMANAGER_VPN_LOGE("❌ 错误码: errno=%{public}d", errno_save);
        NETMANAGER_VPN_LOGE("❌ 错误描述: %{public}s", strerror(errno_save));
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }
    
    NETMANAGER_VPN_LOGI("✅ 步骤1完成: UDP socket创建成功, fd=%{public}d", sockFd);

    // 步骤2: 设置socket选项（超时）
    NETMANAGER_VPN_LOGI("⏰ 步骤2: 设置socket接收超时...");
    struct timeval timeout = {1, 0};
    int setsockopt_result = setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, 
                                       reinterpret_cast<const char*>(&timeout), sizeof(struct timeval));
    if (setsockopt_result == -1) {
        int errno_save = errno;
        NETMANAGER_VPN_LOGE("⚠️ setsockopt(SO_RCVTIMEO) 失败: errno=%{public}d, %{public}s", errno_save, strerror(errno_save));
        NETMANAGER_VPN_LOGI("⚠️ 继续执行，超时设置失败不影响UDP连接");
    } else {
        NETMANAGER_VPN_LOGI("✅ 步骤2完成: Socket接收超时已设置为1秒");
    }

    // 步骤3: 配置服务器地址
    NETMANAGER_VPN_LOGI("🌐 步骤3: 配置VPN服务器地址...");
    memset(&g_fdInfo.serverAddr, 0, sizeof(g_fdInfo.serverAddr));
    g_fdInfo.serverAddr.sin_family = AF_INET;
    
    // 转换IP地址字符串为网络字节序
    in_addr_t addr = inet_addr(ipAddr.c_str());
    if (addr == INADDR_NONE) {
        NETMANAGER_VPN_LOGE("❌ IP地址转换失败: %{public}s", ipAddr.c_str());
        NETMANAGER_VPN_LOGE("❌ inet_addr() 返回 INADDR_NONE，IP地址格式无效");
        NETMANAGER_VPN_LOGE("❌ 有效格式示例: 127.0.0.1, 192.168.1.1, 10.20.23.147");
        close(sockFd);
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }
    
    g_fdInfo.serverAddr.sin_addr.s_addr = addr;
    g_fdInfo.serverAddr.sin_port = htons(port);
    
    NETMANAGER_VPN_LOGI("✅ 步骤3完成: 服务器地址配置成功");
    NETMANAGER_VPN_LOGI("🔗 服务器地址详情:");
    NETMANAGER_VPN_LOGI("   - IP字符串: %{public}s", ipAddr.c_str());
    NETMANAGER_VPN_LOGI("   - 网络字节序IP: %{public}s", inet_ntoa(g_fdInfo.serverAddr.sin_addr));
    NETMANAGER_VPN_LOGI("   - 端口(主机序): %{public}d", port);
    NETMANAGER_VPN_LOGI("   - 端口(网络序): %{public}d", ntohs(g_fdInfo.serverAddr.sin_port));

    // 步骤4: 创建返回值
    NETMANAGER_VPN_LOGI("📦 步骤4: 创建NAPI返回值...");
    napi_value tunnelFd;
    status = napi_create_int32(env, sockFd, &tunnelFd);
    if (status != napi_ok) {
        NETMANAGER_VPN_LOGE("❌ napi_create_int32() 失败: napi_status=%{public}d", status);
        close(sockFd);
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }
    
    NETMANAGER_VPN_LOGI("✅ 步骤4完成: NAPI返回值已创建");
    NETMANAGER_VPN_LOGI("✅ ========== UdpConnect() 执行成功 ==========");
    NETMANAGER_VPN_LOGI("✅ 返回tunnelFd: %{public}d", sockFd);
    NETMANAGER_VPN_LOGI("📝 注意: UDP是无连接协议，socket已创建但未实际连接");
    NETMANAGER_VPN_LOGI("📝 后续将通过 sendto() 发送数据包到服务器");
    
    return tunnelFd;
}
 
static napi_value StartVpn(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_get_value_int32(env, args[0], &g_fdInfo.tunFd);
    napi_get_value_int32(env, args[1], &g_fdInfo.tunnelFd);

    if (g_threadRunF) {
        g_threadRunF = false;
        g_threadT1.join();
        g_threadT2.join();
    }
 
    // 启动两个线程, 一个处理读取虚拟网卡的数据，另一个接收服务端的数据
    g_threadRunF = true;
    std::thread tt1(HandleReadTunfd, g_fdInfo);
    std::thread tt2(HandleTcpReceived, g_fdInfo);

    g_threadT1 = std::move(tt1);
    g_threadT2 = std::move(tt2);

    napi_value retValue;
    napi_create_int32(env, 0, &retValue);
    return retValue;
}

static napi_value StopVpn(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int32_t tunnelFd;
    napi_get_value_int32(env, args[0], &tunnelFd);
    if (tunnelFd) {
        close(tunnelFd);
        tunnelFd = 0;
    }
 
    // 停止两个线程
    if (g_threadRunF) {
        g_threadRunF = false;
        g_threadT1.join();
        g_threadT2.join();
    }
 
    NETMANAGER_VPN_LOGI("[ZBQ] StopVpn successful");
 
    napi_value retValue;
    napi_create_int32(env, 0, &retValue);
    return retValue;
}
 
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"udpConnect", nullptr, UdpConnect, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startVpn", nullptr, StartVpn, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopVpn", nullptr, StopVpn, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END
 
static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void *)0),
    .reserved = {0},
};
 
extern "C" __attribute__((constructor)) void RegisterEntryModule(void) { napi_module_register(&demoModule); }
// [End vpn_control_case_c++]
