# HarmonyOS VPN 配置变量详解

## 📚 三个核心配置变量

### 1️⃣ `trustedApplications: string[]`
**受信任的应用程序列表 - 这些应用会绕过VPN隧道**

#### 语义
```typescript
public trustedApplications: string[];   // 受信任的应用程序（可通过 VPN 使用）
```

**⚠️ 注意**: 这个注释容易引起误解! 真实含义应该是:
```typescript
public trustedApplications: string[];   // 受信任的应用程序（绕过VPN，直连真实网络）
```

#### 行为说明
- **列表中的应用**: 绕过VPN隧道,直接访问真实网络(不经过TUN设备)
- **不在列表中的应用**: 所有流量被TUN设备捕获,必须走VPN隧道

#### 配置示例

| 配置 | 行为 |
|------|------|
| `trustedApplications = []` | 所有应用流量都走VPN隧道 |
| `trustedApplications = ['com.hellen.vpnserver']` | VPN服务器直连网络,其他应用走VPN |
| `trustedApplications = ['com.example.browser']` | 浏览器直连网络(不走VPN),其他应用走VPN |

#### 典型使用场景
```typescript
// ✅ 正确配置 - VPN服务器需要直连真实网络来转发流量
this.trustedApplications = ['com.hellen.vpnserver'];

// ❌ 错误配置 - 会导致VPN服务器自己的流量也进入VPN隧道(路由循环)
this.trustedApplications = [];
```

---

### 2️⃣ `blockedApplications: string[]`
**阻止的应用程序列表 - 这些应用无法使用网络**

#### 语义
```typescript
public blockedApplications: string[];   // 不允许通过 VPN 使用的应用程序
```

#### 行为说明
- **列表中的应用**: 完全禁止访问网络(既不能走VPN,也不能直连)
- **不在列表中的应用**: 正常处理(根据`trustedApplications`决定是否走VPN)

#### 配置示例

| 配置 | 行为 |
|------|------|
| `blockedApplications = []` | 不阻止任何应用 |
| `blockedApplications = ['com.example.game']` | 游戏应用无法联网 |

#### 典型使用场景
```typescript
// ✅ 常见配置 - 不阻止任何应用
this.blockedApplications = [];

// ✅ 家长控制 - 阻止某些应用联网
this.blockedApplications = ['com.example.game', 'com.example.social'];
```

---

### 3️⃣ `routes: RouteConfig[]`
**路由配置 - 定义哪些IP地址范围的流量会被VPN捕获**

#### 语义
```typescript
public routes: RouteConfig[];  // 路由配置 - 控制流量捕获范围
```

#### 行为说明
- 定义哪些目标IP地址的流量会被**TUN设备捕获**并路由到VPN隧道
- 不在路由表中的IP地址会走**物理网络接口**(直连)

#### RouteConfig结构
```typescript
interface RouteConfig {
  interface: string;              // VPN接口名称,如 'vpn-tun'
  destination: {
    address: Address;             // 目标网络地址
    prefixLength: number;         // 子网掩码长度
  };
  gateway: {
    address: string;              // 网关地址(VPN服务器的TUN IP)
    family: number;               // 1=IPv4, 2=IPv6
    port: number;                 // 端口(通常为0)
  };
  hasGateway: boolean;            // 是否有网关
  isDefaultRoute: boolean;        // 是否是默认路由
}
```

#### 配置示例

##### 示例1: 全流量VPN(捕获所有流量)
```typescript
this.routes = [
  // IPv4 默认路由 - 捕获所有 IPv4 流量
  {
    interface: 'vpn-tun',
    destination: {
      address: new Address('0.0.0.0', 1),  // 0.0.0.0/0 = 所有IPv4流量
      prefixLength: 0                      // /0 = 匹配所有地址
    },
    gateway: {
      address: '192.168.100.1',            // VPN网关(服务器的TUN IP)
      family: 1,                           // IPv4
      port: 0
    },
    hasGateway: true,
    isDefaultRoute: true                   // 这是默认路由
  },
  // IPv6 默认路由 - 捕获所有 IPv6 流量
  {
    interface: 'vpn-tun',
    destination: {
      address: new Address('::', 2),       // ::/0 = 所有IPv6流量
      prefixLength: 0
    },
    gateway: {
      address: '::1',                      // VPN网关(IPv6)
      family: 2,                           // IPv6
      port: 0
    },
    hasGateway: true,
    isDefaultRoute: true
  }
];
```

##### 示例2: 分流VPN(仅捕获特定IP段)
```typescript
this.routes = [
  // 只捕获访问 10.0.0.0/8 的流量
  {
    interface: 'vpn-tun',
    destination: {
      address: new Address('10.0.0.0', 1),
      prefixLength: 8                      // /8 = 10.0.0.0 ~ 10.255.255.255
    },
    gateway: {
      address: '192.168.100.1',
      family: 1,
      port: 0
    },
    hasGateway: true,
    isDefaultRoute: false
  },
  // 国内IP直连,国外IP走VPN的场景也可以通过添加多条路由实现
];
```

##### 示例3: 排除某些IP段
```typescript
// ⚠️ 注意: HarmonyOS VPN可能不直接支持排除路由
// 如果需要排除127.0.0.0/8(本地回环),需要不添加该路由
// 或者使用 vpnConnection.protect() 保护特定socket
```

---

## 🎯 典型配置组合

### 配置A: 全流量VPN (所有应用都必须走VPN)
```typescript
this.trustedApplications = [];           // 没有应用绕过VPN
this.blockedApplications = [];           // 不阻止任何应用
this.routes = [
  { destination: '0.0.0.0/0', ... },     // 捕获所有IPv4
  { destination: '::/0', ... }           // 捕获所有IPv6
];
```
**行为**: 所有应用的所有流量都被捕获到VPN隧道

**⚠️ 问题**: 如果VPN服务器也在本设备上运行,会产生**路由循环**!
- 浏览器发送请求 → TUN设备捕获 → VPN客户端转发给VPN服务器
- VPN服务器发送到真实网络 → TUN设备又捕获(因为服务器不在trustedApplications中)
- 无限循环...

---

### 配置B: 全流量VPN + VPN服务器排除 (推荐)
```typescript
this.trustedApplications = ['com.hellen.vpnserver'];  // VPN服务器绕过VPN
this.blockedApplications = [];                        // 不阻止任何应用
this.routes = [
  { destination: '0.0.0.0/0', ... },                  // 捕获所有IPv4
  { destination: '::/0', ... }                        // 捕获所有IPv6
];
```
**行为**: 
- 浏览器等应用 → 走VPN隧道
- VPN服务器 → 直连真实网络(不经过TUN)

**✅ 优点**: 避免路由循环,VPN服务器可以正常转发流量

---

### 配置C: 分流VPN (仅特定IP走VPN)
```typescript
this.trustedApplications = [];                        // 所有应用都受路由表控制
this.blockedApplications = [];                        // 不阻止任何应用
this.routes = [
  { destination: '10.0.0.0/8', ... },                 // 仅捕获访问10.x.x.x的流量
  { destination: '192.168.0.0/16', ... }              // 仅捕获访问192.168.x.x的流量
];
```
**行为**:
- 访问10.x.x.x或192.168.x.x → 走VPN隧道
- 访问其他IP(如8.8.8.8) → 直连真实网络

---

## 🔥 关键理解

### trustedApplications vs routes 的关系

| 场景 | trustedApplications | routes | 结果 |
|------|---------------------|--------|------|
| 应用A访问百度 | A不在列表中 | 0.0.0.0/0在路由表中 | 流量被TUN捕获 → 走VPN |
| 应用B访问百度 | B在列表中 | 0.0.0.0/0在路由表中 | 流量**不被TUN捕获** → 直连网络 |
| 应用C访问百度 | C不在列表中 | 百度IP不在路由表中 | 流量不被TUN捕获 → 直连网络 |

**优先级**: `trustedApplications` > `routes`
- 如果应用在`trustedApplications`中,**无论路由表如何配置**,都不走VPN
- 如果应用不在`trustedApplications`中,**根据路由表**决定是否走VPN

---

## 🛡️ vpnConnection.protect() 的作用

```typescript
// 保护UDP隧道socket,使其不被TUN设备捕获
this.vpnConnection.protect(tunnelFd);
```

**作用**: 即使应用(如VPN客户端自己)不在`trustedApplications`中,被`protect()`的socket也会绕过VPN。

**使用场景**:
- VPN客户端与VPN服务器之间的UDP隧道
- 如果不protect,隧道流量会被TUN捕获,形成无限循环

**关系**:
```
应用是否走VPN的判断顺序:
1. 如果应用在 trustedApplications 中 → 不走VPN
2. 如果socket被 protect() → 不走VPN  
3. 如果目标IP在 routes 中 → 走VPN
4. 否则 → 直连网络
```

---

## 📊 总结表

| 配置项 | 作用域 | 作用 | 空数组含义 |
|--------|--------|------|-----------|
| `trustedApplications` | **应用级别** | 指定哪些应用绕过VPN | 所有应用都走VPN |
| `blockedApplications` | **应用级别** | 指定哪些应用禁止联网 | 不阻止任何应用 |
| `routes` | **IP级别** | 指定哪些目标IP走VPN | 不捕获任何流量 |
| `vpnConnection.protect()` | **Socket级别** | 保护特定socket不走VPN | N/A |

---

## ✅ 推荐配置 (针对您的场景)

**场景**: VPN客户端和VPN服务器都在同一台设备上运行

```typescript
// VPN服务器需要直连真实网络,不能走VPN隧道
this.trustedApplications = ['com.hellen.vpnserver'];

// 不阻止任何应用
this.blockedApplications = [];

// 捕获所有流量(除了trustedApplications中的应用)
this.routes = [
  { destination: '0.0.0.0/0', gateway: '192.168.100.1', ... },  // IPv4
  { destination: '::/0', gateway: '::1', ... }                  // IPv6
];

// 额外保护:保护UDP隧道socket
this.vpnConnection.protect(tunnelFd);
```

**预期行为**:
1. ✅ 浏览器访问网站 → TUN捕获 → VPN客户端 → UDP隧道(protect) → VPN服务器(trusted) → 真实网络
2. ✅ VPN服务器的流量 → 不被TUN捕获(因为在trustedApplications中) → 直连真实网络
3. ✅ UDP隧道 → 不被TUN捕获(因为被protect) → 直连VPN服务器
4. ✅ 无路由循环

**如果不启动VPN服务器**:
- ❌ 浏览器访问网站 → TUN捕获 → VPN客户端 → UDP隧道连接失败(因为服务器没运行)
- ❌ 浏览器无法访问网站(符合预期)

---

## 🐛 常见错误配置

### 错误1: trustedApplications为空
```typescript
this.trustedApplications = [];  // ❌ 导致路由循环!
```
**问题**: VPN服务器的流量也会被TUN捕获,形成无限循环。

### 错误2: routes为空
```typescript
this.routes = [];  // ❌ 不捕获任何流量!
```
**问题**: 所有流量都走物理网络,VPN形同虚设。

### 错误3: 依赖protect()而不设置trustedApplications
```typescript
this.trustedApplications = [];
this.vpnConnection.protect(tunnelFd);  // 只保护了隧道socket
```
**问题**: VPN服务器的其他socket(如DNS查询)仍会被TUN捕获,可能导致问题。

---

## 🔍 调试方法

### 1. 检查应用是否走VPN
```typescript
hilog.info(0x0000, 'ZBQ', '✅ 信任应用(绕过VPN): %{public}s', 
  JSON.stringify(config.trustedApplications));
```

### 2. 检查路由配置
```typescript
hilog.info(0x0000, 'ZBQ', '✅ 路由数量: %{public}d', config.routes.length);
config.routes.forEach((route, index) => {
  hilog.info(0x0000, 'ZBQ', '  路由%{public}d: %{public}s/%{public}d → %{public}s',
    index,
    route.destination.address.address,
    route.destination.prefixLength,
    route.gateway.address
  );
});
```

### 3. 检查流量是否被捕获
```bash
# 在VPN客户端的packet处理函数中
LOG("📥 TUN设备收到数据包: size=%d, src=%s, dst=%s", 
  size, srcIP, dstIP);
```

如果看到大量`📥 TUN设备收到数据包`日志,说明流量被成功捕获。
如果完全没有日志,说明流量没有被TUN捕获(可能是trustedApplications或routes配置问题)。
