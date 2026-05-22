# LFGateway - 高性能分布式 API 网关

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/w/)
[![Linux](https://img.shields.io/badge/Linux-epoll-green)]()

**从零自研**的高性能分布式 API 网关，集成 Reactor 网络库、分布式限流、消息队列、鉴权、路由与可观测性。

---

## 🔥 核心亮点

| 模块 | 技术 | 面试必问 |
|------|------|----------|
| **网络** | epoll + Reactor + 非阻塞I/O | LT/ET 模式？EAGAIN 处理？ |
| **分布式限流** | Redis Lua 令牌桶算法 | 为什么不用固定窗口？Lua 如何保证原子性？ |
| **消息队列** | Redis Streams 异步日志 | 消费者组与 Pending 机制？宕机恢复如何保证不丢数据？ |
| **鉴权** | Redis token 缓存 | 为什么用 Redis 而不是 MySQL？ |
| **可观测性** | HEALTH/STATS 管理命令 | 连接池状态如何监控？ |

---

## 📊 压测数据（wrk, 4核8G虚拟机）

```
QPS: 56,312  |  平均延迟: 1.98ms  |  并发: 5000+
```

---

## 🚀 30秒跑起来

```bash
git clone https://github.com/whn121/LFGateway.git && cd LFGateway
mkdir build && cd build && cmake .. && make -j$(nproc)
echo '<h1>Hello!</h1>' > ../www/index.html
./gateway -p 8080 -t 4 -w ../www
# 浏览器打开 http://localhost:8080
```

---

## 🧩 架构一览

```
EventLoop (epoll_wait)
  ├── Acceptor (listenfd → accept4)
  ├── TcpConnection × N (connfd → handleRead/Write)
  │     └── 投递到线程池 → HttpParser → send
  ├── 分布式限流器 (Redis Lua 令牌桶)
  ├── 时间轮 (管理超时连接)
  ├── 异步日志 (Redis Streams → MySQL)
  └── 管理端口 (HEALTH / STATS)
        ↑
     内存池 (所有对象分配)
```

---

## 📁 核心文件

```
include/net/      Reactor 核心 (EventLoop, Channel, TcpConnection)
gateway/          网关业务层
  ├── distributed_rate_limiter.h/cpp  分布式限流器
  ├── ratelimit_plugin.h/cpp          本地限流器（降级用）
  ├── auth_plugin.h/cpp               鉴权插件
  ├── router.h/cpp                    路由管理
  ├── mysql_pool.h/cpp                MySQL 连接池
  ├── redis_client.h/cpp              Redis 客户端（含 Streams）
  ├── log_consumer.h/cpp              日志生产者
  ├── stream_writer.h/cpp             日志消费者
  └── gateway.h/cpp                   网关核心
```

---

## 🎯 面试可聊的点

- **Reactor + epoll**：为什么用 LT？如何处理粘包？
- **分布式限流**：Redis Lua 令牌桶 vs 固定窗口，Fail-Open 降级策略
- **消息队列**：Redis Streams 的消费者组与 ACK 机制，与 Kafka 的取舍
- **数据库**：MySQL 连接池设计，批量写入优化
- **系统稳定性**：GDB 排查 `bad_weak_ptr`、`epoll_ctl` 段错误的经验

---

## 📝 许可证

MIT License

**GitHub**: [whn121/LFGateway](https://github.com/whn121/LFGateway)
```

可以直接把这个内容粘贴到你的 README.md 文件中，然后提交推送。
