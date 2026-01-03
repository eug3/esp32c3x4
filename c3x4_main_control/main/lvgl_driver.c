/**
 * @file lvgl_driver.c
 * @brief LVGL驱动适配层 - EPD和按键输入 (LVGL 9.x)
 */

#include "lvgl_driver.h"
#include "EPD_4in26.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

static const char *TAG = "LVGL_DRV";

// LVGL 缓冲区对齐（通常是 4 字节）
#ifndef LV_DRAW_BUF_ALIGN_BYTES
#define LV_DRAW_BUF_ALIGN_BYTES(x) (((x) + 3) & ~3)
#endif

// EPD显示尺寸
// 物理屏幕为 800x480，但旧版 welcome 使用 ROTATE_270 形成竖屏逻辑坐标 480x800。
// 为了沿用旧版竖向布局，这里让 LVGL 也工作在 480x800 的逻辑分辨率。
#define EPD_WIDTH 800
#define EPD_HEIGHT 480
#define DISP_HOR_RES 480
#define DISP_VER_RES 800
#define DISP_BUF_LINES 20  // 1bpp: 480×20÷8 = 12 KB
#define MAX_PARTIAL_REFRESHES 10

// 1bpp framebuffer for EPD (物理坐标系 800x480)
static uint8_t s_epd_framebuffer[(EPD_WIDTH * EPD_HEIGHT) / 8];  // 48 KB

// LVGL 1bpp 工作缓冲区（逻辑坐标系 480x800）
// 使用 PARTIAL 模式的小缓冲区，避免 DIRECT 模式的兼容性问题
// 内存占用: (480×20÷8) + 8 = 12008 字节
// +8 字节：LVGL I1 格式需要 8 字节调色板头部
static uint8_t s_lvgl_draw_buffer[(DISP_HOR_RES * DISP_BUF_LINES) / 8 + 8];

// Track the union of flushed areas since last EPD refresh.
static lv_area_t s_dirty_area;
static bool s_dirty_valid = false;

// 当前刷新模式(可动态切换)
// 重要: 首次刷新应用FULL模式,确保framebuffer与EPD完全同步!
// 之后可切换为FAST或PARTIAL提升速度
static epd_refresh_mode_t s_refresh_mode = EPD_REFRESH_FULL;

// 局部刷新计数器: 多次局部刷新后强制全刷以消除鬼影
static uint32_t s_partial_refresh_count = 0;
#define FORCE_FULL_REFRESH_AFTER_N_PARTIAL 10

// EPD refresh state tracking (异步刷新)
static bool s_epd_refreshing = false;
static bool s_render_done = false; // disp_flush_cb 已完成写入
static SemaphoreHandle_t s_epd_mutex = NULL;
static SemaphoreHandle_t s_render_done_sem = NULL; // 渲染完成信号
static TaskHandle_t s_epd_refresh_task_handle = NULL;
static QueueHandle_t s_refresh_queue = NULL;

// Protect dirty area state against concurrent access (flush_cb vs refresh
// task).
static portMUX_TYPE s_dirty_mux = portMUX_INITIALIZER_UNLOCKED;

// 全局显示设备指针（用于手动刷新模式）
static lv_display_t *g_lv_display = NULL;

// 刷新请求结构
typedef struct {
  epd_refresh_mode_t mode;   // 刷新模式
} refresh_request_t;

// 手动触发 LVGL 渲染刷新（用于 EPD 手动刷新模式）
void lvgl_trigger_render(lv_display_t *disp) {
  // 如果传入 NULL，使用全局 display
  if (disp == NULL) {
    disp = g_lv_display;
  }

  if (disp != NULL) {
    // Avoid rendering while EPD refresh task is reading/sending framebuffer.
    // This prevents mixed old/new bytes being sent to the panel.
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(8000);
    while (s_epd_refreshing) {
      if ((xTaskGetTickCount() - start) > timeout) {
        ESP_LOGW(TAG, "lvgl_trigger_render: timed out waiting for EPD refresh");
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    // 调用一次 lv_timer_handler 处理动画和定时器
    lv_timer_handler();
    // 立即触发渲染
    lv_refr_now(disp);
  } else {
    ESP_LOGW(TAG, "lvgl_trigger_render: display is NULL!");
  }
}

// 优化：不再使用单独的局部刷新缓冲区
// 改为流式发送，直接从 s_epd_framebuffer 按行发送数据
// 节省内存：约 12 KB

static void dirty_area_add(const lv_area_t *area) {
  // NOTE: Never call ESP_LOG inside a critical section; logging may acquire
  // locks.
  bool do_expand_log = false;
  int32_t y_gap = 0;
  lv_area_t init_area = {0};
  lv_area_t old_area = {0};
  lv_area_t new_area = {0};

  portENTER_CRITICAL(&s_dirty_mux);

  if (!s_dirty_valid) {
    s_dirty_area = *area;
    s_dirty_valid = true;
    init_area = *area;
    portEXIT_CRITICAL(&s_dirty_mux);

    ESP_LOGI(TAG, "[DIRTY] init: (%d,%d)-(%d,%d)", (int)init_area.x1,
             (int)init_area.y1, (int)init_area.x2, (int)init_area.y2);
    return;
  }

  old_area = s_dirty_area;

  // 计算新旧区域之间的距离（基于 old_area）
  y_gap = (area->y1 > old_area.y2)
              ? (area->y1 - old_area.y2 - 1)
              : ((old_area.y1 > area->y2) ? (old_area.y1 - area->y2 - 1) : 0);

  // 总是扩展脏区到包含所有已刷新区域的包围盒
  if (area->x1 < s_dirty_area.x1)
    s_dirty_area.x1 = area->x1;
  if (area->y1 < s_dirty_area.y1)
    s_dirty_area.y1 = area->y1;
  if (area->x2 > s_dirty_area.x2)
    s_dirty_area.x2 = area->x2;
  if (area->y2 > s_dirty_area.y2)
    s_dirty_area.y2 = area->y2;

  new_area = s_dirty_area;
  if (old_area.x1 != new_area.x1 || old_area.y1 != new_area.y1 ||
      old_area.x2 != new_area.x2 || old_area.y2 != new_area.y2) {
    do_expand_log = true;
  }

  portEXIT_CRITICAL(&s_dirty_mux);

  if (do_expand_log) {
    ESP_LOGI(TAG,
             "[DIRTY] expanded: (%d,%d)-(%d,%d) -> (%d,%d)-(%d,%d), y_gap=%d",
             (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
             (int)new_area.x1, (int)new_area.y1, (int)new_area.x2,
             (int)new_area.y2, (int)y_gap);
  }
}

static void queue_refresh_request(epd_refresh_mode_t mode) {
  if (s_refresh_queue == NULL) {
    ESP_LOGW(TAG, "Refresh queue not initialized");
    return;
  }

  refresh_request_t req = {
      .mode = mode,
  };

  // 检查队列中是否已有未处理的请求
  // 如果有 FULL 请求在排队，新的 FULL 请求不需要重复入队
  // 如果有 FAST/PARTIAL 请求在排队，新的 FULL 请求应该覆盖
  refresh_request_t queued;
  BaseType_t has_queued = xQueuePeek(s_refresh_queue, &queued, 0);

  if (has_queued == pdTRUE) {
    // 如果队列中已有 FULL 请求，且新请求不是 FULL，直接忽略
    if (queued.mode == EPD_REFRESH_FULL && mode != EPD_REFRESH_FULL) {
      ESP_LOGI(TAG, "queue_refresh_request: skip %d, FULL already queued", mode);
      return;
    }
    // 如果新请求是 FULL，但已有 PARTIAL/FAST 在排队，直接发送 FULL
    // 不要用覆盖，而是等待当前请求处理完后由调用方发送新的 FULL
    // 这里我们选择入队让任务处理
  }

  // 如果队列已满（实际上单槽队列不会满，会覆盖）
  // 使用 xQueueSend 替代 xQueueOverwrite，确保不丢失
  // 但单槽队列只能存一个，所以使用 xQueueOverwrite
  (void)xQueueOverwrite(s_refresh_queue, &req);
  ESP_LOGI(TAG, "queue_refresh_request: mode=%d queued", mode);
}

// LVGL 9.x 显示flush回调 - DIRECT 模式
// LVGL 已经将数据渲染到 s_lvgl_draw_buffer 中
// 这里只需要将 RGB565 格式转换为 1bpp EPD 格式并写入 s_epd_framebuffer
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  // 获取互斥锁，保护 framebuffer 访问
  if (xSemaphoreTake(s_epd_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    ESP_LOGW(TAG, "disp_flush_cb: failed to acquire mutex, skipping write");
    lv_display_flush_ready(disp);
    return;
  }

  // 标记渲染未完成（用于刷新任务判断）
  s_render_done = false;

  // 记录每次 flush 调用
  static uint32_t flush_count = 0;
  flush_count++;

  int32_t x, y;

  // 获取显示颜色格式
  lv_color_format_t cf = lv_display_get_color_format(disp);

  // Bounds check for area
  if (area->x1 < 0 || area->y1 < 0 || area->x2 >= DISP_HOR_RES ||
      area->y2 >= DISP_VER_RES) {
    ESP_LOGW(TAG,
             "disp_flush_cb: area out of bounds - x1=%d, y1=%d, x2=%d, y2=%d "
             "(max=%dx%d)",
             (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
             DISP_HOR_RES, DISP_VER_RES);
  }

  // 首次 flush 时打印信息
  if (flush_count <= 20) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    ESP_LOGI(TAG, "disp_flush_cb #%u: area(%d,%d)-(%d,%d) size=%dx%d, cf=%d",
             flush_count, (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2,
             (int)w, (int)h, (int)cf);
  }

  // PARTIAL 模式 + 1bpp：LVGL 使用 I1 格式渲染到小缓冲区
  // flush_cb 需要应用 ROTATE_270 坐标映射并写入 s_epd_framebuffer
  // 检查颜色格式
  if (cf != LV_COLOR_FORMAT_I1) {
    ESP_LOGE(TAG, "Unexpected color format: %d (expected I1)", (int)cf);
    xSemaphoreGive(s_epd_mutex);
    lv_display_flush_ready(disp);
    return;
  }

  uint32_t pixel_count = 0;

  // LVGL 9.x I1 格式：前 8 字节为调色板，必须跳过
  // stride 对齐到 4 字节边界
  px_map += 8;  // 跳过调色板头部

  const int32_t buf_w = lv_area_get_width(area);
  const int32_t buf_h = lv_area_get_height(area);

  // LVGL I1 格式的 stride：每行字节数，对齐到 4 字节
  const uint32_t stride = LV_DRAW_BUF_ALIGN_BYTES(((buf_w + 7) / 8));
  
  if (flush_count <= 5) {
    ESP_LOGI(TAG, "flush_cb: area_w=%d, stride=%u bytes (px_map already +8 for palette)", 
             (int)buf_w, (unsigned)stride);
  }
  
  for (y = area->y1; y <= area->y2; y++) {
    for (x = area->x1; x <= area->x2; x++) {
      // PARTIAL 模式：使用相对坐标读取 LVGL 缓冲区
      const int32_t buf_x = x - area->x1;
      const int32_t buf_y = y - area->y1;
      const uint32_t src_byte_idx = buf_y * stride + (buf_x / 8);
      const uint8_t src_bit_idx = 7 - (buf_x % 8);
      
      if (src_byte_idx >= sizeof(s_lvgl_draw_buffer)) {
        if (flush_count <= 2) {
          ESP_LOGW(TAG, "Buffer overflow: idx=%u, size=%u", 
                   src_byte_idx, sizeof(s_lvgl_draw_buffer));
        }
        continue;
      }
      
      const bool pixel = (px_map[src_byte_idx] >> src_bit_idx) & 1;

      // 写入 EPD buffer（物理坐标，应用 ROTATE_270）
      const int32_t memX = y;
      const int32_t memY = EPD_HEIGHT - 1 - x;
      const uint32_t dst_byte_idx = memY * (EPD_WIDTH / 8) + (memX / 8);
      const uint8_t dst_bit_idx = 7 - (memX % 8);

      if (dst_byte_idx < sizeof(s_epd_framebuffer)) {
        if (pixel == 0) {  // 黑色
          s_epd_framebuffer[dst_byte_idx] &= ~(1 << dst_bit_idx);
        } else {  // 白色
          s_epd_framebuffer[dst_byte_idx] |= (1 << dst_bit_idx);
        }
        pixel_count++;
      } else {
        ESP_LOGE(TAG, "FB overflow at idx=%u", (unsigned)dst_byte_idx);
      }
    }
  }

  // Flush处理完成
  if (flush_count <= 20) {
    ESP_LOGI(TAG,
             "disp_flush_cb #%u: area(%d,%d)-(%d,%d), pixels=%u (1bpp fast copy)",
             flush_count, (int)area->x1, (int)area->y1, (int)area->x2,
             (int)area->y2, pixel_count);
  }

  // 脏区跟踪：在 PARTIAL 模式下记录所有刷新的区域，用于优化 EPD 刷新
  if (s_refresh_mode == EPD_REFRESH_PARTIAL) {
    dirty_area_add(area);
  }

  // 标记渲染完成并通知刷新任务
  s_render_done = true;
  xSemaphoreGive(s_render_done_sem);

  // 释放互斥锁
  xSemaphoreGive(s_epd_mutex);

  // 通知LVGL刷新完成（不触发 EPD 硬件刷新）
  lv_display_flush_ready(disp);
}

// 异步 EPD 刷新任务（前置声明）
static void epd_refresh_task(void *arg);

// 初始化LVGL显示驱动
lv_display_t *lvgl_display_init(void) {
  ESP_LOGI(TAG, "Initializing LVGL display driver (LVGL 9.x)");

  // 创建互斥锁（用于保护 framebuffer 访问）
  if (s_epd_mutex == NULL) {
    s_epd_mutex = xSemaphoreCreateMutex();
    if (s_epd_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create EPD mutex!");
      return NULL;
    }
  }

  // 创建信号量（用于 disp_flush_cb 通知渲染完成）
  if (s_render_done_sem == NULL) {
    s_render_done_sem = xSemaphoreCreateBinary();
    if (s_render_done_sem == NULL) {
      ESP_LOGE(TAG, "Failed to create render done semaphore!");
      return NULL;
    }
  }

  // 创建刷新请求队列（异步刷新）
  if (s_refresh_queue == NULL) {
    // Single-slot queue + overwrite ensures the latest refresh request is not
    // dropped.
    s_refresh_queue = xQueueCreate(1, sizeof(refresh_request_t));
    if (s_refresh_queue == NULL) {
      ESP_LOGE(TAG, "Failed to create refresh queue!");
      return NULL;
    }
  }

  // 创建异步刷新任务
  if (s_epd_refresh_task_handle == NULL) {
    BaseType_t ret = xTaskCreate(epd_refresh_task, "epd_refresh", 4096, NULL,
                                 3, // 优先级高于 LVGL 任务 (2)
                                 &s_epd_refresh_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create EPD refresh task!");
      return NULL;
    }
    ESP_LOGI(TAG, "EPD refresh task created (async mode)");
  }

  // 清空 framebuffer 并初始化为白色 (1=白色)
  memset(s_epd_framebuffer, 0xFF, sizeof(s_epd_framebuffer));
  memset(s_lvgl_draw_buffer, 0xFF, sizeof(s_lvgl_draw_buffer));
  
  const uint32_t total_kb = (sizeof(s_epd_framebuffer) + sizeof(s_lvgl_draw_buffer)) / 1024;
  ESP_LOGI(TAG, "Buffers initialized: EPD=%u KB, LVGL=%u KB, Total=%u KB",
           (unsigned)(sizeof(s_epd_framebuffer) / 1024),
           (unsigned)(sizeof(s_lvgl_draw_buffer) / 1024),
           total_kb);

  // 清空脏区域追踪
  s_dirty_valid = false;
  s_partial_refresh_count = 0;
  s_epd_refreshing = false;
  s_render_done = false;

  // 初始化LVGL
  lv_init();

  // 创建显示设备 - LVGL 9.x 新API
  lv_display_t *disp = lv_display_create(DISP_HOR_RES, DISP_VER_RES);
  lv_display_set_flush_cb(disp, disp_flush_cb);

  // 保存 display 指针到全局变量
  g_lv_display = disp;

  // 配置：PARTIAL 模式 + 1bpp 颜色格式
  // - 使用 LV_COLOR_FORMAT_I1 (1bpp黑白)
  // - LVGL 分块渲染到小缓冲区 (12 KB)
  // - flush_cb 进行坐标旋转映射，无需颜色转换
  // - 内存占用: ~60 KB (相比 RGB565 DIRECT 的 ~798 KB)
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
  
  // 使用 PARTIAL 渲染模式（更稳定）
  lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_PARTIAL);

  // 设置小缓冲区（PARTIAL 模式）
  lv_display_set_buffers(disp, s_lvgl_draw_buffer, NULL,
                         sizeof(s_lvgl_draw_buffer),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  ESP_LOGI(TAG,
           "LVGL display initialized: %dx%d, 1bpp, PARTIAL mode, %u KB total RAM",
           DISP_HOR_RES, DISP_VER_RES, total_kb);

  return disp;
}

// 异步 EPD 刷新任务
static void epd_refresh_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "EPD refresh task started");

  while (1) {
    refresh_request_t req;
    // 等待刷新请求，阻塞等待
    if (xQueueReceive(s_refresh_queue, &req, portMAX_DELAY) == pdTRUE) {
      ESP_LOGI(TAG, "EPD refresh task: received request, mode=%d", req.mode);

      // Mark refreshing to indicate EPD is busy
      s_epd_refreshing = true;
      __sync_synchronize();

      // 关键：先清空信号量，防止 disp_flush_cb 在我们开始等待之前就已经给了信号
      // 如果 disp_flush_cb 在我们清空信号量之前就已经给了，它会设置 s_render_done=true
      // 我们通过检查 s_render_done 来判断是否有渲染发生
      xSemaphoreTake(s_render_done_sem, 0);  // 非阻塞清除

      // 使用信号量等待渲染完成（比轮询 s_render_done 更可靠）
      // 如果 s_render_done 已经为 true，说明渲染已完成，直接获取信号量
      // 否则等待信号量，最大等待 200ms
      bool waited_for_render = false;
      if (!s_render_done) {
        waited_for_render = true;
        BaseType_t got_sem = xSemaphoreTake(s_render_done_sem, pdMS_TO_TICKS(200));
        if (got_sem != pdTRUE) {
          ESP_LOGW(TAG,
                   "EPD refresh task: render not ready, sending current data");
        }
      }
      // 无论是否等到信号量，都检查 s_render_done 确保数据已写入
      if (!s_render_done) {
        ESP_LOGW(TAG, "EPD refresh task: s_render_done is false, using current data");
      }
      if (waited_for_render) {
        ESP_LOGI(TAG,
                 "EPD refresh task: render completed, sending updated data");
      }

      // 再获取锁执行刷新
      if (xSemaphoreTake(s_epd_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ESP_LOGI(TAG, "EPD refresh task: refreshing, s_epd_refreshing=%d",
                 s_epd_refreshing);

        epd_refresh_mode_t mode = req.mode;

        // 刷新模式选择逻辑：
        // 1. FULL: 执行全刷（用于屏幕切换，确保显示清晰）
        // 2. FAST: 执行快刷（全屏数据，速度快）
        // 3. PARTIAL: 如果局刷计数 = 0，执行快刷；否则执行局刷，计数器+1
        //    计数器 >= 10 时重置，下次将执行快刷
        if (mode == EPD_REFRESH_FULL) {
          ESP_LOGI(TAG, "EPD refresh task: FULL refresh (requested)");
          EPD_4in26_Display(s_epd_framebuffer);
          s_partial_refresh_count = 0;
          portENTER_CRITICAL(&s_dirty_mux);
          s_dirty_valid = false;
          portEXIT_CRITICAL(&s_dirty_mux);

        } else if (mode == EPD_REFRESH_FAST) {
          ESP_LOGI(TAG, "EPD refresh task: FAST refresh");
          EPD_4in26_Display_Fast(s_epd_framebuffer);
          s_partial_refresh_count = 0;
          portENTER_CRITICAL(&s_dirty_mux);
          s_dirty_valid = false;
          portEXIT_CRITICAL(&s_dirty_mux);

        } else if (mode == EPD_REFRESH_PARTIAL) {
          // 局刷次数为 0 时，直接执行快刷
          if (s_partial_refresh_count == 0) {
            ESP_LOGI(TAG,
                     "EPD refresh task: PARTIAL count=0, using FAST refresh");
            EPD_4in26_Display(s_epd_framebuffer);
            portENTER_CRITICAL(&s_dirty_mux);
            s_dirty_valid = false;
            portEXIT_CRITICAL(&s_dirty_mux);
            s_partial_refresh_count++; // 从 1 开始计数
            goto refresh_done;
          }

          // 原子性地快照脏区，避免与 flush_cb 并发修改
          bool dirty_valid;
          lv_area_t dirty_area;
          portENTER_CRITICAL(&s_dirty_mux);
          dirty_valid = s_dirty_valid;
          dirty_area = s_dirty_area;
          portEXIT_CRITICAL(&s_dirty_mux);

          if (!dirty_valid) {
            ESP_LOGW(TAG, "EPD refresh task: PARTIAL requested but no dirty area, skipping");
            goto partial_done;
          }

          // 局部刷新：直接使用完整的 s_epd_framebuffer，只裁剪脏区发送给硬件
          // 获取脏区尺寸
          const int32_t dirty_w = dirty_area.x2 - dirty_area.x1 + 1;
          const int32_t dirty_h = dirty_area.y2 - dirty_area.y1 + 1;

          // LVGL 坐标系 (480x800) 到 EPD 物理坐标系 (800x480) 的映射
          // ROTATE_270: LVGL(x,y) -> EPD(memX=y, memY=EPD_HEIGHT-1-x)
          int32_t epd_x = dirty_area.y1;
          int32_t epd_y = (int32_t)EPD_HEIGHT - 1 - dirty_area.x2;
          int32_t epd_w = dirty_h; // 注意：宽高互换
          int32_t epd_h = dirty_w;

          // EPD 硬件要求：X 坐标必须是 8 的倍数（字节对齐）
          if (epd_x % 8 != 0) {
            const int32_t orig_x = epd_x;
            epd_x = (epd_x / 8) * 8;   // 向下对齐
            epd_w += (orig_x - epd_x); // 补偿宽度
          }
          if (epd_w % 8 != 0) {
            epd_w = ((epd_w + 7) / 8) * 8; // 向上对齐
          }

          // 边界检查
          if (epd_x < 0)
            epd_x = 0;
          if (epd_y < 0)
            epd_y = 0;
          if (epd_x + epd_w > EPD_WIDTH)
            epd_w = EPD_WIDTH - epd_x;
          if (epd_y + epd_h > EPD_HEIGHT)
            epd_h = EPD_HEIGHT - epd_y;

          ESP_LOGI(TAG,
                   "EPD refresh task: PARTIAL #%u/%u LVGL(%d,%d,%dx%d) -> "
                   "EPD(x=%d,y=%d,%dx%d)",
                   s_partial_refresh_count, FORCE_FULL_REFRESH_AFTER_N_PARTIAL,
                   (int)dirty_area.x1, (int)dirty_area.y1, (int)dirty_w,
                   (int)dirty_h, (int)epd_x, (int)epd_y, (int)epd_w,
                   (int)epd_h);

          // 从完整 framebuffer 裁剪脏区数据发送给硬件
          if (epd_w > 0 && epd_h > 0) {
            EPD_4in26_Display_Partial(s_epd_framebuffer, (UWORD)epd_x,
                                      (UWORD)epd_y, (UWORD)epd_w, (UWORD)epd_h);
          } else {
            ESP_LOGW(TAG, "EPD refresh task: invalid area, fallback FAST");
            EPD_4in26_Display_Fast(s_epd_framebuffer);
          }

          // 计数器递增，达到阈值后重置
          s_partial_refresh_count++;
          if (s_partial_refresh_count >= FORCE_FULL_REFRESH_AFTER_N_PARTIAL) {
            ESP_LOGI(TAG,
                     "EPD refresh task: Reached %u PARTIALs, resetting count",
                     s_partial_refresh_count);
            s_partial_refresh_count = 0;
          }

        partial_done:
          portENTER_CRITICAL(&s_dirty_mux);
          s_dirty_valid = false;
          portEXIT_CRITICAL(&s_dirty_mux);
        } else {
          ESP_LOGE(TAG, "EPD refresh task: Unknown mode %d, fallback to FAST",
                   mode);
          EPD_4in26_Display_Fast(s_epd_framebuffer);
          s_partial_refresh_count = 0;
          s_dirty_valid = false;
        }

      refresh_done:
        s_epd_refreshing = false;
        s_render_done = false; // 重置，为下一次渲染做准备
        ESP_LOGI(TAG, "EPD refresh task: complete, s_epd_refreshing=%d",
                 s_epd_refreshing);
        xSemaphoreGive(s_epd_mutex);

      } else {
        ESP_LOGW(TAG, "Failed to acquire mutex for refresh");
        s_epd_refreshing = false;
        s_render_done = false;
      }
    }
  }
}

// 刷新 EPD - 使用当前配置的模式
void lvgl_display_refresh(void) { queue_refresh_request(s_refresh_mode); }

// 局部刷新 EPD
void lvgl_display_refresh_partial(void) {
  queue_refresh_request(EPD_REFRESH_PARTIAL);
}

// 快速刷新 EPD
void lvgl_display_refresh_fast(void) {
  queue_refresh_request(EPD_REFRESH_FAST);
}

// 全刷 EPD
void lvgl_display_refresh_full(void) {
  queue_refresh_request(EPD_REFRESH_FULL);
}

// 设置刷新模式
void lvgl_set_refresh_mode(epd_refresh_mode_t mode) {
  s_refresh_mode = mode;

  switch (mode) {
  case EPD_REFRESH_PARTIAL:
    ESP_LOGI(TAG, "Refresh mode set to PARTIAL (fastest, may have ghosting)");
    break;
  case EPD_REFRESH_FAST:
    ESP_LOGI(TAG, "Refresh mode set to FAST (balanced)");
    break;
  case EPD_REFRESH_FULL:
    ESP_LOGI(TAG, "Refresh mode set to FULL (clearest)");
    break;
  }
}

// 获取当前刷新模式
epd_refresh_mode_t lvgl_get_refresh_mode(void) { return s_refresh_mode; }

// 检查 EPD 是否正在刷新
bool lvgl_is_refreshing(void) { return s_epd_refreshing; }

// 重置刷新状态
void lvgl_reset_refresh_state(void) {
  portENTER_CRITICAL(&s_dirty_mux);
  s_dirty_valid = false;
  portEXIT_CRITICAL(&s_dirty_mux);
  s_partial_refresh_count = 0;

  // 清空刷新队列，丢弃所有待处理的刷新请求
  // 这对于屏幕切换很重要：旧屏幕的 PARTIAL 刷新请求不应该污染新屏幕
  if (s_refresh_queue != NULL) {
    xQueueReset(s_refresh_queue);
    ESP_LOGI(TAG, "Cleared refresh queue during reset");
  }

  ESP_LOGI(TAG, "Refresh state reset (dirty_valid=%d, partial_count=%u)",
           s_dirty_valid, s_partial_refresh_count);
}

// 清空 framebuffer 为白色
void lvgl_clear_framebuffer(void) {
  if (xSemaphoreTake(s_epd_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    memset(s_epd_framebuffer, 0xFF, sizeof(s_epd_framebuffer));
    xSemaphoreGive(s_epd_mutex);
    ESP_LOGI(TAG, "Framebuffer cleared to white");
  } else {
    ESP_LOGW(TAG, "Failed to acquire mutex for clearing framebuffer");
  }
}

/* ========================================================================
 * 输入设备驱动 - 按键 (LVGL 9.x)
 * ========================================================================*/

// 按键防抖配置
#define KEY_REPEAT_DELAY_MS 300  // 按住后首次重复的延迟
#define KEY_REPEAT_PERIOD_MS 150 // 之后每次重复的周期

// 双击检测配置
#define DOUBLE_CLICK_TIMEOUT_MS 400  // 双击时间窗口（毫秒）

// 按键状态
typedef struct {
  button_t last_key;
  bool pressed;
  lv_point_t point;             // 用于模拟触摸位置（可选）
  uint32_t press_time_ms;       // 按键首次按下的时间
  uint32_t last_repeat_time_ms; // 上次重复事件的时间
  // 双击检测状态
  button_t last_back_key;       // 上次返回键
  uint32_t last_back_release_ms;// 上次返回键释放时间
  bool back_key_double_clicked; // 是否检测到双击
} button_state_t;

static button_state_t btn_state = {.last_key = BTN_NONE,
                                   .pressed = false,
                                   .point = {0, 0},
                                   .press_time_ms = 0,
                                   .last_repeat_time_ms = 0,
                                   .last_back_key = BTN_NONE,
                                   .last_back_release_ms = 0,
                                   .back_key_double_clicked = false};

// LVGL keypad expects the last key to be reported even on RELEASED.
// If key is cleared to 0 too early, some widgets/group navigation may not
// receive KEY events reliably.
static uint32_t s_last_lvgl_key = 0;

// 导出双击状态供外部查询
bool lvgl_is_back_key_double_clicked(void) {
  return btn_state.back_key_double_clicked;
}

void lvgl_clear_back_key_double_click(void) {
  btn_state.back_key_double_clicked = false;
}

// 按键映射辅助函数：将物理按键映射为LVGL按键
static uint32_t map_button_to_lvgl_key(button_t btn) {
  switch (btn) {
  case BTN_CONFIRM:
    return LV_KEY_ENTER;
  case BTN_BACK:
    return LV_KEY_ESC;
  case BTN_LEFT:
    return LV_KEY_LEFT;
  case BTN_RIGHT:
    return LV_KEY_RIGHT;
  case BTN_VOLUME_UP:
    return LV_KEY_PREV; // UP -> PREV (for lv_group navigation)
  case BTN_VOLUME_DOWN:
    return LV_KEY_NEXT; // DOWN -> NEXT
  case BTN_POWER:
  case BTN_NONE:
  default:
    return 0;
  }
}

// 输入设备读取回调 - LVGL 9.x
static void keypad_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  button_t btn = get_pressed_button();

  if (btn != BTN_NONE && btn != btn_state.last_key) {
    // 新按键按下
    btn_state.pressed = true;
    btn_state.last_key = btn;
    btn_state.press_time_ms = 0;
    btn_state.last_repeat_time_ms = 0;

    uint32_t key = map_button_to_lvgl_key(btn);
    data->key = key;
    data->state = LV_INDEV_STATE_PRESSED;
    s_last_lvgl_key = key;

    ESP_LOGI(TAG, "Key pressed: btn=%d -> lvgl_key=%u", btn, key);
  } else if (btn == BTN_NONE && btn_state.pressed) {
    // 按键释放
    btn_state.pressed = false;

    // 检测返回键的双击
    if (btn_state.last_key == BTN_BACK) {
      uint32_t now = lv_tick_get();
      if (btn_state.last_back_key == BTN_BACK &&
          (now - btn_state.last_back_release_ms) < DOUBLE_CLICK_TIMEOUT_MS) {
        // 双击检测成功
        btn_state.back_key_double_clicked = true;
        ESP_LOGI(TAG, "Back key double-clicked detected!");
      } else {
        // 单击或超时，重置双击标志
        btn_state.back_key_double_clicked = false;
      }
      btn_state.last_back_key = BTN_BACK;
      btn_state.last_back_release_ms = now;
    }

    btn_state.last_key = BTN_NONE;
    btn_state.press_time_ms = 0;
    btn_state.last_repeat_time_ms = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = s_last_lvgl_key;
  } else if (btn_state.pressed && btn_state.last_key != BTN_NONE) {
    // 按键持续按下 - 带防抖延迟的重复事件
    uint32_t now = lv_tick_get();
    bool should_repeat = false;

    if (btn_state.press_time_ms == 0) {
      btn_state.press_time_ms = now;
      btn_state.last_repeat_time_ms = now;
      should_repeat = true;
    } else if ((now - btn_state.last_repeat_time_ms) >= KEY_REPEAT_PERIOD_MS &&
               (now - btn_state.press_time_ms) >= KEY_REPEAT_DELAY_MS) {
      btn_state.last_repeat_time_ms = now;
      should_repeat = true;
    }

    if (should_repeat) {
      // 重复按键：使用 UP/DOWN 而非 PREV/NEXT
      uint32_t key;
      switch (btn_state.last_key) {
      case BTN_CONFIRM:
        key = LV_KEY_ENTER;
        break;
      case BTN_BACK:
        key = LV_KEY_ESC;
        break;
      case BTN_LEFT:
        key = LV_KEY_LEFT;
        break;
      case BTN_RIGHT:
        key = LV_KEY_RIGHT;
        break;
      case BTN_VOLUME_UP:
        key = LV_KEY_UP;
        break;
      case BTN_VOLUME_DOWN:
        key = LV_KEY_DOWN;
        break;
      default:
        key = 0;
        break;
      }
      data->key = key;
      data->state = LV_INDEV_STATE_PRESSED;
      s_last_lvgl_key = key;
    } else {
      data->state = LV_INDEV_STATE_RELEASED;
      data->key = 0;
    }
  } else {
    // 无按键
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = s_last_lvgl_key;
  }
}

// 初始化LVGL输入设备驱动 - LVGL 9.x
lv_indev_t *lvgl_input_init(void) {
  ESP_LOGI(TAG, "Initializing LVGL input driver (LVGL 9.x)");

  // 创建输入设备 - LVGL 9.x 新API
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb(indev, keypad_read_cb);

  ESP_LOGI(TAG, "LVGL input driver initialized (UP/DOWN mapped to PREV/NEXT "
                "for lv_group)");

  return indev;
}

// LVGL tick任务
void lvgl_tick_task(void *arg) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10));
    lv_tick_inc(10);
  }
}

// LVGL定时器任务 - 手动刷新模式
// 对于 EPD，我们仍然需要定期调用 lv_timer_handler() 来处理输入事件
// 但渲染是手动的，由 lv_refr_now() 触发
void lvgl_timer_task(void *arg) {
  ESP_LOGI(TAG, "LVGL timer task started (manual refresh mode for EPD)");

  while (1) {
    // 使用 lv_timer_handler_run_in_period() 替代直接调用 lv_timer_handler()
    // 这个函数会确保在指定时间内完成处理，避免长时间阻塞导致看门狗超时
    //
    // 参数 2 表示最多运行 2ms，然后返回，让其他任务有机会运行
    // 这对于 EPD 这种慢速显示设备尤其重要，因为渲染操作可能耗时较长
    //
    // 该函数处理：
    // 1. 输入设备读取和事件分发
    // 2. 动画和定时器
    // 3. 焦点管理
    // 但不会自动触发渲染（需要调用 lv_refr_now()）
    lv_timer_handler_run_in_period(2);

    // 关键：必须调用 vTaskDelay 让出 CPU，确保 idle 任务能运行并喂狗
    // 这个延迟不能省略，否则 idle 任务会饥饿导致看门狗超时
    vTaskDelay(1);
  }
}

/* ========================================================================
 * 文件系统驱动 - SD 卡支持
 * 用于通过文件路径加载图片和字体
 * ========================================================================*/

// 文件系统驱动：打开文件
static void *fs_open_cb(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
  (void)drv;
  char real_path[256];

  // LVGL 9.x: path 参数已经不包含盘符,直接映射到 /sdcard/
  // 例如: "S:/壁纸/锤子灯.jpg" -> path="/壁纸/锤子灯.jpg"
  snprintf(real_path, sizeof(real_path), "/sdcard%s", path);

  const char *fmode = (mode == LV_FS_MODE_WR) ? "wb" : "rb";
  FILE *f = fopen(real_path, fmode);

  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open file: %s (mode=%s)", real_path, fmode);
  } else {
    ESP_LOGD(TAG, "Opened file: %s", real_path);
  }

  return f;
}

// 文件系统驱动：关闭文件
static lv_fs_res_t fs_close_cb(lv_fs_drv_t *drv, void *file) {
  (void)drv;
  if (file == NULL) {
    return LV_FS_RES_INV_PARAM;
  }
  fclose((FILE *)file);
  return LV_FS_RES_OK;
}

// 文件系统驱动：读取文件
static lv_fs_res_t fs_read_cb(lv_fs_drv_t *drv, void *file, void *buf,
                              uint32_t bytes_to_read, uint32_t *bytes_read) {
  (void)drv;
  if (file == NULL) {
    return LV_FS_RES_INV_PARAM;
  }

  size_t read = fread(buf, 1, bytes_to_read, (FILE *)file);
  *bytes_read = read;

  return (read == bytes_to_read) ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;
}

// 文件系统驱动：写入文件
static lv_fs_res_t fs_write_cb(lv_fs_drv_t *drv, void *file, const void *buf,
                               uint32_t bytes_to_write, uint32_t *bytes_written) {
  (void)drv;
  if (file == NULL) {
    return LV_FS_RES_INV_PARAM;
  }

  size_t written = fwrite(buf, 1, bytes_to_write, (FILE *)file);
  *bytes_written = written;

  return (written == bytes_to_write) ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;
}

// 文件系统驱动：定位文件指针
static lv_fs_res_t fs_seek_cb(lv_fs_drv_t *drv, void *file, uint32_t pos,
                              lv_fs_whence_t whence) {
  (void)drv;
  if (file == NULL) {
    return LV_FS_RES_INV_PARAM;
  }

  int w = SEEK_SET;
  if (whence == LV_FS_SEEK_CUR) {
    w = SEEK_CUR;
  } else if (whence == LV_FS_SEEK_END) {
    w = SEEK_END;
  }

  int ret = fseek((FILE *)file, pos, w);
  return (ret == 0) ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;
}

// 文件系统驱动：获取文件大小
static lv_fs_res_t fs_tell_cb(lv_fs_drv_t *drv, void *file, uint32_t *pos) {
  (void)drv;
  if (file == NULL) {
    return LV_FS_RES_INV_PARAM;
  }

  long tell = ftell((FILE *)file);
  if (tell >= 0) {
    *pos = (uint32_t)tell;
    return LV_FS_RES_OK;
  }
  return LV_FS_RES_HW_ERR;
}

// 文件系统驱动：目录读取（LVGL 9.x 需要返回文件名）
static lv_fs_res_t fs_dir_read_cb(lv_fs_drv_t *drv, void *dir, char *fn,
                                  uint32_t fn_len) {
  (void)drv;
  (void)dir;
  (void)fn;
  (void)fn_len;
  return LV_FS_RES_NOT_IMP;
}

// 文件系统驱动：目录打开
static void *fs_dir_open_cb(lv_fs_drv_t *drv, const char *path) {
  (void)drv;
  char real_path[256];

  // LVGL 9.x: path 参数已经不包含盘符,直接映射到 /sdcard/
  snprintf(real_path, sizeof(real_path), "/sdcard%s", path);

  DIR *d = opendir(real_path);
  return d;
}

// 文件系统驱动：目录关闭
static lv_fs_res_t fs_dir_close_cb(lv_fs_drv_t *drv, void *dir) {
  (void)drv;
  if (dir == NULL) {
    return LV_FS_RES_INV_PARAM;
  }
  closedir((DIR *)dir);
  return LV_FS_RES_OK;
}

// 初始化 LVGL 文件系统驱动
void lvgl_fs_init(void) {
  ESP_LOGI(TAG, "Initializing LVGL file system driver for SD card...");

  // 分配驱动结构
  lv_fs_drv_t *fsdrv = malloc(sizeof(lv_fs_drv_t));
  if (fsdrv == NULL) {
    ESP_LOGE(TAG, "Failed to allocate file system driver");
    return;
  }

  // 初始化驱动
  memset(fsdrv, 0, sizeof(lv_fs_drv_t));

  // 设置回调函数
  fsdrv->letter = 'S';                    // 盘符 S:
  fsdrv->cache_size = 0;                  // 不使用缓存
  fsdrv->open_cb = fs_open_cb;
  fsdrv->close_cb = fs_close_cb;
  fsdrv->read_cb = fs_read_cb;
  fsdrv->write_cb = fs_write_cb;
  fsdrv->seek_cb = fs_seek_cb;
  fsdrv->tell_cb = fs_tell_cb;
  fsdrv->dir_read_cb = fs_dir_read_cb;
  fsdrv->dir_open_cb = fs_dir_open_cb;
  fsdrv->dir_close_cb = fs_dir_close_cb;
  fsdrv->user_data = NULL;

  // 注册驱动
  lv_fs_drv_register(fsdrv);

  ESP_LOGI(TAG, "LVGL file system driver registered (S:/ -> /sdcard)");
}
