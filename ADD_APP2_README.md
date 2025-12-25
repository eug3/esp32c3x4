添加 app2 的步骤说明（16MB flash）

前提说明
- 设备 flash 大小：16MB（0x1000000）
- 已存在 `app0` 和 `app1`，其镜像大小约为 0x640000（根据 workspace 中的 `app0.bin`/`app1.bin`）
- 本方案不修改现有 `app0.bin`/`app1.bin` 文件；我们只更新分区表并将 `app2` 写入剩余空间

分区布局（见 `partitions_with_app2.csv`）
- `app0` @ 0x10000, size 0x640000
- `app1` @ 0x650000, size 0x640000
- `app2` @ 0xC90000, size 0x370000 (剩余空间)
- `nvs`, `otadata` 保持默认（CSV 中留空 offset）

刷写 app2（只写 app2，不改其他分区）
- 假设你已经有 `build/app2.bin`：

```powershell
# 仅写入 app2 到对应偏移（替换 COMx）
python -m esptool --chip esp32c3 --port COMx write_flash 0xC90000 build/app2.bin
```

完整刷写（包含 bootloader/partition table/app0/app1/app2）示例
```powershell
python -m esptool --chip esp32c3 --port COMx write_flash -z \
  0x1000 build/bootloader.bin \
  0x8000 build/partition-table.bin \
  0x10000 build/app0.bin \
  0x650000 build/app1.bin \
  0xC90000 build/app2.bin
```

如何让设备下次启动 `app2`
- 推荐方法（运行在当前正在执行的应用中）：调用 ESP-IDF API `esp_ota_set_boot_partition()` 指定 `app2` 分区，然后 `esp_restart()`：

示例代码片段：

```c
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

void boot_to_app2()
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_2, "app2");
    if (p) {
        esp_err_t err = esp_ota_set_boot_partition(p);
        if (err == ESP_OK) {
            esp_restart();
        }
    }
}
```

- 也可在你的 boot selector 应用中读取 GPIO/NVS/串口决定要切换到 `app2`，然后调用上面函数。

注意与风险
- 请确保 `app2` 大小不超过 `0x370000`（约 3.5MB）。若 app2 更大，则需重新规划分区或精简应用。
- 在写入分区表或 partition-table.bin 时要小心（partition-table.bin 通常由 IDF 构建工具生成）。建议先只写入 `app2` 镜像到偏移 `0xC90000`，并通过运行时 API 切换启动分区进行验证。
- 写分区表需要写入 partition-table.bin 到 0x8000；若你不想覆盖已有表，请不要写该文件。

我可以为你继续做的事
- 生成 `build/app2.bin` 的示例工程（最小可引导空应用）并编译。
- 生成一个小工具程序（在当前运行的 app 中）来把下次 boot 指向 `app2` 并重启。
- 如果需要，我也可以生成用于写入 `partition-table.bin` 的完整刷写脚本（但需要你确认要覆盖现有分区表）。
