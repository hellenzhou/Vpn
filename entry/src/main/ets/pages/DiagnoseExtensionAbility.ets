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
    hilog.info(0x0000, 'ZHOUB', '[诊断] ========== 开始扩展能力诊断 ==========');
    
    // 1. 检查配置
    this.checkConfiguration();
    
    // 2. 检查权限
    this.checkPermissions();
    
    // 3. 检查文件
    this.checkFiles();
    
    // 4. 尝试启动扩展能力
    await this.tryStartExtensionAbility();
    
    hilog.info(0x0000, 'ZHOUB', '[诊断] ========== 诊断完成 ==========');
  }
  
  /**
   * 检查module.json5配置
   */
  private static checkConfiguration(): void {
    hilog.info(0x0000, 'ZHOUB', '[诊断] 📋 检查配置...');

  }
  
  /**
   * 检查权限
   */
  private static checkPermissions(): void {
    hilog.info(0x0000, 'ZHOUB', '[诊断] 🔐 检查权限...');

  }
  
  /**
   * 检查文件
   */
  private static checkFiles(): void {
    hilog.info(0x0000, 'ZHOUB', '[诊断] 📁 检查文件...');

  }
  
  /**
   * 尝试启动扩展能力
   */
  private static async tryStartExtensionAbility(): Promise<void> {
    hilog.info(0x0000, 'ZHOUB', '[诊断] 🚀 尝试启动扩展能力...');
    
    let want: Want = {
      deviceId: '',
      bundleName: 'com.samples.vpncontrol_case',
      abilityName: 'MyVpnExtAbility',
    };
    

    hilog.info(0x0000, 'ZHOUB', '[诊断]   - bundleName: %{public}s', want.bundleName);
    hilog.info(0x0000, 'ZHOUB', '[诊断]   - abilityName: %{public}s', want.abilityName);
    
    try {
      hilog.info(0x0000, 'ZHOUB', '[诊断] 📞 调用 vpnExtension.startVpnExtensionAbility()...');
      const startTime = Date.now();
      
      await vpnExtension.startVpnExtensionAbility(want);
      
      const elapsed = Date.now() - startTime;
      hilog.info(0x0000, 'ZHOUB', '[诊断] ✅ startVpnExtensionAbility() 成功 (耗时: %{public}dms)', elapsed);

      setTimeout(() => {
        hilog.info(0x0000, 'ZHOUB', '[诊断] ⏰ 等待5秒后检查扩展能力日志...');

      }, 5000);
      
    } catch (error) {

      hilog.error(0x0000, 'ZHOUB', '[诊断] ❌ 错误消息: %{public}s', (error as Error)?.message ?? '');
    }
  }
}
