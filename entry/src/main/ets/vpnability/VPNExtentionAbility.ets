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
  // - 官方DEMO使用 '0.0.0.0'（系统自动分配）
  // - 也可以使用192.168.100.x段（VPN虚拟网络）
  // - Gateway必须是192.168.100.1（VPN网关）
  // - ⚠️ 不要改成10.8段或其他物理网络段
  private tunIp: string = '0.0.0.0';  // 按照官方DEMO使用0.0.0.0（系统自动分配）
  private blockedAppName: string = 'com.example.testvpn';

  onCreate(want: Want) {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onCreate() 被调用 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - bundleName: %{public}s', want.bundleName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] - abilityName: %{public}s', want.abilityName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ⚠️ 注意：VPN扩展通常使用onRequest()，但onCreate()也会被调用');
    super.onCreate(want);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] onCreate() 执行完成');
    // ⚠️ 注意：VPN扩展启动时会调用onRequest而不是onCreate
    // 初始化逻辑在onRequest()中完成
  }

  onRequest(want: Want, startId: number) {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onRequest() 被调用 ==========');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅✅✅ 确认：onRequest() 已被系统调用 ✅✅✅');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📋 调用参数详情:');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - abilityName: %{public}s', want.abilityName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - startId: %{public}d', startId);
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - bundleName: %{public}s', want.bundleName ?? '');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - context存在: %{public}s', this.context ? '是' : '否');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ VPN扩展启动时会调用onRequest()，这是正确的启动方式');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📝 注意：VPN扩展通常使用onRequest()而不是onCreate()');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ⚠️ 手动操作模式：不自动初始化VPN，等待用户在UI中手动操作');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📝 用户需要在UI中依次点击：1)创建隧道 2)保护隧道 3)打开VPN');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ onRequest() 执行完成（仅启动扩展能力，不执行VPN初始化）');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== onRequest() 结束 ==========');
    // ⚠️ 注意：不自动调用 initializeVpn()，改为手动操作模式
    // 所有VPN操作（创建隧道、保护隧道、打开VPN）都在UI进程中手动执行
  }

  private initializeVpn() {
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ========== initializeVpn() 开始执行 ==========');

    if (!this.context) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ Context is invalid，无法继续初始化');
      return;
    }
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ Context有效，准备创建VpnConnection');

    try {
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 调用 vpnExtension.createVpnConnection()...');
      this.vpnConnection = vpnExtension.createVpnConnection(this.context);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ createVpnConnection() 成功，VpnConnection对象已创建');
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 准备调用 CreateTunnel() 创建UDP隧道...');
      this.CreateTunnel();
    } catch (error) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ Failed to create VpnConnection: %{public}s', JSON.stringify(error));
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
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   - vpnConnection状态: %{public}s', this.vpnConnection ? '已创建' : '未创建');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] step1: 准备创建UDP隧道');
    // 连接VPN服务器
    // [StartExclude vpn_control_case_extention_ability]
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔗 准备连接VPN服务器: %{public}s:8888', this.vpnServerIp);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📞 调用原生函数 vpn_client.udpConnect(%{public}s, 8888)...', this.vpnServerIp);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ⏳ 等待udpConnect()返回...');
    
    let tunnelFd: number = -999; // 初始化为一个明显无效的值，用于调试
    try {
      tunnelFd = vpn_client.udpConnect(this.vpnServerIp, 8888);
    } catch (error) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ udpConnect() 抛出异常');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 异常详情: %{public}s', JSON.stringify(error) ?? '');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 可能原因: 1)原生模块未加载 2)函数签名不匹配 3)参数类型错误');
      return;
    }
    
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📥 udpConnect() 调用完成，返回值: tunnelFd = %{public}d', tunnelFd);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 返回值类型检查: typeof(tunnelFd) = %{public}s', typeof tunnelFd);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 返回值有效性检查: tunnelFd < 0 ? %{public}s', (tunnelFd < 0) ? '是(无效)' : '否(有效)');
    
    if (tunnelFd < 0) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌❌❌ 创建UDP隧道失败 ❌❌❌');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 失败详情:');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   - VPN服务器: %{public}s:8888', this.vpnServerIp);
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   - 返回的FD: %{public}d', tunnelFd);
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ❌ 可能原因分析:');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   1) VPN服务器未启动 - 请检查VPNServer应用是否已启动并点击"Start"按钮');
      hilog.error(0x0000, 'ZBQ', '[扩展能力]   2) VPN服务器未监听8888端口 - 请检查服务器日志中的"Socket bound successfully"消息');
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
    
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅✅✅ UDP隧道创建成功 ✅✅✅');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ 隧道文件描述符: FD = %{public}d', tunnelFd);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📝 注意: UDP是无连接协议，socket已创建但未实际连接');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 📝 后续将通过sendto()发送数据包到服务器');
    hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔧 准备设置全局变量 gTunnelFd...');
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   当前 gTunnelFd = %{public}d', gTunnelFd);
    setTunnelFd(tunnelFd);
    hilog.info(0x0000, 'ZBQ', '[扩展能力] ✅ gTunnelFd已设置为: %{public}d', tunnelFd);
    hilog.info(0x0000, 'ZBQ', '[扩展能力]   验证 gTunnelFd = %{public}d', gTunnelFd);
    if (gTunnelFd !== tunnelFd) {
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ⚠️ 警告: gTunnelFd设置后验证失败！');
      hilog.error(0x0000, 'ZBQ', '[扩展能力] ⚠️ 期望值: %{public}d, 实际值: %{public}d', tunnelFd, gTunnelFd);
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

    // 定义目标地址的接口类型
    interface DestinationItem {
      address: Address;
      prefixLength: number;
      port: number;
    }

    // 定义网关的接口类型
    interface GatewayItem {
      address: string;
      family: number;
      port: number;
    }

    // 定义路由配置的接口类型
    interface RouteConfig {
      interface: string;
      destination: DestinationItem;
      gateway: GatewayItem;
      hasGateway: boolean;
      isDefaultRoute: boolean;
    }

    class Config {
      public addresses: AddressWithPrefix[];
      public dnsAddresses: string[];
      public trustedApplications: string[];
      public blockedApplications: string[];
      constructor(
        tunIp: string,
        blockedAppName: string
      ) {
        this.addresses = [
          new AddressWithPrefix(new Address(tunIp, 1), 24)
        ];
        this.dnsAddresses = ['114.114.114.114']; // 通用DNS地址
        this.trustedApplications = [];
        this.blockedApplications = [];
      }
    }

    let config = new Config(this.tunIp, this.blockedAppName);

    if (this.vpnConnection) {
      hilog.info(0x0000, 'ZBQ', '[扩展能力] VPN配置: tunIp=%{public}s, blockedAppName=%{public}s', this.tunIp, this.blockedAppName);
      hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 [TUN IP检查] VPN虚拟网络IP配置: %{public}s', this.tunIp);
      if (this.tunIp === '0.0.0.0') {
        hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 [TUN IP检查] 使用系统自动分配（官方DEMO方式），系统会分配192.168.100.x段的IP');
      } else {
        hilog.info(0x0000, 'ZBQ', '[扩展能力] 🔍 [TUN IP检查] 使用手动指定IP: %{public}s（便于调试）', this.tunIp);
      }
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

  // 发送测试数据包的方法
  sendTestData() {
    // 创建一个简单的DNS查询包作为测试
    const testData: Uint8Array = new Uint8Array([
      0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x07, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x03, 0x63, 0x6f, 0x6d,
      0x00, 0x00, 0x01, 0x00, 0x01
    ]);

    // 将数据写入TUN设备，测试是否能到达服务器
    if (gTunFd >= 0) {
      hilog.info(0x0000, 'testTag', '*** 尝试发送测试数据包到TUN设备 ***');
      // 注意：这里需要通过NAPI调用写入数据，或者修改native代码
      vpn_client.sendTestData(gTunFd, testData);
    } else {
      hilog.error(0x0000, 'testTag', '*** TUN设备未准备好，无法发送测试数据 ***');
    }
  }
}
// [End vpn_control_case_extention_ability]