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
            hilog.info(0x0000, 'ZHOUB', '[诊断] ========== VPN状态诊断 ==========');
            hilog.info(0x0000, 'ZHOUB', '[诊断] 📋 检查VPN状态...');
            
            // 检查VPN ID
            try {
              let vpnId: string = this.getVpnIdTest.getVpnId();
              hilog.info(0x0000, 'ZHOUB', '[诊断] VPN ID: %{public}s', vpnId || '未获取到');
              if (vpnId) {
                hilog.info(0x0000, 'ZHOUB', '[诊断] ✅ VPN连接已创建');
              } else {
                hilog.error(0x0000, 'ZHOUB', '[诊断] ❌ VPN连接未创建或获取失败');
              }
            } catch (err) {
              hilog.error(0x0000, 'ZHOUB', '[诊断] ❌ 获取VPN ID失败: %{public}s', JSON.stringify(err) ?? '');
            }
            
            hilog.info(0x0000, 'ZHOUB', '[诊断] 📝 请查看原生代码日志（napi_init.cpp）:');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   - 搜索 "ZHOUB" 查看数据包转发日志');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   - 搜索 "从TUN读取" 查看是否读取到数据包');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   - 搜索 "转发成功" 查看是否成功转发');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   - 搜索 "转发失败" 查看转发错误');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   - 搜索 "转发统计" 查看转发统计信息');
            hilog.info(0x0000, 'ZHOUB', '[诊断] ⚠️ 如果看不到原生代码日志，可能原因：');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   1. 数据转发线程未启动（检查startVpn是否被调用）');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   2. TUN设备未创建（检查VPN连接是否创建成功）');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   3. UDP隧道未创建（检查CreateTunnel是否成功）');
            hilog.info(0x0000, 'ZHOUB', '[诊断]   4. 日志被过滤（检查Hilog过滤条件）');
            hilog.info(0x0000, 'ZHOUB', '[诊断] ====================================');
            
            showToast('诊断完成，请查看Hilog日志', TOAST_DURATION);
          })
          .width('70%')
          .fontSize(BUTTON_FONT_SIZE)
          .margin(BUTTON_MARGIN)
          .backgroundColor(0x4CAF50); // 绿色

        // 🔥 测试 Extension Ability onCreate()
        Button('🧪 启动VPN Extension Ability')
          .onClick(() => {
            hilog.info(0x0000, 'ZHOUB', '[主页] ========== 准备启动 VPN Extension Ability ==========');
            hilog.info(0x0000, 'ZHOUB', '[主页] 🔥 按钮点击事件触发');
            
            // 🔥 添加详细的配置验证
            hilog.info(0x0000, 'ZHOUB', '[主页] 📋 配置验证:');
            hilog.info(0x0000, 'ZHOUB', '[主页]   - bundleName: com.samples.vpncontrol_case');
            hilog.info(0x0000, 'ZHOUB', '[主页]   - abilityName: MyVpnExtAbility');
            hilog.info(0x0000, 'ZHOUB', '[主页]   - 文件路径: ./ets/vpnability/VPNExtentionAbility.ets');
            hilog.info(0x0000, 'ZHOUB', '[主页]   - 扩展能力类型: vpn');
            hilog.info(0x0000, 'ZHOUB', '[主页]   - exported: true');
            hilog.info(0x0000, 'ZHOUB', '[主页]   - 必需权限: ohos.permission.MANAGE_VPN');
            
            let want: Want = {
              deviceId: '',
              bundleName: 'com.samples.vpncontrol_case',
              abilityName: 'MyVpnExtAbility',
            };
            
            hilog.info(0x0000, 'ZHOUB', '[主页] 📋 Want配置:');
            hilog.info(0x0000, 'ZHOUB', '[主页]   - bundleName: %{public}s', want.bundleName);
            hilog.info(0x0000, 'ZHOUB', '[主页]   - abilityName: %{public}s', want.abilityName);
            hilog.info(0x0000, 'ZHOUB', '[主页] 📞 调用 vpnExtension.startVpnExtensionAbility()...');
            hilog.info(0x0000, 'ZHOUB', '[主页] ⏰ 开始时间: %{public}s', new Date().toISOString());
            
            // 🔥 添加错误捕获和详细日志
            try {
            
            // 🔥 添加超时检查
            let startTime = Date.now();
            vpnExtension.startVpnExtensionAbility(want)
              .then(() => {
                let elapsed = Date.now() - startTime;
                hilog.info(0x0000, 'ZHOUB', '[主页] ✅ startVpnExtensionAbility() Promise resolved (耗时: %{public}dms)', elapsed);
                hilog.info(0x0000, 'ZHOUB', '[主页] ✅ VPN Extension Ability 启动请求已发送！');
                hilog.info(0x0000, 'ZHOUB', '[主页] ⚠️ 注意：Promise resolved 不代表扩展能力已启动，只是请求已发送');
                hilog.info(0x0000, 'ZHOUB', '[主页] 🔍 请查看 Hilog 中的 [扩展能力] 日志！');
                hilog.info(0x0000, 'ZHOUB', '[主页] ⏰ 注意：扩展能力的onCreate()和onRequest()可能稍后才会被调用');
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
