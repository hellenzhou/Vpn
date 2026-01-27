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

import router from '@ohos.router';
import { BusinessError } from '@ohos.base';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { ComponentId } from '../common/CommonConstant';
import Logger from '../common/Logger';
import { notificationUtil } from '../notification/NotificationUtil';
import { showToast } from '../common/ShowToast';
import { VpnTest as GetVpnIdTest } from '../vpnability/GetVpnIdTest';
import { VpnTest as DestroyVpnTest } from '../vpnability/DestroyVpnTest';
import { vpnExtension } from '@kit.NetworkKit';
import { Want } from '@kit.AbilityKit';

// 常量定义
const TITLE_FONT_SIZE = 35; // 标题字体大小
const BUTTON_FONT_SIZE = 25; // 按钮字体大小
const BUTTON_MARGIN = 16; // 按钮边距
const TOAST_DURATION = 2000; // Toast 显示时间

@Entry
@Component
struct Index {
  @State message: string = 'VPN';
  @State flag: string = 'wait';
  getVpnIdTest: GetVpnIdTest = new GetVpnIdTest();
  destroyVpnTest: DestroyVpnTest = new DestroyVpnTest();

  aboutToAppear(): void {
    notificationUtil.enableNotification();
  }

  build() {
    Row() {
      Column() {
        Text(this.message)
          .fontSize(TITLE_FONT_SIZE)
          .fontWeight(FontWeight.Bold)
          .id(this.flag)
          .onClick(() => {
            hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'vpn Client');
          })

        // Start VPN 按钮
        Button($r('app.string.Start_VPN'))
          .onClick(async () => {
            hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'Succeeded in clicking the button.');
            try {
              await router.pushUrl({ url: 'pages/StartVpn' });
              hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'Succeeded in jumping to the second page.');
              this.flag = 'Pass';
            } catch (err) {
              Logger.error('developTag', 'Failed to jump to the second page: %{public}s',
                JSON.stringify(err) ?? '');
              this.flag = 'Error';
            }
          })
          .id(ComponentId.START_VPN_BUTTON)
          .width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN)

        // Stop VPN 按钮
        Button($r('app.string.Stop_VPN'))
          .onClick(async () => {
            hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'Succeeded in clicking the button.');
            try {
              await router.pushUrl({ url: 'pages/StopVpn' });
              hilog.info(0x0000, 'testTag', 'developTag', '%{public}s', 'Succeeded in jumping to the second page.');
              this.flag = 'Pass';
            } catch (err) {
              Logger.error('developTag', 'Failed to jump to the second page: %{public}s',
                JSON.stringify(err) ?? '');
              this.flag = 'Error';
            }
          })
          .id(ComponentId.STOP_VPN_BUTTON)
          .width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN);

        //生成VPN Id
        Button('getVpnId').onClick(() => {
          let vpnId: string = this.getVpnIdTest.getVpnId();
          showToast('vpn getVpnId Success', TOAST_DURATION);
          hilog.info(0x0000, 'testTag', vpnId);
        }).width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN);

        //断开VPN
        Button('destroy').onClick(() => {
          let idDestroy: boolean = this.destroyVpnTest.destroy();
          if (idDestroy) {
            showToast('vpn destroy Success', TOAST_DURATION);
          } else {
            showToast('vpn destroy fail', TOAST_DURATION);
          }
        }).width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN);

        // 🔥 诊断VPN状态和数据转发
        Button('🔍 诊断VPN状态')
          .onClick(() => {
            try {
              let vpnId: string = this.getVpnIdTest.getVpnId();
              if (vpnId) {
                hilog.info(0x0000, 'ZHOUB', '[诊断] VPN连接已创建 VPN ID=%{public}s', vpnId);
              } else {
                hilog.error(0x0000, 'ZHOUB', '[诊断] VPN连接未创建或获取失败');
              }
            } catch (err) {
              hilog.error(0x0000, 'ZHOUB', '[诊断] 获取VPN ID失败: %{public}s', JSON.stringify(err) ?? '');
            }
            showToast('诊断完成，请查看Hilog日志', TOAST_DURATION);
          })
          .width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN)
          .backgroundColor(0x4CAF50); // 绿色

        // 🔥 测试 Extension Ability onCreate()
        Button('🧪 启动VPN Extension Ability')
          .onClick(() => {
            let want: Want = {
              deviceId: '',
              bundleName: 'com.samples.vpncontrol_case',
              abilityName: 'MyVpnExtAbility',
            };
            
            try {
            vpnExtension.startVpnExtensionAbility(want)
              .then(() => {
                hilog.info(0x0000, 'ZHOUB', '[主页] VPN Extension Ability 启动请求已发送');
                hilog.info(0x0000, 'ZHOUB', '[主页] ⏰ 如果看不到[扩展能力]日志，请等待几秒后再次查看');
                hilog.info(0x0000, 'ZHOUB', '[主页] 📋 预期日志顺序：');
                hilog.info(0x0000, 'ZHOUB', '[主页]   1. [扩展能力] 🔥🔥🔥 MyVpnExtAbility 类已加载');
                hilog.info(0x0000, 'ZHOUB', '[主页]   2. [扩展能力] 🔥🔥🔥 onCreate() 开始执行');
                hilog.info(0x0000, 'ZHOUB', '[主页]   3. [扩展能力] 🔥🔥🔥 onRequest() 开始执行');
                hilog.info(0x0000, 'ZHOUB', '[主页] ⚠️ 重要提示：');
                hilog.info(0x0000, 'ZHOUB', '[主页]   - 扩展能力可能运行在独立进程，日志可能在不同的进程ID');
                hilog.info(0x0000, 'ZHOUB', '[主页]   - 请在Hilog中搜索 "扩展能力" 或 "ZHOUB"，不要只看当前进程的日志');
                hilog.info(0x0000, 'ZHOUB', '[主页]   - 如果看不到日志，尝试清除Hilog过滤器，查看所有进程的日志');
                hilog.info(0x0000, 'ZHOUB', '[主页]   - 检查是否有系统错误日志（搜索 "ERROR" 或 "FATAL"）');
                hilog.info(0x0000, 'ZHOUB', '[主页]   - 检查扩展能力进程是否崩溃（查看崩溃日志）');
                
                // 🔥 延迟检查：等待扩展能力启动
                setTimeout(() => {
                  hilog.info(0x0000, 'ZHOUB', '[主页] ⏰ 延迟检查1：已等待3秒，检查扩展能力是否已启动...');
                  hilog.info(0x0000, 'ZHOUB', '[主页] 🔍 如果仍然看不到[扩展能力]日志，可能的原因：');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   1. 扩展能力进程启动失败（检查module.json5配置）');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   2. onCreate()未被调用（检查扩展能力类型配置）');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   3. 日志被过滤（检查Hilog过滤条件）');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   4. 扩展能力运行在独立进程（检查进程日志）');
                  hilog.info(0x0000, 'ZHOUB', '[主页] 📝 排查建议：');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   - 在Hilog中搜索 "扩展能力" 或 "ZHOUB"');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   - 检查是否有错误日志（搜索 "ERROR" 或 "❌"）');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   - 确认module.json5中extensionAbilities配置正确');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   - 扩展能力可能运行在独立进程，查看所有进程的日志');
                }, 3000);
                
                setTimeout(() => {
                  hilog.info(0x0000, 'ZHOUB', '[主页] ⏰ 延迟检查2：已等待10秒，再次检查...');
                  hilog.info(0x0000, 'ZHOUB', '[主页] ⚠️ 如果10秒后仍然看不到[扩展能力]日志，说明扩展能力可能未启动');
                  hilog.info(0x0000, 'ZHOUB', '[主页] ⚠️ 请检查：');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   1. module.json5中MyVpnExtAbility配置是否正确');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   2. 文件路径是否正确: ./ets/vpnability/VPNExtentionAbility.ets');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   3. 扩展能力类型是否为 "vpn"');
                  hilog.info(0x0000, 'ZHOUB', '[主页]   4. exported是否为 true');
                }, 10000);
                
                showToast('✅ Extension Ability 已启动\n请查看 Hilog [扩展能力] 日志', TOAST_DURATION);
              })
              .catch((err: Error) => {
                hilog.error(0x0000, 'ZHOUB', '[主页] ❌ startVpnExtensionAbility() Promise rejected');
                hilog.error(0x0000, 'ZHOUB', '[主页] ❌ VPN Extension Ability 启动失败');
                hilog.error(0x0000, 'ZHOUB', '[主页] ❌ 错误对象: %{public}s', JSON.stringify(err) ?? '');
                hilog.error(0x0000, 'ZHOUB', '[主页] ❌ 错误消息: %{public}s', err?.message ?? '');
                hilog.error(0x0000, 'ZHOUB', '[主页] ❌ 错误堆栈: %{public}s', err?.stack ?? '');
                hilog.error(0x0000, 'ZHOUB', '[主页] 🔍 可能的原因:');
                hilog.error(0x0000, 'ZHOUB', '[主页]   1. 扩展能力配置错误（检查module.json5）');
                hilog.error(0x0000, 'ZHOUB', '[主页]   2. 权限未授予（检查ohos.permission.MANAGE_VPN）');
                hilog.error(0x0000, 'ZHOUB', '[主页]   3. 文件路径错误（检查VPNExtentionAbility.ets）');
                hilog.error(0x0000, 'ZHOUB', '[主页]   4. 类名或导出错误（检查export default class）');
                showToast('❌ 启动失败: ' + JSON.stringify(err), TOAST_DURATION);
              });
            
            hilog.info(0x0000, 'ZHOUB', '[主页] ========== 启动请求已发送 ==========');
            } catch (error) {
              hilog.error(0x0000, 'ZHOUB', '[主页] ❌ 启动扩展能力时发生异常');
              hilog.error(0x0000, 'ZHOUB', '[主页] ❌ 异常: %{public}s', JSON.stringify(error) ?? '');
              hilog.error(0x0000, 'ZHOUB', '[主页] ❌ 异常消息: %{public}s', (error as Error)?.message ?? '');
            }
          })
          .width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN)
          .backgroundColor(0xFF6B35); // 橙色，突出显示

      }.width('100%');
    }.height('100%');
  }
}
