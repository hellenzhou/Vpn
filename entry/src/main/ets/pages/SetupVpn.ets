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

// [Start vpn_config_import]
import { vpnExtension } from '@kit.NetworkKit';
import { common } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
// [End vpn_config_import]
import { ComponentId } from '../common/CommonConstant'; // 引入枚举
import { BusinessError } from '@kit.BasicServicesKit';
import { showToast } from '../common/ShowToast';
import vpn_client from 'libentry.so';
import NotificationOperations from '../notification/NotificationOperations';
import { gTunFd, gTunnelFd, setTunFd, setTunnelFd } from '../vpnability/GlobalVpnState';

const buttonFontSize = 25; // 按钮的字体大小
const buttonMargin = 16;
let notificationOperations: NotificationOperations = new NotificationOperations(getContext(this));

// ⚠️ 重要：TUN IP是VPN虚拟网络的IP，与物理网络无关
// - 使用192.168.100.x段（VPN虚拟网络）
// - Gateway必须是192.168.100.1（VPN网关）
// - ⚠️ 不要改成10.8段或其他物理网络段
let tunIp: string = '192.168.100.2';  // VPN虚拟网络中的客户端IP（不要修改为物理网络IP）
let blockedAppName: string = 'com.example.baidumyapplication'; // 示例

@Component
export default struct allNetworksBtn {
  @Link flag: string;

  build() {
    Column() {
      Button($r('app.string.SetupVpn'))
        .onClick(() => {
          this.SetupVpn();
        })
        .width('70%')
        .id(ComponentId.SETUP_VPN_BUTTON)
        .fontSize(buttonFontSize)
        .margin(buttonMargin);
    }
  }

  SetupVpn() {
    hilog.info(0x0000, 'ZBQ', '[UI] ========== 手动打开VPN ==========');
    hilog.info(0x0000, 'ZBQ', '[UI] 📋 当前状态:');
    hilog.info(0x0000, 'ZBQ', '[UI]   - gTunFd: %{public}d', gTunFd);
    hilog.info(0x0000, 'ZBQ', '[UI]   - gTunnelFd: %{public}d', gTunnelFd);
    hilog.info(0x0000, 'ZBQ', '[UI]   - tunIp: %{public}s', tunIp);
    hilog.info(0x0000, 'ZBQ', '[UI]   - blockedAppName: %{public}s', blockedAppName);
    hilog.info(0x0000, 'ZBQ', '[UI] 🔍 [TUN IP检查] VPN虚拟网络IP配置: %{public}s', tunIp);
    if (tunIp === '0.0.0.0') {
      hilog.info(0x0000, 'ZBQ', '[UI] 🔍 [TUN IP检查] 使用系统自动分配（官方DEMO方式），系统会分配192.168.100.x段的IP');
    } else {
      hilog.info(0x0000, 'ZBQ', '[UI] 🔍 [TUN IP检查] 使用手动指定IP: %{public}s（便于调试）', tunIp);
    }
    
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
      public mtu: number;
      public dnsAddresses: string[];
      public trustedApplications: string[];
      public blockedApplications: string[];
      public routes: RouteConfig[];
      constructor(
        tunIp: string,
        blockedAppName: string
      ) {
        this.addresses = [
          new AddressWithPrefix(new Address(tunIp, 1), 24), // IPv4地址
          new AddressWithPrefix(new Address('::1', 2), 128)  // IPv6地址
        ];
        this.mtu = 1400;
        // DNS查询不通过VPN，使用系统默认DNS
        this.dnsAddresses = ['114.114.114.114'];  // 设置 DNS 服务器地址
        // ⚠️ 重要：将VPN Proxy Server添加到信任列表，避免路由循环
        // VPN Proxy Server需要直接访问真实网络来转发流量
        this.trustedApplications = ['com.hellen.vpnserver'];  // VPN代理服务器需要排除在VPN路由之外
        this.blockedApplications = [blockedAppName];  // 被阻止的应用是传入的 `blockedAppName`

        // 配置路由：默认路由，所有IPv4流量通过VPN
        this.routes = [
          {
            interface: 'vpn-tun',  // 虚拟接口名称（通过 VPN 隧道通信的接口）
            destination: {
              address: new Address('0.0.0.0', 1),  // 所有IPv4地址（默认路由）
              prefixLength: 0,  // 目标网络的子网掩码
              port: 0  // 目标端口
            },
            gateway: {
              address: '192.168.100.1',  // VPN网关地址（必须是192.168.100.1）
              family: 1,  // 地址族（IPv4）
              port: 0  // 网关端口
            },
            hasGateway: true,  // 该路由有网关
            isDefaultRoute: true  // 该路由是默认路由
          }
        ];
      }
    }

    let vpnConfig = new Config(tunIp, blockedAppName);

    // [Start vpn_config_parameters]
    // ⚠️ 检查扩展能力是否已经创建了VPN连接
    if (gTunFd >= 0) {
      hilog.info(0x0000, 'ZBQ', '[UI] ⚠️ VPN连接已存在，gTunFd: %{public}d', gTunFd);
      hilog.info(0x0000, 'ZBQ', '[UI] 📝 直接启动数据转发线程...');
      if (gTunnelFd < 0) {
        hilog.error(0x0000, 'ZBQ', '[UI] ❌❌❌ 无法启动VPN：tunnelFd无效 ❌❌❌');
        hilog.error(0x0000, 'ZBQ', '[UI] ❌ gTunnelFd=%{public}d，请先创建并保护隧道', gTunnelFd);
        showToast('VPN start failed: 请先创建并保护隧道', 4000);
        this.flag = 'Error';
        return;
      }
      hilog.info(0x0000, 'ZBQ', '[UI] 📞 调用 vpn_client.startVpn(%{public}d, %{public}d)...', gTunFd, gTunnelFd);
      vpn_client.startVpn(gTunFd, gTunnelFd);
      hilog.info(0x0000, 'ZBQ', '[UI] ✅✅✅ VPN数据转发线程已启动 ✅✅✅');
      showToast('VPN started successfully', 4000);
      this.flag = 'Pass';
      notificationOperations.publishNotificationWithWantAgent();
      return;
    }

    // 检查tunnelFd是否有效
    if (gTunnelFd < 0) {
      hilog.error(0x0000, 'ZBQ', '[UI] ❌❌❌ 无法创建VPN连接：tunnelFd无效 ❌❌❌');
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ gTunnelFd=%{public}d', gTunnelFd);
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 请先依次点击：1)创建隧道 2)保护隧道');
      showToast('VPN start failed: 请先创建并保护隧道', 4000);
      this.flag = 'Error';
      return;
    }

    hilog.info(0x0000, 'ZBQ', '[UI] 🚀 准备创建VPN连接...');
    hilog.info(0x0000, 'ZBQ', '[UI] 📋 VPN配置:');
    hilog.info(0x0000, 'ZBQ', '[UI]   - TUN IP: %{public}s', tunIp);
    hilog.info(0x0000, 'ZBQ', '[UI]   - 前缀长度: 24');
    hilog.info(0x0000, 'ZBQ', '[UI]   - 被阻止应用: %{public}s', blockedAppName);
    hilog.info(0x0000, 'ZBQ', '[UI] 📞 获取UI Context...');
    let context = this.getUIContext().getHostContext() as common.VpnExtensionContext;
    hilog.info(0x0000, 'ZBQ', '[UI] ✅ Context已获取');
    hilog.info(0x0000, 'ZBQ', '[UI] 📞 调用 vpnExtension.createVpnConnection(context)...');
    let vpnConnection: vpnExtension.VpnConnection = vpnExtension.createVpnConnection(context);
    hilog.info(0x0000, 'ZBQ', '[UI] ✅ VpnConnection已创建');
    hilog.info(0x0000, 'ZBQ', '[UI] 📞 调用 vpnConnection.create(config)...');
    // 创建 VPN 连接并应用配置
    vpnConnection.create(vpnConfig).then((data) => {
      hilog.info(0x0000, 'ZBQ', '[UI] ✅✅✅ VPN连接创建成功 ✅✅✅');
      hilog.info(0x0000, 'ZBQ', '[UI] ✅ TUN设备文件描述符: FD = %{public}d', data);
      // [StartExclude vpn_config_parameters]
      setTunFd(data);  // 获取到的虚拟网络接口文件描述符
      hilog.info(0x0000, 'ZBQ', '[UI] ✅ 全局变量已设置: gTunFd = %{public}d', gTunFd);
      hilog.info(0x0000, 'ZBQ', '[UI] 📋 当前状态: gTunFd=%{public}d, gTunnelFd=%{public}d', gTunFd, gTunnelFd);

      // 启动 VPN 连接，传入隧道文件描述符和虚拟网络接口文件描述符
      hilog.info(0x0000, 'ZBQ', '[UI] 📞 调用 vpn_client.startVpn(%{public}d, %{public}d)...', gTunFd, gTunnelFd);
      vpn_client.startVpn(gTunFd, gTunnelFd);
      hilog.info(0x0000, 'ZBQ', '[UI] ✅✅✅ VPN数据转发线程已启动 ✅✅✅');
      hilog.info(0x0000, 'ZBQ', '[UI] 📝 VPN已完全启动，可以开始使用');
      showToast('VPN started successfully', 4000);
      this.flag = 'Pass';  // 设置状态为成功

      notificationOperations.publishNotificationWithWantAgent();
      // [EndExclude vpn_config_parameters]
    })
    // [End vpn_config_parameters]
      .catch((err: BusinessError) => {
      // 如果 VPN 创建失败，捕获异常并处理
      hilog.error(0x0000, 'ZBQ', '[UI] ❌❌❌ VPN连接创建失败 ❌❌❌');
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 错误代码: %{public}d', err.code ?? -1);
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 错误消息: %{public}s', err.message ?? '');
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 错误详情: %{public}s', JSON.stringify(err) ?? '');
      showToast('VPN start failed: ' + JSON.stringify(err), 4000);
      this.flag = 'Error';  // 设置状态为失败
    });
    hilog.info(0x0000, 'ZBQ', '[UI] ========== 打开VPN调用完成 ==========');
  }

  // 测试VPN路由是否生效
  testVpnRouting() {
    hilog.info(0x0000, 'testTag', '*** Testing VPN routing effectiveness ***');

    // 延迟一秒后测试，确保VPN完全启动
    setTimeout(() => {
      hilog.info(0x0000, 'testTag', '*** Sending test packet through TUN device ***');

      // 创建一个简单的测试数据包 - 模拟DNS查询
      const testData: Uint8Array = new Uint8Array([0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x07, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00, 0x01, 0x00, 0x01]);

      // 尝试直接写入TUN设备
      if (gTunFd >= 0) {
        // 这里应该通过TUN设备发送测试数据
        hilog.info(0x0000, 'testTag', '*** TUN device FD available: %{public}d ***', gTunFd);
        hilog.info(0x0000, 'testTag', '*** Test packet should be routed through VPN tunnel ***');

        // 注意：这里我们不能直接写入TUN设备，因为那是内核的工作
        // 我们应该检查是否有数据从TUN设备被读取并发送到服务器
        hilog.info(0x0000, 'testTag', '*** Please check VPNServer logs to see if test traffic is received ***');
      } else {
        hilog.error(0x0000, 'testTag', '*** TUN device FD not available ***');
      }

      // 显示路由测试提示
      showToast('VPN routing test started - check server logs', 3000);
      hilog.info(0x0000, 'testTag', '*** VPN routing test completed ***');

    }, 1000);
  }

  // ✅ 新增：启动数据转发线程
  StartDataForward() {
    hilog.error(0x0000, 'ZBQ', '[启动数据转发] 按钮被点击 gTunFd=%{public}d gTunnelFd=%{public}d', gTunFd, gTunnelFd);
    
    if (gTunFd < 0) {
      hilog.error(0x0000, 'ZBQ', '[启动数据转发] ❌ 错误：VPN连接未创建 gTunFd=%{public}d，请先点击"启动VPN"按钮', gTunFd);
      showToast('VPN连接未创建，请先点击"启动VPN"按钮', 4000);
      return;
    }
    
    if (gTunnelFd < 0) {
      hilog.error(0x0000, 'ZBQ', '[启动数据转发] ❌ 错误：UDP隧道未创建 gTunnelFd=%{public}d，请先启动VPN拓展程序', gTunnelFd);
      showToast('UDP隧道未创建，请先启动VPN拓展程序', 4000);
      return;
    }

    vpn_client.startVpn(gTunFd, gTunnelFd);
    hilog.error(0x0000, 'ZBQ', '[启动数据转发] ✅ 数据转发线程已启动 gTunFd=%{public}d gTunnelFd=%{public}d', gTunFd, gTunnelFd);
    showToast('数据转发线程已启动', 4000);
    this.flag = 'Pass';
    notificationOperations.publishNotificationWithWantAgent();
  }
}
