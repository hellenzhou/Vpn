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

// [Start start_vpn_extension_ability]
import { common, Want } from '@kit.AbilityKit';
import { vpnExtension } from '@kit.NetworkKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
// [StartExclude start_vpn_extension_ability]
import vpn_client from 'libentry.so';
import { showToast } from '../common/ShowToast';
import { ComponentId } from '../common/CommonConstant'; // 引入枚举
import TitleBar from '../common/component';
import NotificationOperations from '../notification/NotificationOperations';
import SetupVpn from './SetupVpn';
import { gTunFd, gTunnelFd, setTunnelFd } from '../vpnability/GlobalVpnState';
import { MyVpnExtAbility } from '../vpnability/VPNExtentionAbility';

// 提取常量
const LABEL_FONT_SIZE = 20; // 标签字体大小
const INPUT_MARGIN = 2; // 输入框边距
const BUTTON_SUCCESS_MESSAGE = 4000; // Toast 显示时间

const TITLE_FONT_SIZE = 35; // 标题字体大小
const BUTTON_FONT_SIZE = 25; // 按钮字体大小
const BUTTON_MARGIN = 16;

// 局部变量（类似官方DEMO）
let g_tunnelFd = -1;

let want: Want = {
  deviceId: '',
  bundleName: 'com.samples.vpncontrol_case',
  abilityName: 'MyVpnExtAbility',
};

@Entry
@Component
struct StartVpn {
  @State message: string = 'VPN';
  // [StartExclude start_vpn_extension_ability]
  // ⚠️ 重要：VPN服务器IP配置
  // - 部署机器IP：10.20.23.147（可通过UI输入框修改）
  // - 本地测试：127.0.0.1
  // - ⚠️ 不要使用10.8段（局域网冲突）
  @State vpnServerIp: string = '127.0.0.1'; // VPN服务器IP（可改为10.20.23.147）
  // ⚠️ 重要：TUN IP是VPN虚拟网络的IP，与物理网络无关
  // - 使用192.168.100.x段（VPN虚拟网络）
  // - ⚠️ 不要改成10.8段或其他物理网络段
  @State tunIp: string = '192.168.100.2';  // VPN虚拟网络中的客户端IP（不要修改为物理网络IP）
  @State prefix: string = '24';
  @State blockedAppName: string = 'com.example.baidumyapplication'; // 示例
  @State flag: string = 'wait';

  private context = getContext(this) as common.VpnExtensionContext;
  private vpnConnection: vpnExtension.VpnConnection = vpnExtension.createVpnConnection(this.context);
  private notificationOperations: NotificationOperations = new NotificationOperations(getContext(this));

  CreateTunnel() {
    hilog.info(0x0000, 'ZBQ', '[UI] ========== 手动创建UDP隧道 ==========');
    hilog.info(0x0000, 'ZBQ', '[UI] 📋 当前配置:');
    hilog.info(0x0000, 'ZBQ', '[UI]   - VPN服务器IP: %{public}s', this.vpnServerIp);
    hilog.info(0x0000, 'ZBQ', '[UI]   - VPN服务器端口: 8888');
    hilog.info(0x0000, 'ZBQ', '[UI] 📞 调用原生函数 vpn_client.udpConnect(%{public}s, 8888)...', this.vpnServerIp);
    hilog.info(0x0000, 'ZBQ', '[UI] ⏳ 等待udpConnect()返回...');
    
    let tunnelFd: number = -999;
    try {
      tunnelFd = vpn_client.udpConnect(this.vpnServerIp, 8888);
    } catch (error) {
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ udpConnect() 抛出异常');
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 异常详情: %{public}s', JSON.stringify(error) ?? '');
      showToast('CreateTunnel Fail: ' + JSON.stringify(error), BUTTON_SUCCESS_MESSAGE);
      this.flag = 'Error';
      return;
    }
    
    hilog.info(0x0000, 'ZBQ', '[UI] 📥 udpConnect() 返回: tunnelFd = %{public}d', tunnelFd);
    
    if (tunnelFd >= 0) {
      g_tunnelFd = tunnelFd;
      setTunnelFd(tunnelFd); // 同步到全局变量，供SetupVpn使用
      hilog.info(0x0000, 'ZBQ', '[UI] ✅✅✅ UDP隧道创建成功 ✅✅✅');
      hilog.info(0x0000, 'ZBQ', '[UI] ✅ 隧道文件描述符: FD = %{public}d', tunnelFd);
      hilog.info(0x0000, 'ZBQ', '[UI] ✅ 全局变量已设置: gTunnelFd = %{public}d', gTunnelFd);
      showToast('CreateTunnel Success', BUTTON_SUCCESS_MESSAGE);
      hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'CreateTunnel Success');
      this.flag = 'Pass';
    } else {
      hilog.error(0x0000, 'ZBQ', '[UI] ❌❌❌ 创建UDP隧道失败 ❌❌❌');
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 返回的FD: %{public}d', tunnelFd);
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 可能原因: 1)VPN服务器未启动 2)网络不可达 3)socket创建失败');
      showToast('CreateTunnel Fail', BUTTON_SUCCESS_MESSAGE);
      hilog.error(0x0000, 'testTag', 'developTag', 'CreateTunnel Fail %{public}s', 'CreateTunnel Fail');
      this.flag = 'Error';
    }
    hilog.info(0x0000, 'ZBQ', '[UI] ========== 创建UDP隧道完成 ==========');
  }

  Protect() {
    hilog.info(0x0000, 'ZBQ', '[UI] ========== 手动保护UDP隧道 ==========');
    hilog.info(0x0000, 'ZBQ', '[UI] 📋 当前状态:');
    hilog.info(0x0000, 'ZBQ', '[UI]   - g_tunnelFd: %{public}d', g_tunnelFd);
    hilog.info(0x0000, 'ZBQ', '[UI]   - gTunnelFd: %{public}d', gTunnelFd);
    hilog.info(0x0000, 'ZBQ', '[UI]   - vpnConnection状态: %{public}s', this.vpnConnection ? '已创建' : '未创建');
    
    if (g_tunnelFd < 0 && gTunnelFd < 0) {
      hilog.error(0x0000, 'ZBQ', '[UI] ❌❌❌ 无法保护隧道：tunnelFd无效 ❌❌❌');
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ g_tunnelFd=%{public}d, gTunnelFd=%{public}d', g_tunnelFd, gTunnelFd);
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 请先点击"创建隧道"按钮创建UDP隧道');
      showToast('Protect Fail: 请先创建隧道', BUTTON_SUCCESS_MESSAGE);
      this.flag = 'Error';
      return;
    }
    
    // 使用有效的tunnelFd
    let tunnelFd = g_tunnelFd >= 0 ? g_tunnelFd : gTunnelFd;
    hilog.info(0x0000, 'ZBQ', '[UI] 🔒 准备保护UDP隧道，FD: %{public}d', tunnelFd);
    hilog.info(0x0000, 'ZBQ', '[UI] 📞 调用 vpnConnection.protect(%{public}d)...', tunnelFd);
    
    this.vpnConnection.protect(tunnelFd).then(() => {
      hilog.info(0x0000, 'ZBQ', '[UI] ✅✅✅ UDP隧道保护成功 ✅✅✅');
      hilog.info(0x0000, 'ZBQ', '[UI] ✅ UDP隧道已标记为绕过VPN路由表');
      hilog.info(0x0000, 'ZBQ', '[UI] 📝 下一步：点击"创建VPN连接"按钮创建VPN连接');
      showToast('vpn Protect Success', BUTTON_SUCCESS_MESSAGE);
      hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'vpn Protect Success');
      this.flag = 'Pass';
    }).catch((err: Error) => {
      hilog.error(0x0000, 'ZBQ', '[UI] ❌❌❌ UDP隧道保护失败 ❌❌❌');
      hilog.error(0x0000, 'ZBQ', '[UI] ❌ 错误详情: %{public}s', JSON.stringify(err) ?? '');
      showToast('vpn Protect Failed: ' + JSON.stringify(err), BUTTON_SUCCESS_MESSAGE);
      hilog.error(0x0000, 'testTag', 'developTag', 'vpn Protect Failed %{public}s', JSON.stringify(err) ?? '');
      this.flag = 'Error';
    });
    hilog.info(0x0000, 'ZBQ', '[UI] ========== 保护UDP隧道调用完成 ==========');
  }
  // [EndExclude start_vpn_extension_ability]
  build() {
    Row() {
      Column() {
        // [StartExclude start_vpn_extension_ability]
        TitleBar({ hasBackPress: true, title: 'BACK' })
        // [EndExclude start_vpn_extension_ability]
        Text(this.message)
          .fontSize(TITLE_FONT_SIZE)
          .fontWeight(FontWeight.Bold)
          // [StartExclude start_vpn_extension_ability]
          .id(this.flag)
          // [EndExclude start_vpn_extension_ability]
          .onClick(() => {
            hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'vpn Client');
          })
        // [StartExclude start_vpn_extension_ability]
        Row() {
          Text('server ip')
            .fontSize(LABEL_FONT_SIZE)
            .width('40%')
            .textAlign(TextAlign.Center)
            .backgroundColor(0xAFEEEE);
          // VPN服务器IP输入框（可输入10.20.23.147或127.0.0.1）
          TextInput({ text: '127.0.0.1' })
            .onChange((value) => {
              this.vpnServerIp = value;
            })
            .width('60%')
            .margin(INPUT_MARGIN)
        }.width('90%');

        Row() {
          Text('tun ip')
            .fontSize(LABEL_FONT_SIZE)
            .width('40%')
            .textAlign(TextAlign.Center)
            .backgroundColor(0xAFEEEE);
          TextInput({ text: '192.168.100.2' })
            .onChange((value) => {
              this.tunIp = value;
            })
            .width('60%')
            .margin(INPUT_MARGIN)
        }.width('90%')

        Row() {
          Text('blockedApp')
            .fontSize(LABEL_FONT_SIZE)
            .width('40%')
            .textAlign(TextAlign.Center)
            .backgroundColor(0xAFEEEE)
          TextInput({ text: 'com.example.baidumyapplication' })
            .onChange((value) => {
              this.blockedAppName = value;
            })
            .width('60%')
            .margin(INPUT_MARGIN)
        }.width('90%')

        Button($r('app.string.CreateTunnel'))
          .onClick(() => {
            this.CreateTunnel();
          })
          .width('70%')
          .id(ComponentId.CREATE_TUNNEL_BUTTON)
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN);
        Button($r('app.string.Protect'))
          .onClick(() => {
            this.Protect();
          })
          .width('70%')
          .id(ComponentId.PROTECT_BUTTON)
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN);
        // [EndExclude start_vpn_extension_ability]
        Button($r('app.string.start_vpnExt'))
          .onClick(() => {
            // [StartExclude start_vpn_extension_ability]
            hilog.info(0x0000, 'ZBQ', '[UI] ========== 开始启动VPN扩展能力 ==========');
            hilog.info(0x0000, 'ZBQ', '[UI] want配置: bundleName=%{public}s, abilityName=%{public}s', want.bundleName ?? '', want.abilityName ?? '');
            try {
              // [EndExclude start_vpn_extension_ability]
              hilog.info(0x0000, 'ZBQ', '[UI] 调用 vpnExtension.startVpnExtensionAbility()...');
              hilog.info(0x0000, 'ZBQ', '[UI] 🔥 强制启动扩展能力进程...');
              
              // 🔥 关键修复：使用更强制的方式启动扩展能力
              vpnExtension.startVpnExtensionAbility(want)
              // [StartExclude start_vpn_extension_ability]
                .then(() => {
                  hilog.info(0x0000, 'ZBQ', '[UI] ✅ startVpnExtensionAbility() Promise resolved (成功)');
                  hilog.info(0x0000, 'ZBQ', '[UI] 🔍 诊断：检查VPN扩展能力启动状态...');
                  hilog.info(0x0000, 'ZBQ', '[UI] 📋 当前时间: %{public}s', new Date().toISOString());
                  hilog.info(0x0000, 'ZBQ', '[UI] 📋 等待3秒后检查扩展能力进程日志...');
                  
                  // 延迟检查，给扩展能力时间启动
                  setTimeout(() => {
                    hilog.info(0x0000, 'ZBQ', '[UI] 🔍 延迟检查：3秒后检查扩展能力是否启动');
                    hilog.info(0x0000, 'ZBQ', '[UI] 📋 请检查日志中是否有以下内容：');
                    hilog.info(0x0000, 'ZBQ', '[UI]   1. [扩展能力] ========== onCreate() 被调用 ==========');
                    hilog.info(0x0000, 'ZBQ', '[UI]   2. [扩展能力] 🔥 测试：扩展能力进程已启动！');
                    hilog.info(0x0000, 'ZBQ', '[UI] 📋 如果没有看到上述日志，说明扩展能力未启动');
                    hilog.info(0x0000, 'ZBQ', '[UI] 📋 这将导致VPN路由表不生效');
                    
                    // 🔥 尝试调用扩展能力的测试方法
                    hilog.info(0x0000, 'ZBQ', '[UI] 🔍 尝试调用扩展能力的测试方法...');
                    this.testExtensionAbility();
                  }, 3000);
                  showToast('start vpnExt Success', BUTTON_SUCCESS_MESSAGE);
                  hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'start vpnExt Success');
                  this.flag = 'Pass';
                })
                .catch((err: Error) => {
                  hilog.error(0x0000, 'ZBQ', '[UI] ❌ startVpnExtensionAbility() Promise rejected (失败)');
                  hilog.error(0x0000, 'ZBQ', '[UI] 错误详情: %{public}s', JSON.stringify(err) ?? '');
                  showToast('start vpnExt Fail: ' + JSON.stringify(err), BUTTON_SUCCESS_MESSAGE);
                  this.flag = 'Error';
                });
            } catch (err) {
              hilog.error(0x0000, 'ZBQ', '[UI] ❌ startVpnExtensionAbility() 抛出异常');
              hilog.error(0x0000, 'ZBQ', '[UI] 异常详情: %{public}s', JSON.stringify(err) ?? '');
              showToast('start vpnExt Fail: ' + JSON.stringify(err), BUTTON_SUCCESS_MESSAGE);
              this.flag = 'Error';
              hilog.error(0x0000, 'testTag', 'developTag', 'start vpnExt Fail %{public}s', JSON.stringify(err) ?? '');
            }
            hilog.info(0x0000, 'ZBQ', '[UI] ========== 启动VPN扩展能力调用完成 ==========');
            // [EndExclude start_vpn_extension_ability]
          })
          .width('70%')
          // [StartExclude start_vpn_extension_ability]
          .id(ComponentId.START_VPN_EXT_BUTTON)
          // [EndExclude start_vpn_extension_ability]
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN);
        
        // 🔥 新增：测试扩展能力按钮
        Button("测试扩展能力")
          .onClick(() => {
            hilog.info(0x0000, 'ZBQ', '[UI] ========== 测试扩展能力 ==========');
            hilog.info(0x0000, 'ZBQ', '[UI] 🔍 尝试直接调用扩展能力的方法...');
            
            // 尝试创建扩展能力实例并调用测试方法
            try {
              // 这里我们尝试创建扩展能力的实例
              hilog.info(0x0000, 'ZBQ', '[UI] 📞 尝试创建扩展能力实例...');
              // 注意：这种方法可能不会工作，但值得一试
              const extensionAbility = new MyVpnExtAbility();
              const result = extensionAbility.testExtensionAbility();
              hilog.info(0x0000, 'ZBQ', '[UI] ✅ 扩展能力测试方法调用成功: %{public}s', result);
              showToast('ExtensionAbility Test Success', BUTTON_SUCCESS_MESSAGE);
            } catch (error) {
              hilog.error(0x0000, 'ZBQ', '[UI] ❌ 扩展能力测试失败: %{public}s', JSON.stringify(error));
              showToast('ExtensionAbility Test Failed', BUTTON_SUCCESS_MESSAGE);
            }
          })
          .width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN);
        // [StartExclude start_vpn_extension_ability]
        SetupVpn({ flag: this.flag})
        // [EndExclude start_vpn_extension_ability]
      }.width('100%');
    }.height('100%');
  }

  testExtensionAbility() {
    hilog.info(0x0000, 'ZBQ', '[UI] 🔍 测试扩展能力的方法...');
    // 这里我们可以添加测试扩展能力的方法
  }
}
// [End start_vpn_extension_ability]
