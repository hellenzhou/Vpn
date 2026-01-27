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
// [Start vpn_control_case_extention_ability]
import { vpnExtension, VpnExtensionAbility } from '@kit.NetworkKit';
import { Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { gTunFd, gTunnelFd, setTunnelFd, setTunFd } from './GlobalVpnState';

// [StartExclude vpn_control_case_extention_ability]
// vpn_client是一个C语言便携的so，比如import vpn_client from 'libvpn_client.so';
import vpn_client from 'libentry.so';
// [EndExclude vpn_control_case_extention_ability]

const TAG: string = '[MyVpnExtAbility]';

export default class MyVpnExtAbility extends VpnExtensionAbility {
  constructor() {
    super();
  }
  
  private vpnConnection: vpnExtension.VpnConnection | null = null;
  // ⚠️ 重要：VPN服务器IP配置
  // - ✅ 同设备应用间通信：使用 127.0.0.1（本地回环，避免路由问题）
  // - 跨设备通信：可改为实际服务器IP
  // - 本地设备真实IP：10.18.15.169（仅供参考，不应在同设备通信时使用）
  // - ⚠️ 不要使用10.8段（局域网冲突）
  private vpnServerIp: string = '127.0.0.1'; // ✅ 修复：同设备应用间通信使用 localhost
  // ⚠️ 重要：TUN IP是VPN虚拟网络的IP，与物理网络无关
  // - 使用192.168.100.x段（VPN虚拟网络）
  // - Gateway必须是192.168.100.1（VPN网关）
  // - ⚠️ 不要改成10.8段或其他物理网络段
  private tunIp: string = '192.168.100.2';  // VPN虚拟网络中的客户端IP（和工作版本一致）
  // ⚠️ 必须填写“真实 bundleName”，否则绕过不生效，服务端转发 socket 也会被 VPN 接管导致回环/无响应
  private blockedAppName: string = 'com.hellen.vpnserver';

  testExtensionAbility() {
    return "ExtensionAbility is working!";
  }

  protectForwardingSocket(sockFd: number): boolean {

    if (!this.vpnConnection) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] VpnConnection不存在，无法保护socket');
      return false;
    }

    try {
      hilog.info(0x0000, 'ZHOUB', '🔍 [流程跟踪-4] 调用VpnConnection.protect(): fd=%{public}d', sockFd);
      this.vpnConnection.protect(sockFd).then(() => {
        hilog.info(0x0000, 'ZHOUB', '✅ [流程跟踪-4] socket保护成功: fd=%{public}d (socket已绕过VPN路由表)', sockFd);
      }).catch((err: Error) => {
        hilog.error(0x0000, 'ZHOUB', '❌ [流程跟踪-4] 转发socket保护失败 fd=%{public}d: %{public}s', sockFd, JSON.stringify(err));
        hilog.error(0x0000, 'ZHOUB', '🚨🚨🚨 如果protect()失败，socket流量会被VPN路由表捕获，形成环路！');
      });
      return true;
    } catch (error) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] protectForwardingSocket异常: %{public}s', JSON.stringify(error));
      return false;
    }
  }

  onCreate(want: Want) {
    hilog.info(0x0000, 'ZHOUB', '[扩展能力] onCreate() bundleName=%{public}s abilityName=%{public}s', want.bundleName ?? '', want.abilityName ?? '');
    
    try {
      super.onCreate(want);
    } catch (error) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] super.onCreate()失败: %{public}s', error?.message ?? '');
      return;
    }
    
    if (!this.context) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] Context无效，无法创建VpnConnection');
      return;
    }
    
    try {
      this.vpnConnection = vpnExtension.createVpnConnection(this.context);
      hilog.info(0x0000, 'ZHOUB', '[扩展能力] VpnConnection创建成功');
      this.CreateTunnel();
    } catch (error) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] 创建VpnConnection失败: %{public}s', error?.message ?? '');
    }
  }

  onRequest(want: Want, startId: number) {
    hilog.info(0x0000, 'ZHOUB', '[扩展能力] onRequest() abilityName=%{public}s startId=%{public}d', want.abilityName ?? '', startId);
  }

  initializeVpn() {
    if (!this.vpnConnection) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] VpnConnection为null，无法初始化');
      return;
    }

    try {
      this.vpnConnection = vpnExtension.createVpnConnection(this.context);
      this.CreateTunnel();
    } catch (error) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] 创建VpnConnection失败: %{public}s', error?.message ?? '');
    }
  }

  onConnect(want: Want) {
    return null;
  }

  onDisconnect(want: Want) {
  }

  onDestroy() {
    this.Destroy();
  }

  Destroy() {
    if (this.vpnConnection) {
      this.vpnConnection.destroy().then(() => {
        hilog.info(0x0000, 'ZHOUB', '[扩展能力] VPN已销毁');
      }).catch((err: Error) => {
        hilog.error(0x0000, 'ZHOUB', '[扩展能力] VPN销毁失败: %{public}s', JSON.stringify(err) ?? '');
      });
    }
  }

  // 创建隧道的方法
  CreateTunnel() {
    if (gTunnelFd >= 0) {
      hilog.info(0x0000, 'ZHOUB', '[扩展能力] 隧道已存在，跳过创建 gTunnelFd=%{public}d', gTunnelFd);
      this.Protect();
      return;
    }
    
    try {
      let tunnelFd = vpn_client.udpConnect(this.vpnServerIp, 8888);
      if (tunnelFd < 0) {
        hilog.error(0x0000, 'ZHOUB', '[扩展能力] 创建UDP隧道失败 tunnelFd=%{public}d', tunnelFd);
        return;
      }
      
      setTunnelFd(tunnelFd);
      hilog.info(0x0000, 'ZHOUB', '[扩展能力] UDP隧道创建成功 fd=%{public}d', tunnelFd);
      if (gTunnelFd !== tunnelFd) {
        hilog.error(0x0000, 'ZHOUB', '[扩展能力] gTunnelFd设置验证失败 期望=%{public}d 实际=%{public}d', tunnelFd, gTunnelFd);
      }
      this.Protect();
    } catch (error) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] udpConnect()异常: %{public}s', error?.message ?? '');
    }
  }

  // 保护隧道的方法
  Protect() {
    if (gTunnelFd < 0) {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] gTunnelFd无效，无法保护隧道 gTunnelFd=%{public}d', gTunnelFd);
      return;
    }
    if (this.vpnConnection) {
      this.vpnConnection.protect(gTunnelFd).then(() => {
        hilog.info(0x0000, 'ZHOUB', '[扩展能力] UDP隧道已保护');
        this.SetupVpn();
      }).catch((err: Error) => {
        hilog.error(0x0000, 'ZHOUB', '[扩展能力] 保护隧道失败: %{public}s', JSON.stringify(err) ?? '');
      });
    } else {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] vpnConnection为null，无法保护隧道');
    }
  }

  // 设置VPN网络配置的方法
  SetupVpn() {

    class Address {
      public address: string;
      public family: number;
      constructor(address: string, family: number) {
        this.address = address;
        this.family = family;
      }
    }

    class AddressWithPrefix {
      public address: Address;
      public prefixLength: number;
      constructor(address: Address, prefixLength: number) {
        this.address = address;
        this.prefixLength = prefixLength;
      }
    }

    class Config {
      public addresses: AddressWithPrefix[];
      public mtu: number;
      public dnsAddresses: string[];
      public trustedApplications: string[];
      public blockedApplications: string[];
      public routes: vpnExtension.RouteInfo[];
      constructor(
        tunIp: string,
        blockedAppName: string
      ) {
        // 配置 IPv4 和 IPv6 地址
        this.addresses = [
          new AddressWithPrefix(new Address(tunIp, 1), 24),   // IPv4地址
          new AddressWithPrefix(new Address('::1', 2), 128)   // IPv6地址
        ];
        this.mtu = 1400;
        // 🔥🔥🔥 关键修复：使用空DNS数组强制DNS查询也通过VPN隧道
        // 这样所有网络流量（包括DNS）都必须走VPN隧道
        this.dnsAddresses = [];  // 空数组 = DNS查询也走VPN隧道
        
        // 🎯 正确配置：使用blockedApplications让代理服务器绕过VPN
        // 根据HarmonyOS VPN的实际行为：
        // - trustedApplications: 这些应用绕过VPN，直接访问物理网络
        // - blockedApplications: 这些应用被阻止使用VPN（实际上也是绕过VPN）
        // 代理服务器必须绕过VPN，避免路由循环
        
        // ✅ 正确配置：让代理服务器绕过VPN，避免路由循环
        this.trustedApplications = [];  // 空数组 - 不使用白名单
        this.blockedApplications = [blockedAppName];  // ✅ 代理服务器绕过VPN
        

        this.routes = [
          // IPv4 默认路由 - 捕获所有IPv4流量
          {
            interface: 'vpn-tun',
            destination: {
              address: new Address('0.0.0.0', 1),  // 0.0.0.0/0 = 所有IPv4流量
              prefixLength: 0  // /0 = 匹配所有地址
            },
            gateway: {
              address: '192.168.100.1',
              family: 1,  // IPv4
              port: 0
            },
            hasGateway: true,
            isDefaultRoute: true
          },
          // IPv6 默认路由 - 捕获所有IPv6流量
          {
            interface: 'vpn-tun',
            destination: {
              address: new Address('::', 2),  // ::/0 = 所有IPv6流量
              prefixLength: 0
            },
            gateway: {
              address: '::1',
              family: 2,  // IPv6
              port: 0
            },
            hasGateway: true,
            isDefaultRoute: true
          }
        ];
      }
    }

    let config = new Config(this.tunIp, this.blockedAppName);

    if (this.vpnConnection) {
      hilog.info(0x0000, 'ZHOUB', '[扩展能力] VPN配置 tunIp=%{public}s blockedApp=%{public}s', this.tunIp, this.blockedAppName);
      this.vpnConnection.create(config)
        // [StartExclude vpn_control_case_extention_ability]
        .then((data: number) => {
          setTunFd(data);
          hilog.info(0x0000, 'ZHOUB', '[扩展能力] VPN连接创建成功 gTunFd=%{public}d gTunnelFd=%{public}d', gTunFd, gTunnelFd);
          if (gTunnelFd < 0) {
            hilog.error(0x0000, 'ZHOUB', '[扩展能力] gTunnelFd无效，数据转发可能失败 gTunnelFd=%{public}d', gTunnelFd);
          }
          vpn_client.startVpn(gTunFd, gTunnelFd);

        })
        // [EndExclude vpn_control_case_extention_ability]
        .catch((err: BusinessError) => {
          hilog.error(0x0000, 'ZHOUB', '[扩展能力] VPN连接创建失败: %{public}s', JSON.stringify(err) ?? '');
        });

      this.startSocketProtectionMonitor();
    } else {
      hilog.error(0x0000, 'ZHOUB', '[扩展能力] vpnConnection为null，无法创建VPN连接');
    }
  }

  // 启动socket保护监控定时器
  private startSocketProtectionMonitor() {
    setInterval(() => {
      try {
        const sockFd: number = vpn_client.getNextSocketToProtect();
        if (sockFd >= 0) {
          hilog.info(0x0000, 'ZHOUB', '🔍 [流程跟踪-3] 从保护队列获取socket: fd=%{public}d, 准备调用protect()', sockFd);
          const success: boolean = this.protectForwardingSocket(sockFd);
          if (success) {
            hilog.info(0x0000, 'ZHOUB', '✅ [流程跟踪-3] socket保护请求已发送: fd=%{public}d (等待protect() Promise完成)', sockFd);
          } else {
            hilog.error(0x0000, 'ZHOUB', '❌ [流程跟踪-3] socket保护失败: fd=%{public}d', sockFd);
          }
        }
      } catch (error) {
        hilog.error(0x0000, 'ZHOUB', '[扩展能力] socket保护监控异常: %{public}s', JSON.stringify(error));
      }
    }, 500);
  }

}
// [End vpn_control_case_extention_ability]