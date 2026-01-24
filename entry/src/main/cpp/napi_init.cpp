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

// 统一的VPN客户端日志宏
#define VPN_CLIENT_LOGI(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] " fmt, ##__VA_ARGS__)

#define VPN_CLIENT_LOGE(fmt, ...) \
    OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, "ZHOUB", "[VPN客户端] ❌ " fmt, ##__VA_ARGS__)

// 保留原有宏用于兼容性，但重定向到新的统一宏
#define NETMANAGER_VPN_LOGE VPN_CLIENT_LOGE
#define NETMANAGER_VPN_LOGI VPN_CLIENT_LOGI
#define NETMANAGER_VPN_LOGD VPN_CLIENT_LOGI

struct FdInfo {
    int32_t tunFd = 0;
    int32_t tunnelFd = 0;
    struct sockaddr_in serverAddr;
};

static FdInfo g_fdInfo;
static bool g_threadRunF = false;
static std::thread g_threadT1;
static std::thread g_threadT2;

 
static std::atomic<int> g_packetsSent{0};  // 发送的数据包计数
static std::atomic<int> g_responsesReceived{0};  // 接收的响应计数
static std::atomic<time_t> g_lastResponseTime{0};  // 最后一次收到响应的时间
static std::atomic<int> g_ipv4Packets{0};  // IPv4数据包计数
static std::atomic<int> g_ipv6Packets{0};  // IPv6数据包计数
static std::atomic<int> g_ipv4TcpPackets{0};  // IPv4 TCP数据包计数
static std::atomic<int> g_ipv6TcpPackets{0};  // IPv6 TCP数据包计数
static std::atomic<int> g_httpPackets{0};  // HTTP数据包计数 (端口80)
static std::atomic<int> g_httpsPackets{0};  // HTTPS数据包计数 (端口443)
static std::atomic<int> g_detailedLogCount{0};  // 详细日志计数器（仅记录前20个HTTP/HTTPS连接）
static std::atomic<int> g_packetsReadFromTun{0};  // 从TUN读取的数据包总数
static std::atomic<int> g_packetsForwarded{0};  // 成功转发的数据包总数
static std::atomic<int> g_packetsDropped{0};  // 被丢弃的数据包总数（读取失败、发送失败等）
static std::atomic<int> g_packetsSendFailed{0};  // 发送失败的数据包数
static std::atomic<time_t> g_vpnStartTime{0};  // VPN启动时间
static std::atomic<int> g_trafficCheckInterval{0};  // 流量检查间隔计数器


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

// 前置声明：C++ 在函数定义前使用时必须先声明
void ProtectForwardingSocketNative(int sockFd);

// 🎯 处理控制消息
void HandleControlMessage(const uint8_t* buffer, int length)
{
    VPN_CLIENT_LOGI("🎯 处理控制消息: 长度=%{public}d字节", length);

    // 控制消息格式：IP头(20字节) + UDP头(8字节) + Payload
    if (length < 28) {
        VPN_CLIENT_LOGE("❌ 控制消息太短: %{public}d字节 (最小28字节)", length);
        return;
    }

    // 提取payload：跳过IP头(20字节)和UDP头(8字节)
    const uint8_t* payload = buffer + 20 + 8;
    int payloadLength = length - 20 - 8;

    if (payloadLength < 1) {
        VPN_CLIENT_LOGE("❌ 控制消息payload为空");
        return;
    }

    // 解析控制命令
    uint8_t commandType = payload[0];
    VPN_CLIENT_LOGI("🔍 控制命令类型: 0x%{public}x", commandType);

    switch (commandType) {
        case 0x01: {  // 保护转发socket (PROTECT_FORWARDING_SOCKET)
            if (payloadLength < 5) {  // 1字节命令 + 4字节socket FD
                VPN_CLIENT_LOGE("❌ 保护socket命令参数不足: %{public}d字节 (需要5字节)", payloadLength);
                return;
            }

            // 解析socket FD (大端字节序)
            int32_t sockFd = (payload[1] << 24) | (payload[2] << 16) | (payload[3] << 8) | payload[4];
            VPN_CLIENT_LOGI("🛡️ 收到保护转发socket请求: fd=%{public}d", sockFd);

            // 调用ETS层的保护方法
            ProtectForwardingSocketNative(sockFd);
            break;
        }

        default:
            VPN_CLIENT_LOGE("❌ 未知控制命令类型: 0x%{public}x", commandType);
            break;
    }
}

// 🎯 全局变量：待保护的socket队列
#include <queue>
#include <mutex>
std::queue<int> g_socketsToProtect;
std::mutex g_socketProtectMutex;

// 🎯 添加socket到保护队列
void QueueSocketForProtection(int sockFd)
{
    std::lock_guard<std::mutex> lock(g_socketProtectMutex);
    g_socketsToProtect.push(sockFd);
    VPN_CLIENT_LOGI("📋 已将socket添加到保护队列: fd=%{public}d (队列大小=%{public}zu)",
                   sockFd, g_socketsToProtect.size());
}

// 🎯 获取下一个待保护的socket (供ETS层调用)
int GetNextSocketToProtect()
{
    std::lock_guard<std::mutex> lock(g_socketProtectMutex);
    if (g_socketsToProtect.empty()) {
        return -1;
    }
    int sockFd = g_socketsToProtect.front();
    g_socketsToProtect.pop();
    return sockFd;
}

// 🎯 调用ETS层的socket保护方法
void ProtectForwardingSocketNative(int sockFd)
{
    VPN_CLIENT_LOGI("🔄 将socket加入保护队列: fd=%{public}d", sockFd);
    QueueSocketForProtection(sockFd);
}

void HandleReadTunfd(FdInfo fdInfo)
{
    NETMANAGER_VPN_LOGI("=== TUN READ THREAD STARTED ===");
    VPN_CLIENT_LOGI("[VPN客户端] TUN读取线程已启动");
    NETMANAGER_VPN_LOGI("tunFd: %{public}d, tunnelFd: %{public}d, server: %{public}s:%{public}d",
                        fdInfo.tunFd, fdInfo.tunnelFd,
                        inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
    VPN_CLIENT_LOGI("TUN read thread started: tunFd=%{public}d tunnelFd=%{public}d server=%{public}s:%{public}d",
                 fdInfo.tunFd, fdInfo.tunnelFd,
                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
    
    // 🔥 记录VPN启动时间，用于流量劫持检查
    g_vpnStartTime = time(nullptr);
    VPN_CLIENT_LOGI("[VPN客户端] VPN启动时间: %{public}lld, 开始监控流量劫持情况", (long long)g_vpnStartTime.load());

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
                VPN_CLIENT_LOGE("❌ TUN读取失败: errno=%{public}d, 数据包被丢弃", errno);
                g_packetsDropped.fetch_add(1);
            }
            continue;
        }

        packetCount++;
        g_packetsReadFromTun.fetch_add(1);  // 统计从TUN读取的数据包
        
        // 🔥 精简日志：只在每10个数据包或前5个数据包打印详细信息
        if (packetCount <= 5 || packetCount % 10 == 0) {
            VPN_CLIENT_LOGI("Packet #%{public}d: %{public}d bytes -> server",
                         packetCount, readResult);
        }
        
        // 解析IP版本
        if (readResult >= 1) {
            uint8_t version = (buffer[0] >> 4) & 0x0F;
            
            if (version == 4 && readResult >= 20) {  // IPv4
                g_ipv4Packets.fetch_add(1);
                uint8_t protocol = buffer[9];
                
                // 提取源IP和目标IP
                char srcIP[16], dstIP[16];
                snprintf(srcIP, sizeof(srcIP), "%d.%d.%d.%d", buffer[12], buffer[13], buffer[14], buffer[15]);
                snprintf(dstIP, sizeof(dstIP), "%d.%d.%d.%d", buffer[16], buffer[17], buffer[18], buffer[19]);
                
                if (protocol == 6) {  // TCP
                    g_ipv4TcpPackets.fetch_add(1);
                    if (readResult >= 40) {
                        // ✅ 修复：正确处理网络字节序
                        uint16_t srcPort = ntohs(*(uint16_t*)&buffer[20]);
                        uint16_t dstPort = ntohs(*(uint16_t*)&buffer[22]);
                        
                        // 🔥 ZHOUB日志：TUN设备转发到代理服务器前
                        VPN_CLIENT_LOGI("[TUN->Proxy] src=%{public}s:%{public}d dst=%{public}s:%{public}d proto=TCP size=%{public}d",
                                     srcIP, srcPort, dstIP, dstPort, readResult);
                        
                        // 追踪HTTP/HTTPS流量
                        const char* serviceLabel = "";
                        bool isHttpTraffic = false;
                        if (dstPort == 80) {
                            g_httpPackets.fetch_add(1);
                            serviceLabel = " [HTTP浏览器流量]";
                            isHttpTraffic = true;
                        } else if (dstPort == 443) {
                            g_httpsPackets.fetch_add(1);
                            serviceLabel = " [HTTPS浏览器流量]";
                            isHttpTraffic = true;
                        }
                        
                        // 🔥 精简日志：只记录前5个HTTP/HTTPS连接
                        if (isHttpTraffic && g_detailedLogCount < 5) {
                            g_detailedLogCount.fetch_add(1);
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                         "🌐 %{public}s: %{public}s:%{public}d -> %{public}s:%{public}d",
                                         serviceLabel, srcIP, srcPort, dstIP, dstPort);
                        }
                        
                        // 🔥 记录所有TCP连接（包括HTTP/HTTPS）
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                     "📊 TCP数据包 #%{public}d: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s (大小=%{public}d字节)",
                                     packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel, readResult);
                        
                        NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 TCP %s:%d -> %s:%d%s", 
                                           packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel);
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                     "[VPN客户端] 📊 数据包 #%{public}d: IPv4 TCP %{public}s:%{public}d -> %{public}s:%{public}d%{public}s",
                                     packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel);
                    }
                } else                 if (protocol == 17) {  // UDP
                    if (readResult >= 28) {
                        // ✅ 修复：正确处理网络字节序
                        uint16_t srcPort = ntohs(*(uint16_t*)&buffer[20]);
                        uint16_t dstPort = ntohs(*(uint16_t*)&buffer[22]);

                        // 🔥🔥🔥 控制消息识别：目的IP=127.0.0.1 且 目的端口=0
                        if (strcmp(dstIP, "127.0.0.1") == 0 && dstPort == 0) {
                            // 🎯 这是控制消息！不转发到服务器，而是本地处理
                            VPN_CLIENT_LOGI("🎯 检测到控制消息: src=%{public}s:%{public}d dst=%{public}s:%{public}d size=%{public}d",
                                         srcIP, srcPort, dstIP, dstPort, readResult);

                            // 处理控制消息
                            HandleControlMessage(buffer, readResult);
                            continue;  // 跳过转发，控制消息不发送到服务器
                        }

                        // 🔥 ZHOUB日志：TUN设备转发到代理服务器前
                        VPN_CLIENT_LOGI("[TUN->Proxy] src=%{public}s:%{public}d dst=%{public}s:%{public}d proto=UDP size=%{public}d",
                                     srcIP, srcPort, dstIP, dstPort, readResult);

                        // 🔥 详细记录DNS查询（UDP端口53）
                        if (dstPort == 53) {
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                                         "🔍 DNS查询请求: %{public}s:%{public}d -> %{public}s:%{public}d (UDP, 大小=%{public}d字节)",
                                         srcIP, srcPort, dstIP, dstPort, readResult);
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                                         "📤 DNS查询将通过UDP隧道转发到VPN服务器");
                        } else {
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                                         "📦 UDP数据包: %{public}s:%{public}d -> %{public}s:%{public}d (大小=%{public}d字节)",
                                         srcIP, srcPort, dstIP, dstPort, readResult);
                        }
                    }
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 UDP %s -> %s", packetCount, srcIP, dstIP);
                } else if (protocol == 1) {  // ICMP
                    // 🔥 ZHOUB日志：TUN设备转发到代理服务器前
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "[TUN→代理] 源IP:%{public}s 目的IP:%{public}s 源端口:0 目的端口:0 协议:ICMP 大小:%{public}d字节",
                                 srcIP, dstIP, readResult);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "🏓 ICMP数据包: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 ICMP %s -> %s", packetCount, srcIP, dstIP);
                } else {
                    // 🔥 ZHOUB日志：TUN设备转发到代理服务器前
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "[TUN→代理] 源IP:%{public}s 目的IP:%{public}s 源端口:0 目的端口:0 协议:%{public}d 大小:%{public}d字节",
                                 srcIP, dstIP, protocol, readResult);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📦 其他协议数据包: 协议=%{public}d %{public}s -> %{public}s (大小=%{public}d字节)",
                                 protocol, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 protocol=%d %s -> %s", packetCount, protocol, srcIP, dstIP);
                }
            } else if (version == 6 && readResult >= 40) {  // IPv6
                g_ipv6Packets.fetch_add(1);
                uint8_t nextHeader = buffer[6];
                
                char srcIP[40], dstIP[40];
                snprintf(srcIP, sizeof(srcIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15],
                         buffer[16], buffer[17], buffer[18], buffer[19], buffer[20], buffer[21], buffer[22], buffer[23]);
                snprintf(dstIP, sizeof(dstIP), "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                         buffer[24], buffer[25], buffer[26], buffer[27], buffer[28], buffer[29], buffer[30], buffer[31],
                         buffer[32], buffer[33], buffer[34], buffer[35], buffer[36], buffer[37], buffer[38], buffer[39]);
                
                if (nextHeader == 6) {  // TCP
                    g_ipv6TcpPackets.fetch_add(1);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📊 IPv6 TCP数据包 #%{public}d: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 TCP %s -> %s", packetCount, srcIP, dstIP);
                } else if (nextHeader == 17) {  // UDP
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📊 IPv6 UDP数据包 #%{public}d: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 UDP %s -> %s", packetCount, srcIP, dstIP);
                } else if (nextHeader == 58) {  // ICMPv6
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📊 IPv6 ICMPv6数据包 #%{public}d: %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 ICMPv6 %s -> %s", packetCount, srcIP, dstIP);
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📊 IPv6其他协议数据包 #%{public}d: 协议=%{public}d %{public}s -> %{public}s (大小=%{public}d字节)",
                                 packetCount, nextHeader, srcIP, dstIP, readResult);
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 nextHeader=%d %s -> %s", packetCount, nextHeader, srcIP, dstIP);
                }
            } else {
                // 数据包太小或版本未知，但仍然会被转发
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ 未知/异常数据包 #%{public}d: IP版本=%{public}d (大小=%{public}d字节) - 仍将转发",
                             packetCount, version, readResult);
                NETMANAGER_VPN_LOGI("📊 PACKET #%d: Unknown IP version %d", packetCount, version);
            }
        } else {
            // readResult < 1 的情况（理论上不会到这里，因为上面已经检查了readResult <= 0）
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "⚠️ 异常数据包 #%{public}d: 大小=%{public}d字节 (< 1字节) - 仍将转发",
                         packetCount, readResult);
        }

        // 读取到虚拟网卡的数据，通过udp隧道，发送给服务器
        if (fdInfo.tunnelFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunnelFd: %{public}d, stopping send loop", fdInfo.tunnelFd);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "❌ 隧道FD无效: tunnelFd=%{public}d, 数据包 #%{public}d 无法转发，线程退出",
                         fdInfo.tunnelFd, packetCount);
            g_packetsDropped.fetch_add(1);
            break;
        }
        
        // 🔥 确保所有数据包都被转发（无论大小、版本、协议）
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                     "🔄 准备转发数据包 #%{public}d: 大小=%{public}d字节 -> VPN服务器 %{public}s:%{public}d",
                     packetCount, readResult, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
        
        // 🔥 转发前检查：隧道FD和服务器地址有效性
        if (fdInfo.tunnelFd < 0) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "❌ [转发验证] 隧道FD无效: %{public}d，无法转发数据包", fdInfo.tunnelFd);
        }
        if (fdInfo.serverAddr.sin_addr.s_addr == 0) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
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
                    // ✅ 修复：正确处理网络字节序
                    uint16_t srcPort = ntohs(*(uint16_t*)&buffer[20]);
                    uint16_t dstPort = ntohs(*(uint16_t*)&buffer[22]);
                    if (dstPort == 53) {
                        VPN_CLIENT_LOGI("🚀 转发DNS查询: %{public}s:%{public}d -> %{public}s:%{public}d (UDP, %{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                     srcIP, srcPort, dstIP, dstPort, readResult,
                                     inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                    } else {
                        VPN_CLIENT_LOGI("🚀 转发UDP请求: %{public}s:%{public}d -> %{public}s:%{public}d (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                     srcIP, srcPort, dstIP, dstPort, readResult,
                                     inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                    }
                } else if (protocol == 6 && readResult >= 40) {  // TCP
                    // ✅ 修复：正确处理网络字节序
                    uint16_t srcPort = ntohs(*(uint16_t*)&buffer[20]);
                    uint16_t dstPort = ntohs(*(uint16_t*)&buffer[22]);
                    const char* serviceType = "";
                    if (dstPort == 80) serviceType = " [HTTP]";
                    else if (dstPort == 443) serviceType = " [HTTPS]";
                            VPN_CLIENT_LOGI("🚀 转发TCP请求: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                 srcIP, srcPort, dstIP, dstPort, serviceType, readResult,
                                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                } else if (protocol == 1) {  // ICMP
                        VPN_CLIENT_LOGI("🚀 转发ICMP数据包: %{public}s -> %{public}s (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                 srcIP, dstIP, readResult,
                                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "🚀 转发数据包: 协议=%{public}d %{public}s -> %{public}s (%{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                                 protocol, srcIP, dstIP, readResult,
                                 inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                }
            } else if (version == 6 && readResult >= 40) {  // IPv6
                uint8_t nextHeader = buffer[6];
                char srcIP[INET6_ADDRSTRLEN] = {0}, dstIP[INET6_ADDRSTRLEN] = {0};

                // 🐛 修复：使用inet_ntop正确格式化IPv6地址
                struct in6_addr srcAddr6, dstAddr6;
                memcpy(&srcAddr6, &buffer[8], 16);
                memcpy(&dstAddr6, &buffer[24], 16);

                if (inet_ntop(AF_INET6, &srcAddr6, srcIP, sizeof(srcIP)) == nullptr) {
                    snprintf(srcIP, sizeof(srcIP), "IPv6格式错误");
                }
                if (inet_ntop(AF_INET6, &dstAddr6, dstIP, sizeof(dstIP)) == nullptr) {
                    snprintf(dstIP, sizeof(dstIP), "IPv6格式错误");
                }
                
                const char* protocolName = "";
                if (nextHeader == 6) protocolName = "TCP";
                else if (nextHeader == 17) protocolName = "UDP";
                else if (nextHeader == 58) protocolName = "ICMPv6";
                else protocolName = "其他";
                
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "🚀 转发IPv6数据包: %{public}s -> %{public}s (协议=%{public}s, %{public}d字节) -> VPN服务器 %{public}s:%{public}d",
                             srcIP, dstIP, protocolName, readResult,
                             inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
            } else {
                // 数据包太小（< 20字节），无法解析，但仍会转发
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "🚀 转发异常数据包: 大小=%{public}d字节 (< 20字节，无法解析) -> VPN服务器 %{public}s:%{public}d",
                             readResult, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
            }
        } else {
            // readResult < 20，数据包太小，但仍会转发
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
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
            g_packetsSendFailed.fetch_add(1);
            
            // 🔥 精简日志：转发失败时只打印一条错误信息
            VPN_CLIENT_LOGE("❌ 转发失败 #%{public}d: errno=%{public}d (%{public}s)",
                         packetCount, errno_save, strerror(errno_save));
            continue;
        }

        g_packetsSent.fetch_add(1);
        g_packetsForwarded.fetch_add(1);  // 统计成功转发的数据包
        
        // 🔥 精简日志：只在每10个数据包或前5个数据包打印成功信息
        if (packetCount <= 5 || packetCount % 10 == 0) {
            VPN_CLIENT_LOGI("✅ 转发成功 #%{public}d: %{public}d字节",
                         packetCount, sendResult);
        }
        
        // 🔥 精简日志：每50个数据包输出一次统计信息
        if (g_packetsSent.load() % 50 == 0) {
            g_trafficCheckInterval.fetch_add(1);
            VPN_CLIENT_LOGI("📊 统计: 发送=%{public}d, 成功=%{public}d, 失败=%{public}d",
                         g_packetsSent.load(), g_packetsForwarded.load(), g_packetsSendFailed.load());
            
            // 🔥 流量劫持完整性检查
            time_t currentTime = time(nullptr);
            time_t vpnUptime = currentTime - g_vpnStartTime;
            VPN_CLIENT_LOGI("🔍 [流量劫持检查] ========== 第%{public}d次检查 (VPN运行时间: %{public}lld秒) ==========",
                         g_trafficCheckInterval.load(), (long long)vpnUptime);
            
            // 检查1: 是否有TCP流量（浏览器等应用）
            if (g_ipv4TcpPackets.load() == 0 && g_ipv6TcpPackets.load() == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️⚠️⚠️ [流量劫持检查] 警告：没有检测到TCP数据包！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [流量劫持检查] 可能原因：");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   1. VPN路由表未生效 - 流量未进入TUN设备");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   2. 应用流量绕过VPN - 可能使用了trustedApplications");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   3. 系统路由表配置错误 - 默认路由未指向vpn-tun");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   4. VPN连接未正确建立 - vpnConnection.create()可能失败");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "🔍 [流量劫持检查] 建议：检查VPN扩展能力日志，确认vpnConnection.create()是否成功");
            } else {
                VPN_CLIENT_LOGI("✅ [流量劫持检查] 检测到TCP流量：IPv4 TCP=%{public}d, IPv6 TCP=%{public}d",
                             g_ipv4TcpPackets.load(), g_ipv6TcpPackets.load());
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [流量劫持检查] 说明：VPN路由表已生效，部分流量已进入TUN设备");
            }
            
            // 检查2: 是否有UDP流量（DNS等）
            int totalUdpPackets = g_ipv4Packets - g_ipv4TcpPackets + (g_ipv6Packets - g_ipv6TcpPackets);
            if (totalUdpPackets == 0 && g_ipv4TcpPackets.load() == 0 && g_ipv6TcpPackets.load() == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️⚠️⚠️ [流量劫持检查] 严重警告：完全没有检测到任何流量！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [流量劫持检查] 这意味着：");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   - 所有应用流量都绕过了VPN");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   - 或者VPN路由表完全未生效");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   - 或者TUN设备未正确创建");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [流量劫持检查] 检测到UDP流量：总计约%{public}d个UDP数据包",
                             totalUdpPackets);
            }
            
            // 检查3: HTTP/HTTPS流量检查
            if (g_httpPackets == 0 && g_httpsPackets == 0 && (g_ipv4TcpPackets > 0 || g_ipv6TcpPackets > 0)) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [流量劫持检查] 有TCP流量但无HTTP/HTTPS流量（可能使用其他端口）");
            } else if (g_httpPackets > 0 || g_httpsPackets > 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [流量劫持检查] 检测到浏览器流量：HTTP=%{public}d, HTTPS=%{public}d",
                             g_httpPackets.load(), g_httpsPackets.load());
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [流量劫持检查] 说明：浏览器流量已被VPN成功劫持到TUN设备");
            }
            
            // 检查4: 流量劫持完整性评估
            if (vpnUptime > 10) {  // VPN运行超过10秒
                if (g_ipv4TcpPackets.load() == 0 && g_ipv6TcpPackets.load() == 0) {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "❌ [流量劫持检查] 结论：VPN运行%{public}lld秒，但未检测到TCP流量，流量劫持可能失败",
                                 (long long)vpnUptime);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "❌ [流量劫持检查] 建议：检查VPN配置，确认trustedApplications为空，blockedApplications为空");
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "✅ [流量劫持检查] 结论：VPN运行%{public}lld秒，已检测到流量，流量劫持正常工作",
                                 (long long)vpnUptime);
                }
            }
            
            // 🔥 检查5: 转发到代理服务器的完整性验证
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "🔍 [转发验证] ========== 转发到代理服务器验证 ==========");
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "🔍 [转发验证] 代理服务器: %{public}s:%{public}d",
                         inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "🔍 [转发验证] 隧道FD: %{public}d (有效=%{public}s)",
                         fdInfo.tunnelFd, fdInfo.tunnelFd >= 0 ? "是" : "否");
            
            // 转发统计
            int totalProcessed = g_packetsForwarded.load() + g_packetsSendFailed.load();
            double forwardSuccessRate = 0.0;
            if (g_packetsReadFromTun.load() > 0) {
                forwardSuccessRate = (double)g_packetsForwarded.load() * 100.0 / (double)g_packetsReadFromTun.load();
            }
            
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                         "🔍 [转发验证] 从TUN读取: %{public}d 个数据包", g_packetsReadFromTun.load());
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                         "🔍 [转发验证] 成功转发: %{public}d 个数据包", g_packetsForwarded.load());
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                         "🔍 [转发验证] 转发失败: %{public}d 个数据包", g_packetsSendFailed.load());
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "🔍 [转发验证] 转发成功率: %.2f%%", forwardSuccessRate);
            
            // 转发完整性检查
            if (g_packetsReadFromTun.load() == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 警告：没有从TUN读取任何数据包，无法验证转发");
            } else if (g_packetsForwarded.load() == 0 && g_packetsSendFailed.load() > 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "❌ [转发验证] 严重错误：所有数据包转发都失败！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "❌ [转发验证] 可能原因：");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   1. 代理服务器未运行或未监听 %{public}s:%{public}d",
                             inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port));
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   2. 网络连接问题 - 无法连接到代理服务器");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   3. 隧道FD无效 - tunnelFd=%{public}d", fdInfo.tunnelFd);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "   4. UDP socket发送失败 - 检查errno错误码");
            } else if (g_packetsForwarded.load() == 0 && g_packetsSendFailed.load() == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 警告：读取了%{public}d个数据包，但没有任何转发尝试",
                             g_packetsReadFromTun.load());
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 可能原因：转发逻辑未执行或提前退出");
            } else if (forwardSuccessRate < 50.0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 警告：转发成功率较低 (%.2f%%)，可能存在问题",
                             forwardSuccessRate);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 建议：检查网络连接和代理服务器状态");
            } else if (forwardSuccessRate >= 99.0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [转发验证] 转发成功率优秀 (%.2f%%)，转发工作正常",
                             forwardSuccessRate);
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [转发验证] 转发成功率良好 (%.2f%%)，转发基本正常",
                             forwardSuccessRate);
            }
            
            // 检查是否有数据包遗漏
            if (g_packetsReadFromTun.load() != totalProcessed) {
                int missing = g_packetsReadFromTun.load() - totalProcessed;
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️⚠️⚠️ [转发验证] 警告：有%{public}d个数据包未被处理！",
                             missing);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 读取=%{public}d, 已处理=%{public}d (成功=%{public}d + 失败=%{public}d)",
                             g_packetsReadFromTun.load(), totalProcessed, g_packetsForwarded.load(), g_packetsSendFailed.load());
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 这些数据包可能被遗漏，未转发到代理服务器");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [转发验证] 完整性验证通过：所有读取的数据包都已尝试转发");
            }
            
            // 检查响应情况
            if (g_packetsForwarded.load() > 0 && g_responsesReceived.load() == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 警告：已转发%{public}d个数据包，但未收到任何响应",
                             g_packetsForwarded.load());
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "⚠️ [转发验证] 可能原因：代理服务器未响应或响应处理有问题");
            } else if (g_packetsForwarded.load() > 0 && g_responsesReceived.load() > 0) {
                double responseRate = (double)g_responsesReceived.load() * 100.0 / (double)g_packetsForwarded.load();
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [转发验证] 收到响应: %{public}d个响应 (响应率: %.2f%%)",
                             g_responsesReceived.load(), responseRate);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "✅ [转发验证] 说明：代理服务器正常工作，双向通信正常");
            }
            
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "🔍 [转发验证] ==========================================");
            
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "🔍 [流量劫持检查] ==========================================");
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] 🔍 转发完整性检查: 从TUN读取=%{public}d, 成功转发=%{public}d, 转发失败=%{public}d, 丢弃=%{public}d",
                         g_packetsReadFromTun.load(), g_packetsForwarded.load(), g_packetsSendFailed.load(), g_packetsDropped.load());
            
            // 🔥 关键检查：确保所有读取的数据包都被转发
            // 关系：g_packetsReadFromTun = g_packetsForwarded + g_packetsSendFailed + g_packetsDropped
            // 注意：totalProcessed已在上面定义，这里直接使用
            if (g_packetsReadFromTun.load() != totalProcessed) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "[VPN客户端] ⚠️⚠️⚠️ 警告：转发不完整！读取=%{public}d, 已处理=%{public}d (成功=%{public}d + 失败=%{public}d), 差异=%{public}d",
                             g_packetsReadFromTun.load(), totalProcessed, g_packetsForwarded.load(), g_packetsSendFailed.load(), 
                             g_packetsReadFromTun - totalProcessed);
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "[VPN客户端] ⚠️ 可能有数据包在读取后、发送前被丢弃或遗漏！");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                             "[VPN客户端] ✅ 转发完整性验证通过：所有从TUN读取的数据包都已处理（成功转发或发送失败）");
            }
            
            if (g_ipv4TcpPackets.load() == 0 && g_ipv6TcpPackets.load() == 0) {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ⚠️ ⚠️ ⚠️ 警告：没有检测到TCP数据包（浏览器流量）！");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ⚠️ 说明：VPN路由表未生效，浏览器流量未进入VPN隧道");
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ⚠️ 此时浏览器能访问网站 = 正常（流量走物理网络）");
            } else {
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ✅ 检测到TCP数据包，VPN路由表已生效，流量进入VPN隧道！");
                
                if (g_httpPackets == 0 && g_httpsPackets == 0) {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ⚠️ 但是没有检测到HTTP/HTTPS流量（可能使用其他端口或协议）");
                } else {
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ✅✅✅ 已捕获 %{public}d 个HTTP/HTTPS连接！", g_httpPackets + g_httpsPackets);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] 🔥 关键测试结论：");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ✅ 如果此时浏览器无法访问网站 = VPN工作正常（流量被捕获且服务器未响应）");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ❌ 如果此时浏览器仍能访问网站 = 浏览器有双路径（部分流量绕过VPN）");
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", "[VPN客户端] ");
                }
            }
        }
        
        // 警告：如果发送了很多数据包但没有收到响应
        if (g_packetsSent.load() > 10 && g_responsesReceived.load() == 0) {
            time_t now = time(nullptr);
            if (g_lastResponseTime == 0 || (now - g_lastResponseTime) > 5) {
                const char* serverIp = inet_ntoa(fdInfo.serverAddr.sin_addr);
                uint16_t serverPort = ntohs(fdInfo.serverAddr.sin_port);
                NETMANAGER_VPN_LOGE("⚠️ ⚠️ ⚠️ 警告：已发送 %{public}d 个数据包，但未收到任何响应！",
                                   g_packetsSent.load());
                NETMANAGER_VPN_LOGE("⚠️ 可能原因：VPN服务器(%{public}s:%{public}u)未运行或未响应",
                                   serverIp, static_cast<unsigned>(serverPort));
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                             "[VPN客户端] ⚠️ ⚠️ ⚠️ 警告：已发送 %{public}d 个数据包，但未收到任何响应！",
                             g_packetsSent.load());
                OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                             "[VPN客户端] ⚠️ 可能原因：VPN服务器(%{public}s:%{public}u)未运行或未响应",
                             serverIp, static_cast<unsigned>(serverPort));
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

    socklen_t addrlen = sizeof(struct sockaddr_in);
    uint8_t buffer[BUFFER_SIZE] = {0};
    int responseCount = 0;

    while (g_threadRunF) {
        // 检查文件描述符有效性
        if (fdInfo.tunnelFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunnelFd: %{public}d, stopping receive loop", fdInfo.tunnelFd);
            break;
        }

        // 🐛 修复：recvfrom的addrlen参数应该是socklen_t*，不是socklen_t**
        ssize_t length = recvfrom(fdInfo.tunnelFd, buffer, sizeof(buffer),
            0, reinterpret_cast<struct sockaddr *>(&fdInfo.serverAddr), &addrlen);
        if (length < 0) {
            if (errno != EAGAIN) {
                NETMANAGER_VPN_LOGE("read tun device error: %{public}d，tunnelfd: %{public}d", errno, fdInfo.tunnelFd);
            }
            continue;
        }

        responseCount++;
        g_responsesReceived.fetch_add(1);
        
        // 🔥 详细记录接收到的响应（IPv4和IPv6）
        char srcIP[40] = {0}, dstIP[40] = {0};
        uint16_t srcPort = 0, dstPort = 0;
        uint8_t protocol = 0;
        const char* protocolName = "未知";
        
        if (length >= 20) {
            uint8_t version = (buffer[0] >> 4) & 0x0F;
            if (version == 4) {
                protocol = buffer[9];
                snprintf(srcIP, sizeof(srcIP), "%d.%d.%d.%d", buffer[12], buffer[13], buffer[14], buffer[15]);
                snprintf(dstIP, sizeof(dstIP), "%d.%d.%d.%d", buffer[16], buffer[17], buffer[18], buffer[19]);
                
                if (protocol == 17 && length >= 28) {  // UDP
                    protocolName = "UDP";
                    srcPort = (buffer[20] << 8) | buffer[21];
                    dstPort = (buffer[22] << 8) | buffer[23];
                    // 🎯 控制消息识别：服务器->客户端的控制包 (dst=127.0.0.1:0)
                    if (strcmp(dstIP, "127.0.0.1") == 0 && dstPort == 0) {
                        VPN_CLIENT_LOGI("🎯 收到控制消息(来自服务器): src=%{public}s:%{public}d dst=%{public}s:%{public}d size=%{public}d",
                                      srcIP, srcPort, dstIP, dstPort, length);
                        HandleControlMessage(buffer, static_cast<int>(length));
                        continue;  // 控制消息不写入TUN
                    }
                    if (srcPort == 53) {
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                     "📥 收到DNS响应: %{public}s:%{public}d -> %{public}s:%{public}d (UDP, %{public}d字节) <- VPN服务器",
                                     srcIP, srcPort, dstIP, dstPort, length);
                    } else {
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                     "📥 收到UDP响应: %{public}s:%{public}d -> %{public}s:%{public}d (%{public}d字节) <- VPN服务器",
                                     srcIP, srcPort, dstIP, dstPort, length);
                    }
                } else if (protocol == 6 && length >= 40) {  // TCP
                    protocolName = "TCP";
                    srcPort = (buffer[20] << 8) | buffer[21];
                    dstPort = (buffer[22] << 8) | buffer[23];

                    // 🔍 关键诊断：记录TCP标志位/seq/ack，判断握手/数据是否正常
                    uint8_t ipHeaderLen = (buffer[0] & 0x0F) * 4;
                    if (length >= ipHeaderLen + 20) {
                        uint8_t flags = buffer[ipHeaderLen + 13];
                        uint32_t seq = (static_cast<uint32_t>(buffer[ipHeaderLen + 4]) << 24) |
                                       (static_cast<uint32_t>(buffer[ipHeaderLen + 5]) << 16) |
                                       (static_cast<uint32_t>(buffer[ipHeaderLen + 6]) << 8) |
                                       (static_cast<uint32_t>(buffer[ipHeaderLen + 7]));
                        uint32_t ack = (static_cast<uint32_t>(buffer[ipHeaderLen + 8]) << 24) |
                                       (static_cast<uint32_t>(buffer[ipHeaderLen + 9]) << 16) |
                                       (static_cast<uint32_t>(buffer[ipHeaderLen + 10]) << 8) |
                                       (static_cast<uint32_t>(buffer[ipHeaderLen + 11]));
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB",
                                     "📥 TCP响应标志: flags=0x%{public}02x seq=%{public}u ack=%{public}u %{public}s:%{public}d -> %{public}s:%{public}d",
                                     flags, seq, ack, srcIP, srcPort, dstIP, dstPort);
                    }
                    const char* serviceType = "";
                    if (srcPort == 80) serviceType = " [HTTP响应]";
                    else if (srcPort == 443) serviceType = " [HTTPS响应]";
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📥 收到TCP响应: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s (%{public}d字节) <- VPN服务器",
                                 srcIP, srcPort, dstIP, dstPort, serviceType, length);
                } else if (protocol == 1) {  // ICMP
                    protocolName = "ICMP";
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📥 收到ICMP响应: %{public}s -> %{public}s (%{public}d字节) <- VPN服务器",
                                 srcIP, dstIP, length);
                } else {
                    snprintf(srcIP, sizeof(srcIP), "协议%d", protocol);
                    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                                 "📥 收到响应: 协议=%{public}d %{public}s -> %{public}s (%{public}d字节) <- VPN服务器",
                                 protocol, srcIP, dstIP, length);
                }
            } else if (version == 6 && length >= 40) {  // IPv6
                uint8_t nextHeader = buffer[6];

                // 🐛 修复：使用inet_ntop正确格式化IPv6地址
                struct in6_addr srcAddr6, dstAddr6;
                memcpy(&srcAddr6, &buffer[8], 16);
                memcpy(&dstAddr6, &buffer[24], 16);

                if (inet_ntop(AF_INET6, &srcAddr6, srcIP, sizeof(srcIP)) == nullptr) {
                    snprintf(srcIP, sizeof(srcIP), "IPv6格式错误");
                }
                if (inet_ntop(AF_INET6, &dstAddr6, dstIP, sizeof(dstIP)) == nullptr) {
                    snprintf(dstIP, sizeof(dstIP), "IPv6格式错误");
                }
                
                if (nextHeader == 6) protocolName = "TCP";
                else if (nextHeader == 17) protocolName = "UDP";
                else if (nextHeader == 58) protocolName = "ICMPv6";
                else protocolName = "其他";
                
                VPN_CLIENT_LOGI("📥 收到IPv6响应: %{public}s -> %{public}s (协议=%{public}s, %{public}d字节) <- VPN服务器",
                             srcIP, dstIP, protocolName, length);
            }
        }
        
        NETMANAGER_VPN_LOGI("📥 RESPONSE #%d: Received %{public}d bytes from server (total responses: %{public}d)",
                           responseCount, length, g_responsesReceived.load());

        // 接收到udp server的数据，写入到虚拟网卡中
        if (fdInfo.tunFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunFd: %{public}d, stopping write loop", fdInfo.tunFd);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "❌ TUN设备无效: tunFd=%{public}d, 无法写入响应", fdInfo.tunFd);
            break;
        }

        // 🔥 ZHOUB日志：代理成功后给TUN设备（包含数据前64字节的十六进制）
        char dataHex[129] = {0};  // 64字节 * 2 + 1
        int hexLen = length < 64 ? length : 64;
        for (int i = 0; i < hexLen; i++) {
            snprintf(dataHex + i * 2, 3, "%02x", buffer[i]);
        }
        
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                     "[代理→TUN] 源IP:%{public}s 目的IP:%{public}s 源端口:%{public}d 目的端口:%{public}d 协议:%{public}s 大小:%{public}d字节 数据:%{public}s",
                     srcIP, dstIP, srcPort, dstPort, protocolName, length, dataHex);

        int ret = write(fdInfo.tunFd, buffer, length);
        if (ret <= 0) {
            NETMANAGER_VPN_LOGE("error Write To Tunfd, errno: %{public}d", errno);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                         "❌ 写入TUN设备失败: 响应 #%{public}d (%{public}d字节), errno=%{public}d",
                         responseCount, length, errno);
        } else {
            NETMANAGER_VPN_LOGI("✅ Wrote %{public}d bytes to TUN device", ret);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
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
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                 "[VPN客户端] 🚀 准备启动数据转发线程...");
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                 "[VPN客户端] 📋 线程参数: tunFd=%{public}d, tunnelFd=%{public}d, 服务器=%{public}s:%{public}d",
                 g_fdInfo.tunFd, g_fdInfo.tunnelFd,
                 inet_ntoa(g_fdInfo.serverAddr.sin_addr), ntohs(g_fdInfo.serverAddr.sin_port));
    
    if (g_fdInfo.tunFd < 0) {
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, "ZHOUB", 
                     "[VPN客户端] ❌ 错误: tunFd无效 (%{public}d)，无法启动数据转发线程", g_fdInfo.tunFd);
        NETMANAGER_VPN_LOGE("Invalid tunFd: %{public}d, cannot start VPN", g_fdInfo.tunFd);
    }
    
    if (g_fdInfo.tunnelFd < 0) {
        OH_LOG_Print(LOG_APP, LOG_ERROR, 0x0000, "ZHOUB", 
                     "[VPN客户端] ❌ 错误: tunnelFd无效 (%{public}d)，无法启动数据转发线程", g_fdInfo.tunnelFd);
        NETMANAGER_VPN_LOGE("Invalid tunnelFd: %{public}d, cannot start VPN", g_fdInfo.tunnelFd);
    }
    
    g_threadRunF = true;
    std::thread tt1(HandleReadTunfd, g_fdInfo);
    std::thread tt2(HandleTcpReceived, g_fdInfo);

    g_threadT1 = std::move(tt1);
    g_threadT2 = std::move(tt2);
    
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                 "[VPN客户端] ✅ 数据转发线程已启动");
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                 "[VPN客户端] 📝 线程1: 从TUN读取数据包并转发到服务器");
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                 "[VPN客户端] 📝 线程2: 从服务器接收数据包并写入TUN");
    OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZHOUB", 
                 "[VPN客户端] 🔍 请查看后续日志确认数据包是否被读取和转发");

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

    NETMANAGER_VPN_LOGI("[ZHOUB] StopVpn successful");

    napi_value retValue;
    napi_create_int32(env, 0, &retValue);
    return retValue;
}

// 🔥 新增：保护转发socket的native接口
static napi_value ProtectForwardingSocket(napi_env env, napi_callback_info info)
{
    NETMANAGER_VPN_LOGI("========== ProtectForwardingSocket() 开始执行 ==========");

    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        NETMANAGER_VPN_LOGE("❌ 参数不足: 需要1个参数(sockFd)");
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }

    int32_t sockFd;
    napi_status status = napi_get_value_int32(env, args[0], &sockFd);
    if (status != napi_ok) {
        NETMANAGER_VPN_LOGE("❌ 解析sockFd参数失败");
        napi_value retValue;
        napi_create_int32(env, -1, &retValue);
        return retValue;
    }

    NETMANAGER_VPN_LOGI("🔍 准备保护转发socket: fd=%{public}d", sockFd);

    // 由于我们无法直接访问VpnConnection，这里返回成功
    // 实际的socket保护将通过ETS层的protectForwardingSocket方法完成
    NETMANAGER_VPN_LOGI("✅ ProtectForwardingSocket native接口调用完成: fd=%{public}d", sockFd);

    napi_value retValue;
    napi_create_int32(env, 0, &retValue);
    return retValue;
}

// 🔥 新增：获取下一个待保护socket的native接口
static napi_value GetNextSocketToProtect(napi_env env, napi_callback_info info)
{
    NETMANAGER_VPN_LOGI("========== GetNextSocketToProtect() 开始执行 ==========");

    int sockFd = GetNextSocketToProtect();

    if (sockFd >= 0) {
        NETMANAGER_VPN_LOGI("✅ 返回待保护socket: fd=%{public}d", sockFd);
    } else {
        NETMANAGER_VPN_LOGI("📭 没有待保护的socket");
    }

    napi_value retValue;
    napi_create_int32(env, sockFd, &retValue);
    return retValue;
}
 
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"udpConnect", nullptr, UdpConnect, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startVpn", nullptr, StartVpn, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopVpn", nullptr, StopVpn, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"protectForwardingSocket", nullptr, ProtectForwardingSocket, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getNextSocketToProtect", nullptr, GetNextSocketToProtect, nullptr, nullptr, nullptr, napi_default, nullptr},
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
