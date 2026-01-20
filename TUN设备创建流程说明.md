# VPN 客户端 TUN 设备创建流程详解

## 🎯 核心概念

在鸿蒙（HarmonyOS）VPN 客户端中，**TUN 设备由系统自动创建**，开发者只需：
1. 配置 VPN 参数
2. 调用系统 API
3. 系统返回 TUN 设备的文件描述符（FD）

**不需要像服务端那样手动打开 `/dev/net/tun` 并调用 `ioctl`！**

---

## 📋 完整流程

### **架构图：**

```
┌─────────────────────────────────────────────────┐
│  UI 层（StartVpn.ets）                          │
│  • 用户点击"启动 VPN"按钮                        │
│  • 触发 vpnExtension.startVpnExtensionAbility() │
└───────────────┬─────────────────────────────────┘
                │
                ↓
┌─────────────────────────────────────────────────┐
│  扩展能力层（VPNExtentionAbility.ets）           │
│                                                 │
│  onCreate() {                                   │
│    1. 创建 VpnConnection 对象                   │
│    2. 创建 UDP 隧道到服务器                      │
│    3. 保护 UDP 隧道（protect）                  │
│    4. 配置 VPN 参数（SetupVpn）                 │
│    5. 创建 TUN 设备（系统自动）                  │
│  }                                              │
└───────────────┬─────────────────────────────────┘
                │
                ↓
┌─────────────────────────────────────────────────┐
│  系统层（@kit.NetworkKit）                      │
│  • vpnExtension.createVpnConnection()           │
│  • vpnConnection.protect()                      │
│  • vpnConnection.create(config)                 │
│    → 系统创建 TUN 设备                          │
│    → 返回 tunFd                                 │
└───────────────┬─────────────────────────────────┘
                │
                ↓
┌─────────────────────────────────────────────────┐
│  内核层（HarmonyOS Kernel）                     │
│  • 创建虚拟网卡（tun0）                         │
│  • 配置 IP 地址（192.168.100.2/24）            │
│  • 设置路由规则（0.0.0.0/0 → vpn-tun）         │
│  • 返回 TUN 设备文件描述符                       │
└─────────────────────────────────────────────────┘
```

---

## 🔧 详细步骤

### **步骤 1：创建 VPN 扩展能力**

**位置：** `VPNExtentionAbility.ets` - `onCreate()`

```typescript
onCreate(want: Want) {
  // 1. 检查 Context 有效性
  if (!this.context) {
    hilog.error(0x0000, 'ZBQ', '❌ Context is invalid');
    return;
  }
  
  // 2. 创建 VpnConnection 对象
  this.vpnConnection = vpnExtension.createVpnConnection(this.context);
  
  // 3. 开始初始化流程
  this.CreateTunnel();  // → 步骤 2
}
```

**关键点：**
- ✅ 必须在 `VpnExtensionAbility` 上下文中创建
- ✅ 不能在普通的 `UIAbility` 中创建（权限不足）
- ✅ `this.context` 是 `VpnExtensionContext` 类型

---

### **步骤 2：创建 UDP 隧道到服务器**

**位置：** `VPNExtentionAbility.ets` - `CreateTunnel()`

```typescript
CreateTunnel() {
  // 1. 创建 UDP socket 并连接到 VPN 服务器
  let tunnelFd = vpn_client.udpConnect(this.vpnServerIp, 8888);
  
  if (tunnelFd < 0) {
    hilog.error(0x0000, 'ZBQ', '❌ Failed to create UDP tunnel');
    return;
  }
  
  // 2. 保存隧道 FD
  setTunnelFd(tunnelFd);
  
  // 3. 继续保护隧道
  this.Protect();  // → 步骤 3
}
```

**关键点：**
- ✅ `vpnServerIp`：VPN 服务器的 IP 地址（如 `127.0.0.1` 或 `10.20.23.147`）
- ✅ `8888`：VPN 服务器的端口号
- ✅ `tunnelFd`：UDP socket 的文件描述符

---

### **步骤 3：保护 UDP 隧道（防止路由循环）**

**位置：** `VPNExtentionAbility.ets` - `Protect()`

```typescript
Protect() {
  if (gTunnelFd < 0) {
    hilog.error(0x0000, 'ZBQ', '❌ Invalid tunnelFd');
    return;
  }
  
  if (this.vpnConnection) {
    // 关键：保护 UDP 隧道，让它直接走物理网络
    this.vpnConnection.protect(gTunnelFd).then(() => {
      hilog.info(0x0000, 'ZBQ', '✅ UDP tunnel protected');
      
      // 继续配置 VPN
      this.SetupVpn();  // → 步骤 4
    });
  }
}
```

**关键点：**
- ✅ `protect(tunnelFd)`：告诉系统这个 socket 不走 VPN
- ✅ 防止路由循环：VPN 流量 → UDP 隧道 → VPN 流量 → ...
- ✅ 必须在创建 TUN 之前调用

---

### **步骤 4：配置 VPN 参数**

**位置：** `VPNExtentionAbility.ets` - `SetupVpn()`

```typescript
SetupVpn() {
  // 1. 定义 VPN 配置
  class Config {
    public addresses: AddressWithPrefix[];      // TUN 设备的 IP 地址
    public mtu: number;                         // 最大传输单元
    public dnsAddresses: string[];              // DNS 服务器
    public trustedApplications: string[];       // 绕过 VPN 的应用
    public blockedApplications: string[];       // 禁止使用 VPN 的应用
    public routes: vpnExtension.RouteInfo[];    // 路由规则
    
    constructor(tunIp: string, blockedAppName: string) {
      // 2. 配置 TUN 设备 IP 地址
      this.addresses = [
        new AddressWithPrefix(new Address(tunIp, 1), 24),   // IPv4: 192.168.100.2/24
        new AddressWithPrefix(new Address('::1', 2), 128)   // IPv6: ::1/128
      ];
      
      // 3. 配置 MTU
      this.mtu = 1400;
      
      // 4. 配置 DNS
      this.dnsAddresses = ['8.8.8.8'];  // 使用 Google DNS
      
      // 5. 配置信任的应用（空 = 所有应用都走 VPN）
      this.trustedApplications = [];
      
      // 6. 配置阻止的应用（VPN 服务器自身不能走 VPN）
      this.blockedApplications = [];
      
      // 7. 配置路由规则（全流量 VPN）
      this.routes = [
        // IPv4 默认路由：0.0.0.0/0 → vpn-tun
        {
          interface: 'vpn-tun',
          destination: {
            address: new Address('0.0.0.0', 1),
            prefixLength: 0  // /0 = 匹配所有地址
          },
          gateway: {
            address: '192.168.100.1',  // VPN 网关
            family: 1,
            port: 0
          },
          hasGateway: true,
          isDefaultRoute: true
        },
        // IPv6 默认路由：::/0 → vpn-tun
        {
          interface: 'vpn-tun',
          destination: {
            address: new Address('::', 2),
            prefixLength: 0
          },
          gateway: {
            address: '::1',
            family: 2,
            port: 0
          },
          hasGateway: true,
          isDefaultRoute: true
        }
      ];
    }
  }
  
  // 8. 创建配置对象
  let config = new Config(this.tunIp, this.blockedAppName);
  
  // 9. 创建 TUN 设备（系统自动）
  this.vpnConnection.create(config)
    .then((tunFd: number) => {
      // 10. TUN 设备创建成功！
      hilog.info(0x0000, 'ZBQ', '✅ TUN device created, FD: %{public}d', tunFd);
      
      // 11. 保存 TUN FD
      setTunFd(tunFd);
      
      // 12. 启动数据转发
      vpn_client.startVpn(tunFd, gTunnelFd);
    })
    .catch((err: BusinessError) => {
      hilog.error(0x0000, 'ZBQ', '❌ TUN creation failed: %{public}s', JSON.stringify(err));
    });
}
```

**关键点：**
- ✅ `vpnConnection.create(config)`：系统 API，自动创建 TUN 设备
- ✅ 返回的 `tunFd`：TUN 设备的文件描述符
- ✅ 不需要手动调用 `open("/dev/net/tun")`
- ✅ 不需要手动调用 `ioctl(TUNSETIFF)`
- ✅ 系统会自动配置 IP、路由、DNS

---

## 📊 配置参数详解

### **1. addresses（TUN 设备 IP 地址）**

```typescript
this.addresses = [
  new AddressWithPrefix(
    new Address('192.168.100.2', 1),  // IP 地址, family=1 表示 IPv4
    24                                 // 前缀长度 /24
  ),
  new AddressWithPrefix(
    new Address('::1', 2),            // IPv6 地址, family=2
    128
  )
];
```

**含义：**
- TUN 设备的虚拟 IP：`192.168.100.2/24`
- 子网：`192.168.100.0/24`
- 网关：`192.168.100.1`

---

### **2. mtu（最大传输单元）**

```typescript
this.mtu = 1400;
```

**含义：**
- 每个数据包的最大大小：1400 字节
- 比标准以太网 MTU（1500）小，为加密预留空间

---

### **3. dnsAddresses（DNS 服务器）**

```typescript
this.dnsAddresses = ['8.8.8.8'];
```

**含义：**
- 所有 DNS 查询通过 VPN 发送到 `8.8.8.8`
- 如果为空数组 `[]`，DNS 查询也会走 VPN 隧道

---

### **4. trustedApplications（绕过 VPN 的应用）**

```typescript
this.trustedApplications = [];  // 空数组 = 所有应用都走 VPN
```

**含义：**
- 列表中的应用**不走 VPN**，直接使用物理网络
- 空数组：所有应用都通过 VPN

---

### **5. blockedApplications（禁止使用 VPN 的应用）**

```typescript
this.blockedApplications = [];
```

**含义：**
- 列表中的应用**禁止访问网络**
- 通常用于阻止某些应用联网

---

### **6. routes（路由规则）**

```typescript
this.routes = [
  {
    interface: 'vpn-tun',
    destination: {
      address: new Address('0.0.0.0', 1),
      prefixLength: 0  // 0.0.0.0/0 = 所有 IPv4 地址
    },
    gateway: {
      address: '192.168.100.1',  // VPN 网关
      family: 1,
      port: 0
    },
    hasGateway: true,
    isDefaultRoute: true  // 默认路由
  }
];
```

**含义：**
- 所有流量（`0.0.0.0/0`）都路由到 TUN 设备
- 网关：`192.168.100.1`（VPN 虚拟网关）

---

## 🔍 关键 API 参考

### **`vpnExtension.createVpnConnection(context)`**

**作用：** 创建 VPN 连接对象

**参数：**
- `context`：`VpnExtensionContext`（扩展能力上下文）

**返回值：**
- `VpnConnection` 对象

**示例：**
```typescript
this.vpnConnection = vpnExtension.createVpnConnection(this.context);
```

---

### **`vpnConnection.protect(fd)`**

**作用：** 保护指定的 socket，让它不走 VPN

**参数：**
- `fd`：socket 文件描述符

**返回值：**
- `Promise<void>`

**示例：**
```typescript
await this.vpnConnection.protect(tunnelFd);
```

**重要性：**
- ⚠️ **防止路由循环**
- VPN 客户端的 UDP 隧道必须保护，否则会死锁

---

### **`vpnConnection.create(config)`**

**作用：** 创建 TUN 设备并配置 VPN

**参数：**
- `config`：VPN 配置对象（包含 IP、路由、DNS 等）

**返回值：**
- `Promise<number>`：TUN 设备的文件描述符

**示例：**
```typescript
const tunFd = await this.vpnConnection.create(config);
console.log('TUN FD:', tunFd);
```

**关键：**
- ✅ 系统自动创建 TUN 设备
- ✅ 自动配置 IP 地址和路由
- ✅ 返回 tunFd 用于读写数据

---

### **`vpnConnection.destroy()`**

**作用：** 销毁 VPN 连接，关闭 TUN 设备

**返回值：**
- `Promise<void>`

**示例：**
```typescript
await this.vpnConnection.destroy();
```

---

## 🆚 VPN 客户端 vs VPN 服务端

| 特性 | VPN 客户端（HarmonyOS） | VPN 服务端（Linux/Native） |
|------|------------------------|---------------------------|
| **TUN 创建方式** | 系统 API (`vpnConnection.create`) | 手动 (`open("/dev/net/tun")` + `ioctl`) |
| **权限要求** | `ohos.permission.MANAGE_VPN` | `CAP_NET_ADMIN` 或 root |
| **上下文** | `VpnExtensionAbility` | 任意进程 |
| **配置方式** | 传入 Config 对象 | 手动执行 `ip` 命令 |
| **路由配置** | Config.routes | 手动配置 `iptables` |
| **适用平台** | 鸿蒙手机/平板 | Linux 服务器 |

---

## 🎯 完整代码示例

### **VPN 客户端：创建 TUN 设备**

```typescript
import { vpnExtension } from '@kit.NetworkKit';
import { VpnExtensionAbility } from '@kit.NetworkKit';

export default class MyVpnExtAbility extends VpnExtensionAbility {
  private vpnConnection: vpnExtension.VpnConnection | null = null;
  
  onCreate(want: Want) {
    // 1. 创建 VpnConnection
    this.vpnConnection = vpnExtension.createVpnConnection(this.context);
    
    // 2. 创建 UDP 隧道
    const tunnelFd = createUdpSocket('127.0.0.1', 8888);
    
    // 3. 保护 UDP 隧道
    await this.vpnConnection.protect(tunnelFd);
    
    // 4. 配置 VPN
    const config = {
      addresses: [
        {
          address: { address: '192.168.100.2', family: 1 },
          prefixLength: 24
        }
      ],
      mtu: 1400,
      dnsAddresses: ['8.8.8.8'],
      routes: [
        {
          interface: 'vpn-tun',
          destination: {
            address: { address: '0.0.0.0', family: 1 },
            prefixLength: 0
          },
          gateway: {
            address: '192.168.100.1',
            family: 1,
            port: 0
          },
          hasGateway: true,
          isDefaultRoute: true
        }
      ]
    };
    
    // 5. 创建 TUN 设备
    const tunFd = await this.vpnConnection.create(config);
    console.log('✅ TUN device created, FD:', tunFd);
    
    // 6. 启动数据转发
    startDataForwarding(tunFd, tunnelFd);
  }
}
```

---

## 📚 参考文档

### **官方文档：**
- [VPN 扩展能力开发指南](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-V5/vpn-extension-V5)
- [网络管理 - VPN 连接](https://developer.huawei.com/consumer/cn/doc/harmonyos-references-V5/js-apis-net-vpn-V5)

### **相关代码：**
- `VpnClient/entry/src/main/ets/vpnability/VPNExtentionAbility.ets`
- `VpnClient/entry/src/main/ets/pages/SetupVpn.ets`

---

## 🎯 总结

### ✅ **VPN 客户端创建 TUN 设备的关键要点：**

1. **使用系统 API**
   ```typescript
   vpnConnection.create(config)
   ```

2. **不需要手动操作 /dev/net/tun**
   - ❌ 不需要 `open("/dev/net/tun")`
   - ❌ 不需要 `ioctl(TUNSETIFF)`

3. **必须在 VpnExtensionAbility 中**
   - ✅ 需要 `VpnExtensionContext`
   - ✅ 需要 `ohos.permission.MANAGE_VPN` 权限

4. **系统自动配置**
   - ✅ 自动分配 IP 地址
   - ✅ 自动配置路由规则
   - ✅ 自动处理 DNS

5. **必须保护 UDP 隧道**
   ```typescript
   vpnConnection.protect(tunnelFd)
   ```

---

## 🔧 常见问题

### **Q1：为什么客户端不能在 UIAbility 中创建 TUN？**

**答：** 因为 VPN 操作需要特殊权限，只有 `VpnExtensionAbility` 有 `VpnExtensionContext`，普通的 `UIAbility` 没有这个权限。

---

### **Q2：客户端的 TUN IP（192.168.100.2）和服务器的 TUN IP（10.8.0.1）不同，如何通信？**

**答：** 
- 客户端 TUN IP 是**虚拟网络**的 IP（本地概念）
- 实际通信通过 **UDP 隧道**（物理网络）
- 流程：
  ```
  应用 → TUN(192.168.100.2) → 加密 → UDP(物理IP) → 服务器(10.20.23.147)
  ```

---

### **Q3：为什么要调用 `protect(tunnelFd)`？**

**答：** 防止路由循环
```
不保护的情况：
应用 → TUN → UDP 隧道 → TUN → UDP 隧道 → ... （死循环）

保护后的情况：
应用 → TUN → UDP 隧道（直接走物理网络，绕过 TUN） → 服务器 ✅
```

---

**🎉 现在您已经完全理解了鸿蒙 VPN 客户端如何创建 TUN 设备！**
