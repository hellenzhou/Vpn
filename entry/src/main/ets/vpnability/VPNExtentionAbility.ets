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
  private vpnConnection: vpnExtension.VpnConnection | null = null;
  // ⚠️ 重要：VPN服务器IP配置
  // - 部署机器IP：10.20.23.147
  // - 本地测试：127.0.0.1
  // - ⚠️ 不要使用10.8段（局域网冲突）
  private vpnServerIp: string = '127.0.0.1'; // VPN服务器IP（可改为10.20.23.147）
  // ⚠️ 重要：TUN IP是VPN虚拟网络的IP，与物理网络无关
  // - 使用192.168.100.x段（VPN虚拟网络）
  // - Gateway必须是192.168.100.1（VPN网关）
  // - ⚠️ 不要改成10.8段或其他物理网络段
  private tunIp: string = '192.168.100.2';  // VPN虚拟网络中的客户端IP（和工作版本一致）
  private blockedAppName: string = 'com.example.testvpn';

  // 🔥 新增：测试方法，确保扩展能力能够被调用
  testExtensionAbility() {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== testExtensionAbility() 被调用 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔥 测试：扩展能力进程已启动！');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📋 这是一个测试方法，用于验证扩展能力是否正常工作');
    return "ExtensionAbility is working!";
  }

  onCreate(want: Want) {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onCreate() 被调用 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - bundleName: %{public}s', want.bundleName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - abilityName: %{public}s', want.abilityName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔥 测试：扩展能力进程已启动！');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔥 按照官方DEMO在onCreate()中创建VPN连接');
    super.onCreate(want);
    
    // 🔥 按照官方DEMO：在onCreate()中创建VPN连接
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 检查Context有效性...');
    if (!this.context) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ Context is invalid，无法创建VpnConnection');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ this.context = null');
      return;
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ Context有效，准备创建VpnConnection');
    
    try {
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📞 调用 vpnExtension.createVpnConnection()...');
      this.vpnConnection = vpnExtension.createVpnConnection(this.context);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ createVpnConnection() 成功，VpnConnection对象已创建');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 VpnConnection类型: %{public}s', typeof this.vpnConnection);
      
      // 🔥 按照正确流程：先创建UDP隧道，再保护隧道，最后配置VPN
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🚀 开始VPN初始化流程...');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔹 步骤1: 创建UDP隧道...');
      this.CreateTunnel();
    } catch (error) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ Failed to create VpnConnection: %{public}s', JSON.stringify(error));
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 错误详情: %{public}s', error?.message ?? '');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 错误堆栈: %{public}s', error?.stack ?? '');
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] onCreate() 执行完成');
  }

  onRequest(want: Want, startId: number) {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onRequest() 被调用 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔥 测试：onRequest()被调用了！');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅✅✅ 确认：onRequest() 已被系统调用 ✅✅✅');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📋 调用参数详情:');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - abilityName: %{public}s', want.abilityName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - startId: %{public}d', startId);
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - bundleName: %{public}s', want.bundleName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - context存在: %{public}s', this.context ? '是' : '否');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ VPN扩展启动时会调用onRequest()，这是正确的启动方式');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📝 注意：按照官方DEMO，VPN连接创建在onCreate()中完成');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ onRequest() 执行完成（仅记录日志）');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onRequest() 结束 ==========');
    // ⚠️ 注意：按照官方DEMO，不在onRequest()中创建VPN连接
    // VPN连接创建应该在onCreate()中完成
  }

  // 🔥 新增：自动初始化VPN的方法
  initializeVpn() {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== initializeVpn() 开始执行 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 检查VpnConnection有效性...');
    
    if (!this.vpnConnection) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ VpnConnection is null，无法继续初始化');
      return;
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ VpnConnection有效，准备创建UDP隧道');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 VpnConnection类型: %{public}s', typeof this.vpnConnection);

    try {
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📞 调用 vpnExtension.createVpnConnection()...');
      this.vpnConnection = vpnExtension.createVpnConnection(this.context);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ createVpnConnection() 成功，VpnConnection对象已创建');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 VpnConnection类型: %{public}s', typeof this.vpnConnection);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 准备调用 CreateTunnel() 创建UDP隧道...');
      this.CreateTunnel();
    } catch (error) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ Failed to create VpnConnection: %{public}s', JSON.stringify(error));
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 错误详情: %{public}s', error?.message ?? '');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 错误堆栈: %{public}s', error?.stack ?? '');
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] initializeVpn() 执行完成');
  }

  onConnect(want: Want) {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onConnect() 被调用 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - abilityName: %{public}s', want.abilityName);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - bundleName: %{public}s', want.bundleName ?? '');
    return null;
  }

  onDisconnect(want: Want) {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onDisconnect() 被调用 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - abilityName: %{public}s', want.abilityName);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - bundleName: %{public}s', want.bundleName ?? '');
  }

  onDestroy() {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onDestroy() 被调用 ==========');
    this.Destroy();
  }

  Destroy() {
    hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'vpn Destroy');
    // 关闭VPN
    if (this.vpnConnection) {
      this.vpnConnection.destroy().then(() => {
        hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'vpn Destroy Success');
      }).catch((err: Error) => {
        hilog.error(0x0000, 'testTag', 'developTag', 'vpn Destroy Failed: %{public}s', JSON.stringify(err) ?? '');
      });
    }
  }

  // 创建隧道的方法
  CreateTunnel() {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== CreateTunnel() 开始执行 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📋 当前配置:');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - VPN服务器IP: %{public}s', this.vpnServerIp);
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - VPN服务器端口: 8888');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 检查全局变量状态...');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - gTunnelFd: %{public}d', gTunnelFd);
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - gTunFd: %{public}d', gTunFd);
    
    // 检查是否已经创建了隧道
    if (gTunnelFd >= 0) {
      hilog.info(0x0000, 'ZBQ', '[扩展能力] ⚠️ 隧道已存在，gTunnelFd: %{public}d，跳过创建', gTunnelFd);
      this.Protect();
      return;
    }
    
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📞 准备调用 vpn_client.udpConnect()...');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 参数检查 - vpn_client存在: %{public}s', typeof vpn_client !== 'undefined' ? '是' : '否');
    
    try {
      let tunnelFd = vpn_client.udpConnect(this.vpnServerIp, 8888);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📥 udpConnect() 返回: %{public}d', tunnelFd);
      
      if (tunnelFd < 0) {
        hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ Failed to create UDP tunnel, tunnelFd: %{public}d', tunnelFd);
        return;
      }
      
      hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ Tunnel created, FD: %{public}d', tunnelFd);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 隧道创建成功，准备调用 Protect()...');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔧 准备设置全局变量 gTunnelFd...');
      hilog.info(0x0000, 'ZBQ', '[扩展能力]   当前 gTunnelFd = %{public}d', gTunnelFd);
      setTunnelFd(tunnelFd);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ gTunnelFd已设置为: %{public}d', tunnelFd);
      hilog.info(0x0000, 'ZBQ', '[扩展能力]   验证 gTunnelFd = %{public}d', gTunnelFd);
      if (gTunnelFd !== tunnelFd) {
        hilog.error(0x0000, 'ZBQ', '[扩展能力] ⚠️ 警告: gTunnelFd设置后验证失败！');
        hilog.error(0x0000, 'ZBQ', '[扩展能力] ⚠️ 期望值: %{public}d, 实际值: %{public}d', tunnelFd, gTunnelFd);
      }
    } catch (error) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ udpConnect() 异常: %{public}s', JSON.stringify(error));
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 错误详情: %{public}s', error?.message ?? '');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   3) 网络不可达 - 请确认IP地址正确(127.0.0.1用于本地，10.20.23.147用于远程)');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   4) socket创建失败 - 请检查原生代码日志中的socket()错误信息');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   5) IP地址格式错误 - 请确认IP地址格式正确(例如: 127.0.0.1)');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] 📝 排查建议:');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   - 查看原生代码日志(napi_init.cpp)中的详细错误信息');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   - 确认VPN服务器应用已启动并显示"RUNNING"状态');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   - 检查VPN服务器日志中是否有"Starting VPN Server"和"Socket bound successfully"消息');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   - 如果使用远程IP，请确认网络连接正常');
      return;
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🚀 准备调用 Protect() 保护UDP隧道...');
    this.Protect();
    // [EndExclude vpn_control_case_extention_ability]
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ CreateTunnel() 执行完成');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== CreateTunnel() 结束 ==========');
  }

  // 保护隧道的方法
  Protect() {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== Protect() 开始执行 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] step2: 准备保护UDP隧道');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 当前 gTunnelFd = %{public}d', gTunnelFd);
    if (gTunnelFd < 0) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ gTunnelFd无效 (%{public}d)，无法保护隧道', gTunnelFd);
      return;
    }
    if (this.vpnConnection) {
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 调用 vpnConnection.protect(%{public}d)...', gTunnelFd);
      this.vpnConnection.protect(gTunnelFd).then(() => {
        hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ vpn Protect Success，UDP隧道已保护');
        hilog.info(0x0000, 'ZBQ', '[扩展能力] 准备调用 SetupVpn() 创建VPN连接...');
        this.SetupVpn();
      }).catch((err: Error) => {
        hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ vpn Protect Failed: %{public}s', JSON.stringify(err) ?? '');
      });
    } else {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ vpnConnection为null，无法保护隧道');
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] Protect() 执行完成');
  }

  // 设置VPN网络配置的方法
  SetupVpn() {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== SetupVpn() 开始执行 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] step3: 准备创建VPN连接');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 当前状态: gTunFd=%{public}d, gTunnelFd=%{public}d', gTunFd, gTunnelFd);

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
          new AddressWithPrefix(new Address(tunIp, 1), 24),   // IPv4
          new AddressWithPrefix(new Address('::1', 2), 128)   // IPv6
        ];
        this.mtu = 1400;
        // 🔥🔥🔥 关键修复：使用空DNS数组强制DNS查询也通过VPN隧道
        // 这样所有网络流量（包括DNS）都必须走VPN隧道
        this.dnsAddresses = [];  // 空数组 = DNS查询也走VPN隧道
        
        // 🔥🔥🔥 修复路由循环：必须将VPN代理服务器添加到trustedApplications
        // VPN代理服务器需要直接访问真实网络来转发流量，否则会形成路由循环！
        // ⚠️ 重要：VPN服务器的socket流量不能被TUN设备捕获，否则会无限循环
        this.trustedApplications = ['com.hellen.vpnserver'];  // VPN代理服务器排除在VPN路由之外
        
        // 🔥🔥🔥 关键配置：blockedApplications为空表示不阻止任何应用
        // 所有应用的流量都应该被路由到VPN（除了trustedApplications中的）
        this.blockedApplications = [];  // 空数组 = 不阻止任何应用，都走VPN（除了trusted的）
        
        // 🔥 全流量VPN路由配置 - 按照工作版本
        // ⚠️ 不需要127.0.0.0/8排除路由，vpnConnection.protect()已处理UDP隧道保护
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
      hilog.info(0x0000, 'ZBQ', '[扩展能力] VPN配置: tunIp=%{public}s, blockedAppName=%{public}s', this.tunIp, this.blockedAppName);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 [TUN IP检查] VPN虚拟网络IP配置: %{public}s', this.tunIp);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 [TUN IP检查] 使用手动指定IP: %{public}s（与工作版本一致）', this.tunIp);
      
      // 🔥 输出详细的VPN配置信息
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 ========== 完整VPN配置（防止路由循环） ==========');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 addresses: %{public}s', JSON.stringify(config.addresses));
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 mtu: %{public}d', config.mtu);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 dnsAddresses: %{public}s (空=DNS走VPN)', JSON.stringify(config.dnsAddresses));
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔥 trustedApplications: %{public}s (VPN服务器不走VPN，避免循环)', JSON.stringify(config.trustedApplications));
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 blockedApplications: %{public}s (空=所有应用走VPN，除了trusted)', JSON.stringify(config.blockedApplications));
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 routes数量: %{public}d (仅IPv4+IPv6默认路由)', config.routes.length);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 route[0]: IPv4默认路由 0.0.0.0/0 → vpn-tun (网关: 192.168.100.1)');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 route[1]: IPv6默认路由 ::/0 → vpn-tun (网关: ::1)');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ VPN服务器(com.hellen.vpnserver)已排除在VPN路由外，不会循环');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 📊 ====================================');
      
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 调用 vpnConnection.create(config)...');
      this.vpnConnection.create(config)
        // [StartExclude vpn_control_case_extention_ability]
        .then((data: number) => {
          hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ VPN连接创建成功，TUN FD: %{public}d', data);
          hilog.info(0x0000, 'ZBQ', '[扩展能力] 设置全局变量 gTunFd = %{public}d', data);
          setTunFd(data);
          hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ gTunFd已设置为: %{public}d', data);
          hilog.info(0x0000, 'ZBQ', '[扩展能力] 当前状态: gTunFd=%{public}d, gTunnelFd=%{public}d', gTunFd, gTunnelFd);
          if (gTunnelFd < 0) {
            hilog.error(0x0000, 'ZBQ', '[扩展能力] ⚠️ 警告: gTunnelFd无效 (%{public}d)，数据转发可能失败', gTunnelFd);
          }
          hilog.info(0x0000, 'ZBQ', '[扩展能力] 调用 vpn_client.startVpn(%{public}d, %{public}d)...', gTunFd, gTunnelFd);
          vpn_client.startVpn(gTunFd, gTunnelFd);
          hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ VPN数据转发线程已启动');
        })
        // [EndExclude vpn_control_case_extention_ability]
        .catch((err: BusinessError) => {
          hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ VPN连接创建失败');
          hilog.error(0x0000, 'ZBQ', '[扩展能力] 错误代码: %{public}d', err.code ?? -1);
          hilog.error(0x0000, 'ZBQ', '[扩展能力] 错误消息: %{public}s', err.message ?? '');
          hilog.error(0x0000, 'ZBQ', '[扩展能力] 错误详情: %{public}s', JSON.stringify(err) ?? '');
        });
    } else {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ vpnConnection为null，无法创建VPN连接');
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] SetupVpn() 执行完成');
  }

}
// [End vpn_control_case_extention_ability]