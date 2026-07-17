# NexusRPC Redis Registry 设计

## 1. 目标与版本

Registry 在 v1.1 实现；v1.0 使用静态 Endpoint 配置。Redis v1 只支持单机实例和密码，不支持 TLS、Sentinel、Cluster。Redis 同步客户端不得运行在 IO 线程，必须放入后台线程或任务队列。

## 2. 数据模型

| Key | 类型 | TTL | 内容 |
|---|---|---:|---|
| `nexus:svc:{service}:{instance}` | String | 15s | JSON ServiceMeta |
| `nexus:svc:{service}:instances` | Set | 无 | instance ID 索引 |
| `nexus:svc:changes` | Pub/Sub | - | ServiceEvent JSON |

ServiceMeta 至少包含 `service_id`、`service_name`、`version`、`host`、`port`、`tools`。所有 Redis key 的 service/instance 部分必须经过字符校验，禁止未转义分隔符。

## 3. 注册生命周期

Backend 启动后执行原子注册：写实例键、设置 TTL、加入 Set、发布 upsert 事件。每 5 秒续约一次，TTL 为 15 秒。优雅停止执行原子注销：删除实例键、移出 Set、发布 delete 事件。

注册、续约、注销使用 Lua 脚本保证实例键与索引集合更新原子性。脚本失败必须记录 trace 信息并返回 RegistryUnavailable。

## 4. 发现和缓存

Gateway 启动时全量拉取所有服务及实例，构建内存缓存和 Tool Registry。收到 Pub/Sub 事件后执行增量更新。每次 discover 校验实例键，删除 Set 中已过期的 ID；后台清理任务周期性执行同样操作。

Pub/Sub 断线重连后必须先全量同步，再恢复增量消费。Pub/Sub 只是加速通道，不是事实来源。

## 5. 失联降级

Registry 失联时保留最后一次成功缓存。缓存最多支持新调用 5 分钟；超过 5 分钟拒绝新调用并返回 `UNAVAILABLE`。已进行中的 RPC 不主动终止。Redis 恢复后立即全量同步并更新 cache timestamp。Backend 失联期间继续接受已有直连请求，但不能声称注册成功或续约成功。

## 6. 本地接口

```cpp
class ServiceRegistry {
public:
    Status registerInstance(const ServiceMeta& meta);
    Status unregisterInstance(std::string_view service_id);
    Result<std::vector<ServiceInstance>> discover(std::string_view service_name);
    Status refreshHeartbeat(std::string_view service_id);
    Status fullSync();
    Status watch(ServiceEventCallback callback);
};
```

接口实现必须区分 Redis 错误、数据反序列化错误、实例不存在和缓存降级状态。

## 7. 变更事件

事件包含 `type`（upsert/delete）、`service_name`、`instance_id`、`version` 和时间戳。事件处理必须幂等；收到旧版本或重复事件不能破坏当前缓存。无法安全应用增量事件时触发全量同步。

## 8. 验收

- 两个实例同时注册不会互相覆盖；
- 主动注销在 1 秒内更新 Gateway Tool Registry；
- Redis 短暂断线后可重连并全量恢复；
- TTL 到期实例最终不再被 discover 返回；
- 缓存超过 5 分钟后新调用返回 `UNAVAILABLE`；
- 注册和注销脚本并发测试通过。
