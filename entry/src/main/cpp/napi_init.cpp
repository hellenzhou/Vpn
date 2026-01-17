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
            }
            continue;
        }

        packetCount++;
        
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
                                         "🔥🔥🔥 [关键] HTTP/HTTPS连接 #%{public}d: %{public}s:%{public}d -> %{public}s:%{public}d%{public}s",
                                         g_detailedLogCount, srcIP, srcPort, dstIP, dstPort, serviceLabel);
                            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                         "🔥 [关键] 此连接已被VPN捕获并发送到服务器127.0.0.1:8888");
                        }
                        
                        NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 TCP %s:%d -> %s:%d%s", 
                                           packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel);
                        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", 
                                     "[VPN客户端] 📊 数据包 #%{public}d: IPv4 TCP %{public}s:%{public}d -> %{public}s:%{public}d%{public}s",
                                     packetCount, srcIP, srcPort, dstIP, dstPort, serviceLabel);
                    }
                } else if (protocol == 17) {  // UDP
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 UDP %s -> %s", packetCount, srcIP, dstIP);
                } else if (protocol == 1) {  // ICMP
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv4 ICMP %s -> %s", packetCount, srcIP, dstIP);
                } else {
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
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 TCP %s -> %s", packetCount, srcIP, dstIP);
                } else if (nextHeader == 17) {  // UDP
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 UDP %s -> %s", packetCount, srcIP, dstIP);
                } else if (nextHeader == 58) {  // ICMPv6
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 ICMPv6 %s -> %s", packetCount, srcIP, dstIP);
                } else {
                    NETMANAGER_VPN_LOGI("📊 PACKET #%d: IPv6 nextHeader=%d %s -> %s", packetCount, nextHeader, srcIP, dstIP);
                }
            } else {
                NETMANAGER_VPN_LOGI("📊 PACKET #%d: Unknown IP version %d", packetCount, version);
            }
        }

        // 读取到虚拟网卡的数据，通过udp隧道，发送给服务器
        if (fdInfo.tunnelFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunnelFd: %{public}d, stopping send loop", fdInfo.tunnelFd);
            break;
        }

        int sendResult = sendto(fdInfo.tunnelFd, buffer, readResult, 0,
            reinterpret_cast<struct sockaddr*>(&fdInfo.serverAddr), sizeof(fdInfo.serverAddr));
        if (sendResult <= 0) {
            NETMANAGER_VPN_LOGE("❌ Failed to send packet #%d to server[%{public}s:%{public}d], ret: %{public}d, error: %{public}s",
                                packetCount, inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port),
                                sendResult, strerror(errno));
            continue;
        }

        g_packetsSent++;
        NETMANAGER_VPN_LOGI("✅ PACKET #%d: Sent %{public}d bytes to server (total sent: %{public}d, responses: %{public}d)", 
                           packetCount, sendResult, g_packetsSent, g_responsesReceived);
        OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] ✅ 数据包 #%{public}d: 已发送 %{public}d 字节到服务器 (总计发送: %{public}d, 收到响应: %{public}d)",
                     packetCount, sendResult, g_packetsSent, g_responsesReceived);
        
        // 每10个数据包输出一次统计信息
        if (g_packetsSent % 10 == 0) {
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] 📊 数据包统计: IPv4总数=%{public}d IPv4 TCP=%{public}d IPv6总数=%{public}d IPv6 TCP=%{public}d 发送=%{public}d 响应=%{public}d",
                         g_ipv4Packets, g_ipv4TcpPackets, g_ipv6Packets, g_ipv6TcpPackets, g_packetsSent, g_responsesReceived);
            OH_LOG_Print(LOG_APP, LOG_INFO, 0x0000, "ZBQ", "[VPN客户端] 🌐 浏览器流量统计: HTTP(端口80)=%{public}d HTTPS(端口443)=%{public}d",
                         g_httpPackets, g_httpsPackets);
            
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
        NETMANAGER_VPN_LOGI("📥 RESPONSE #%d: Received %{public}d bytes from server (total responses: %{public}d)", 
                           responseCount, length, g_responsesReceived);

        // 接收到udp server的数据，写入到虚拟网卡中
        if (fdInfo.tunFd < 0) {
            NETMANAGER_VPN_LOGE("Invalid tunFd: %{public}d, stopping write loop", fdInfo.tunFd);
            break;
        }

        int ret = write(fdInfo.tunFd, buffer, length);
        if (ret <= 0) {
            NETMANAGER_VPN_LOGE("error Write To Tunfd, errno: %{public}d", errno);
        } else {
            NETMANAGER_VPN_LOGI("✅ Wrote %{public}d bytes to TUN device", ret);
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
