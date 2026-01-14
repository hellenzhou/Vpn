// 全局VPN状态变量
export let gTunFd: number = -1;
export let gTunnelFd: number = -1;

// 设置全局变量
export function setTunnelFd(fd: number) {
  gTunnelFd = fd;
}

export function setTunFd(fd: number) {
  gTunFd = fd;
}

// 获取全局变量
export function getTunnelFd(): number {
  return gTunnelFd;
}

export function getTunFd(): number {
  return gTunFd;
}
