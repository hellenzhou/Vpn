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
import { gTunFd, gTunnelFd, setTunFd, setTunnelFd } from '../vpnability/GlobalVpnState';

// 提取常量
const LABEL_FONT_SIZE = 20; // 标签字体大小
const INPUT_MARGIN = 2; // 输入框边距
const BUTTON_SUCCESS_MESSAGE = 4000; // Toast 显示时间
// [EndExclude start_vpn_extension_ability]

const TITLE_FONT_SIZE = 35; // 标题字体大小
const BUTTON_FONT_SIZE = 25; // 按钮字体大小
const BUTTON_MARGIN = 16;

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
  // VPN 服务器配置：
  // - 同设备应用间通信：使用 127.0.0.1（本地回环，避免路由问题）
  // - 不同设备通信：可改为实际服务器IP，例如 10.20.23.147
  @State vpnServerIp: string = '127.0.0.1'; // ✅ 修复：同设备应用间通信使用 localhost
  @State icmpTargetIp: string = '223.5.5.5'; // ICMP测试目标（可改为公网可达IP）
  // ⚠️ 重要：TUN IP是VPN虚拟网络的IP（192.168.100.x段），与物理网络无关
  @State tunIp: string = '192.168.100.2';  // VPN虚拟网络中的客户端IP
  @State prefix: string = '24';
  @State blockedAppName: string = 'com.hellen.vpnserver'; // 示例
  @State flag: string = 'wait';

  // Note: This is a regular page with UIAbilityContext, not VpnExtensionContext
  // VPN operations that require VpnExtensionContext should be done in VPNExtentionAbility.ets

  CreateTunnel() {
    let tunnelFd = vpn_client.udpConnect(this.vpnServerIp, 8888);
    setTunnelFd(tunnelFd);
    if (tunnelFd >= 0) {
      showToast('CreateTunnel Success', BUTTON_SUCCESS_MESSAGE);
      hilog.info(0x0000, 'testTag', 'developTag', '[ZHOUB] CreateTunnel Success, tunnelFd: %{public}d', tunnelFd);
      this.flag = 'Pass';
    } else {
      showToast('CreateTunnel Fail', BUTTON_SUCCESS_MESSAGE);
      hilog.error(0x0000, 'ZHOUB', '[主页] ❌ CreateTunnel Fail, tunnelFd: %{public}d', tunnelFd);
      this.flag = 'Error';
    }
  }

  IcmpEchoTest() {
    hilog.info(0x0000, 'ZHOUB', '[主页] 📡 开始ICMP Echo测试: %{public}s', this.icmpTargetIp);
    let result = vpn_client.icmpEchoTest(this.icmpTargetIp, 1000);
    if (result === 0) {
      showToast('✅ ICMP Echo成功', BUTTON_SUCCESS_MESSAGE);
      hilog.info(0x0000, 'ZHOUB', '[主页] ✅ ICMP Echo成功');
    } else {
      showToast('❌ ICMP Echo失败/超时', BUTTON_SUCCESS_MESSAGE);
      hilog.error(0x0000, 'ZHOUB', '[主页] ❌ ICMP Echo失败/超时');
    }
  }

  Protect() {
    let tunnelFd = gTunnelFd;
    hilog.info(0x0000, 'testTag', 'developTag', '[ZHOUB] vpn Protect, tunnelFd: %{public}d', tunnelFd);
    // Note: Protect operation requires VpnExtensionContext
    // This should be called from VPNExtentionAbility.ets, not from UI page
    showToast('Protect should be called from VPN Extension Ability', BUTTON_SUCCESS_MESSAGE);
    hilog.info(0x0000, 'testTag', 'developTag', '[ZHOUB] Protect operation requires VPN Extension context');
    this.flag = 'Pass';
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
          TextInput({ text: this.vpnServerIp })
            .onChange((value) => {
              this.vpnServerIp = value;
            })
            .width('60%')
            .margin(INPUT_MARGIN)
        }.width('90%');

        Row() {
          Text('icmp target')
            .fontSize(LABEL_FONT_SIZE)
            .width('40%')
            .textAlign(TextAlign.Center)
            .backgroundColor(0xAFEEEE);
          TextInput({ text: this.icmpTargetIp })
            .onChange((value) => {
              this.icmpTargetIp = value;
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
          TextInput({ text: this.tunIp })
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
        Button('ICMP Echo Test (SOCK_DGRAM)')
          .onClick(() => {
            this.IcmpEchoTest();
          })
          .width('70%')
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
        // 🔥 橘色一键启动按钮：自动执行 创建隧道→保护隧道→开启VPN
        Button('🚀 一键启动VPN (创建隧道→保护隧道→开启VPN)')
          .onClick(() => {
            try {
              vpnExtension.startVpnExtensionAbility(want)
                .then(() => {
                  hilog.info(0x0000, 'ZHOUB', '[主页] VPN扩展能力启动成功');
                  showToast('✅ VPN扩展能力已启动\n自动执行：创建隧道→保护隧道→开启VPN', BUTTON_SUCCESS_MESSAGE);
                  this.flag = 'Pass';
                })
            } catch (err) {
              hilog.error(0x0000, 'ZHOUB', '[主页] VPN扩展能力启动失败: %{public}s', JSON.stringify(err) ?? '');
              showToast('❌ 启动失败: ' + JSON.stringify(err), BUTTON_SUCCESS_MESSAGE);
              this.flag = 'Error';
            }
            // [EndExclude start_vpn_extension_ability]
          })
          .width('70%')
          // [StartExclude start_vpn_extension_ability]
          .id(ComponentId.START_VPN_EXT_BUTTON)
          // [EndExclude start_vpn_extension_ability]
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN)
          .backgroundColor(0xFF6B35) // 🔥 橘色，突出显示一键启动按钮
        
        // 保留原有的启动按钮（用于测试）
        Button($r('app.string.start_vpnExt'))
          .onClick(() => {
            // [StartExclude start_vpn_extension_ability]
            try {
              // [EndExclude start_vpn_extension_ability]
              vpnExtension.startVpnExtensionAbility(want)
              // [StartExclude start_vpn_extension_ability]
                .then(() => {
                  showToast('start vpnExt Success', BUTTON_SUCCESS_MESSAGE);
                  hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'start vpnExt Success');
                  this.flag = 'Pass';
                })
            } catch (err) {
              showToast('start vpnExt Fail: ' + JSON.stringify(err), BUTTON_SUCCESS_MESSAGE);
              this.flag = 'Error';
              hilog.error(0x0000, 'ZHOUB', '[主页] ❌ start vpnExt Fail: %{public}s', JSON.stringify(err) ?? '');
            }
            // [EndExclude start_vpn_extension_ability]
          })
          .width('70%')
          // [StartExclude start_vpn_extension_ability]
          .id(ComponentId.START_VPN_EXT_BUTTON)
          // [EndExclude start_vpn_extension_ability]
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN)
        // [StartExclude start_vpn_extension_ability]
        SetupVpn({ flag: this.flag})
        // [EndExclude start_vpn_extension_ability]
      }.width('100%');
    }.height('100%');

  }
}
// [End start_vpn_extension_ability]