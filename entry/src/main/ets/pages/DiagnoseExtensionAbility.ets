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

import { hilog } from '@kit.PerformanceAnalysisKit';
import { vpnExtension } from '@kit.NetworkKit';
import { Want } from '@kit.AbilityKit';

/**
 * 扩展能力诊断工具
 * 用于排查VPN扩展能力无法启动的问题
 */
export class DiagnoseExtensionAbility {
  
  /**
   * 执行完整的诊断流程
   */
  static async diagnose(): Promise<void> {
    hilog.info(0x0000, 'ZBQ', '[诊断] ========== 开始扩展能力诊断 ==========');
    
    // 1. 检查配置
    this.checkConfiguration();
    
    // 2. 检查权限
    this.checkPermissions();
    
    // 3. 检查文件
    this.checkFiles();
    
    // 4. 尝试启动扩展能力
    await this.tryStartExtensionAbility();
    
    hilog.info(0x0000, 'ZBQ', '[诊断] ========== 诊断完成 ==========');
  }
  
  /**
   * 检查module.json5配置
   */
  private static checkConfiguration(): void {
    hilog.info(0x0000, 'ZBQ', '[诊断] 📋 检查配置...');
    hilog.info(0x0000, 'ZBQ', '[诊断] 预期配置:');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - extensionAbilities[].name: MyVpnExtAbility');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - extensionAbilities[].srcEntry: ./ets/vpnability/VPNExtentionAbility.ets');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - extensionAbilities[].type: vpn');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - extensionAbilities[].exported: true');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - requestPermissions[].name: ohos.permission.MANAGE_VPN');
  }
  
  /**
   * 检查权限
   */
  private static checkPermissions(): void {
    hilog.info(0x0000, 'ZBQ', '[诊断] 🔐 检查权限...');
    hilog.info(0x0000, 'ZBQ', '[诊断] 必需权限:');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - ohos.permission.INTERNET');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - ohos.permission.MANAGE_VPN (关键权限)');
    hilog.info(0x0000, 'ZBQ', '[诊断] ⚠️ 如果MANAGE_VPN权限未授予，扩展能力将无法启动');
  }
  
  /**
   * 检查文件
   */
  private static checkFiles(): void {
    hilog.info(0x0000, 'ZBQ', '[诊断] 📁 检查文件...');
    hilog.info(0x0000, 'ZBQ', '[诊断] 预期文件:');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - VpnClient/entry/src/main/ets/vpnability/VPNExtentionAbility.ets');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - 文件应包含: export default class MyVpnExtAbility extends VpnExtensionAbility');
  }
  
  /**
   * 尝试启动扩展能力
   */
  private static async tryStartExtensionAbility(): Promise<void> {
    hilog.info(0x0000, 'ZBQ', '[诊断] 🚀 尝试启动扩展能力...');
    
    let want: Want = {
      deviceId: '',
      bundleName: 'com.samples.vpncontrol_case',
      abilityName: 'MyVpnExtAbility',
    };
    
    hilog.info(0x0000, 'ZBQ', '[诊断] Want配置:');
    hilog.info(0x0000, 'ZBQ', '[诊断]   - bundleName: %{public}s', want.bundleName);
    hilog.info(0x0000, 'ZBQ', '[诊断]   - abilityName: %{public}s', want.abilityName);
    
    try {
      hilog.info(0x0000, 'ZBQ', '[诊断] 📞 调用 vpnExtension.startVpnExtensionAbility()...');
      const startTime = Date.now();
      
      await vpnExtension.startVpnExtensionAbility(want);
      
      const elapsed = Date.now() - startTime;
      hilog.info(0x0000, 'ZBQ', '[诊断] ✅ startVpnExtensionAbility() 成功 (耗时: %{public}dms)', elapsed);
      hilog.info(0x0000, 'ZBQ', '[诊断] ⚠️ 注意：Promise resolved 不代表扩展能力已启动');
      hilog.info(0x0000, 'ZBQ', '[诊断] ⚠️ 需要检查扩展能力的onCreate()是否被调用');
      
      // 等待一段时间，检查扩展能力是否启动
      setTimeout(() => {
        hilog.info(0x0000, 'ZBQ', '[诊断] ⏰ 等待5秒后检查扩展能力日志...');
        hilog.info(0x0000, 'ZBQ', '[诊断] 📝 请在Hilog中搜索 "扩展能力" 或 "ZBQ"');
        hilog.info(0x0000, 'ZBQ', '[诊断] 📝 如果看不到日志，可能的原因：');
        hilog.info(0x0000, 'ZBQ', '[诊断]   1. 扩展能力进程启动失败');
        hilog.info(0x0000, 'ZBQ', '[诊断]   2. 权限未授予');
        hilog.info(0x0000, 'ZBQ', '[诊断]   3. 文件路径错误');
        hilog.info(0x0000, 'ZBQ', '[诊断]   4. 类名或导出错误');
        hilog.info(0x0000, 'ZBQ', '[诊断]   5. 扩展能力运行在独立进程，日志在不同进程ID');
      }, 5000);
      
    } catch (error) {
      hilog.error(0x0000, 'ZBQ', '[诊断] ❌ startVpnExtensionAbility() 失败');
      hilog.error(0x0000, 'ZBQ', '[诊断] ❌ 错误: %{public}s', JSON.stringify(error) ?? '');
      hilog.error(0x0000, 'ZBQ', '[诊断] ❌ 错误消息: %{public}s', (error as Error)?.message ?? '');
    }
  }
}
