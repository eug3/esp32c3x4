/**
 * @file ui_region_manager.c
 * @brief UI 区域管理器实现
 */

#include "ui_region_manager.h"
#include "display_engine.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_REGION_MGR";

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool regions_overlap(const ui_region_t *r1, const ui_region_t *r2);
static bool regions_adjacent(const ui_region_t *r1, const ui_region_t *r2, int threshold);
static void merge_two_regions(ui_region_t *dst, const ui_region_t *src);

/**********************
 *  STATIC FUNCTIONS
 **********************/

/**
 * @brief 检查两个区域是否重叠
 */
static bool regions_overlap(const ui_region_t *r1, const ui_region_t *r2)
{
    if (!r1->valid || !r2->valid) {
        return false;
    }

    int r1_x2 = r1->x + r1->width;
    int r1_y2 = r1->y + r1->height;
    int r2_x2 = r2->x + r2->width;
    int r2_y2 = r2->y + r2->height;

    // 检查是否有重叠
    return !(r1->x >= r2_x2 || r2->x >= r1_x2 || 
             r1->y >= r2_y2 || r2->y >= r1_y2);
}

/**
 * @brief 检查两个区域是否相邻（距离小于阈值）
 */
static bool regions_adjacent(const ui_region_t *r1, const ui_region_t *r2, int threshold)
{
    if (!r1->valid || !r2->valid) {
        return false;
    }

    int r1_x2 = r1->x + r1->width;
    int r1_y2 = r1->y + r1->height;
    int r2_x2 = r2->x + r2->width;
    int r2_y2 = r2->y + r2->height;

    // 计算水平和垂直间距
    int h_gap = 0;
    int v_gap = 0;

    if (r1_x2 < r2->x) {
        h_gap = r2->x - r1_x2;
    } else if (r2_x2 < r1->x) {
        h_gap = r1->x - r2_x2;
    }

    if (r1_y2 < r2->y) {
        v_gap = r2->y - r1_y2;
    } else if (r2_y2 < r1->y) {
        v_gap = r1->y - r2_y2;
    }

    // 如果水平或垂直方向有重叠或间距小于阈值，认为相邻
    return (h_gap == 0 && v_gap <= threshold) || 
           (v_gap == 0 && h_gap <= threshold);
}

/**
 * @brief 合并两个区域到 dst
 */
static void merge_two_regions(ui_region_t *dst, const ui_region_t *src)
{
    if (!src->valid) {
        return;
    }

    if (!dst->valid) {
        *dst = *src;
        return;
    }

    int dst_x2 = dst->x + dst->width;
    int dst_y2 = dst->y + dst->height;
    int src_x2 = src->x + src->width;
    int src_y2 = src->y + src->height;

    int new_x = (dst->x < src->x) ? dst->x : src->x;
    int new_y = (dst->y < src->y) ? dst->y : src->y;
    int new_x2 = (dst_x2 > src_x2) ? dst_x2 : src_x2;
    int new_y2 = (dst_y2 > src_y2) ? dst_y2 : src_y2;

    dst->x = new_x;
    dst->y = new_y;
    dst->width = new_x2 - new_x;
    dst->height = new_y2 - new_y;
    dst->valid = true;
}

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void ui_region_manager_init(ui_region_manager_t *manager, bool auto_refresh)
{
    if (manager == NULL) {
        ESP_LOGE(TAG, "manager is NULL");
        return;
    }

    memset(manager, 0, sizeof(ui_region_manager_t));
    manager->auto_refresh = auto_refresh;
    ESP_LOGI(TAG, "Region manager initialized (auto_refresh=%d)", auto_refresh);
}

void ui_region_manager_clear(ui_region_manager_t *manager)
{
    if (manager == NULL) {
        return;
    }

    manager->region_count = 0;
    memset(manager->regions, 0, sizeof(manager->regions));
}

bool ui_region_manager_add_region(ui_region_manager_t *manager, 
                                   int x, int y, int width, int height)
{
    if (manager == NULL) {
        ESP_LOGE(TAG, "manager is NULL");
        return false;
    }

    if (manager->region_count >= MAX_UPDATE_REGIONS) {
        ESP_LOGW(TAG, "Region list is full (%d/%d)", 
                 manager->region_count, MAX_UPDATE_REGIONS);
        return false;
    }

    // 边界检查
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > SCREEN_WIDTH) width = SCREEN_WIDTH - x;
    if (y + height > SCREEN_HEIGHT) height = SCREEN_HEIGHT - y;

    if (width <= 0 || height <= 0) {
        ESP_LOGW(TAG, "Invalid region: x=%d, y=%d, w=%d, h=%d", x, y, width, height);
        return false;
    }

    ui_region_t *region = &manager->regions[manager->region_count];
    region->x = x;
    region->y = y;
    region->width = width;
    region->height = height;
    region->valid = true;

    manager->region_count++;
    ESP_LOGD(TAG, "Added region %d: x=%d, y=%d, w=%d, h=%d", 
             manager->region_count - 1, x, y, width, height);

    return true;
}

bool ui_region_manager_add_focus_change(ui_region_manager_t *manager,
                                         int old_x, int old_y, int old_width, int old_height,
                                         int new_x, int new_y, int new_width, int new_height)
{
    if (manager == NULL) {
        return false;
    }

    bool success = true;

    // 添加旧焦点区域（如果有效）
    if (old_x >= 0 && old_width > 0 && old_height > 0) {
        success = ui_region_manager_add_region(manager, old_x, old_y, old_width, old_height);
        if (!success) {
            return false;
        }
    }

    // 添加新焦点区域
    success = ui_region_manager_add_region(manager, new_x, new_y, new_width, new_height);
    if (!success) {
        return false;
    }

    ESP_LOGI(TAG, "Added focus change: old(%d,%d,%d,%d) -> new(%d,%d,%d,%d)",
             old_x, old_y, old_width, old_height,
             new_x, new_y, new_width, new_height);

    return true;
}

void ui_region_manager_draw_and_refresh(ui_region_manager_t *manager,
                                        region_draw_callback_t draw_callback,
                                        void *user_data)
{
    if (manager == NULL || draw_callback == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return;
    }

    if (manager->region_count == 0) {
        ESP_LOGD(TAG, "No regions to draw");
        return;
    }

    ESP_LOGI(TAG, "Drawing and refreshing %d regions...", manager->region_count);

    // 遍历所有区域
    for (int i = 0; i < manager->region_count; i++) {
        const ui_region_t *region = &manager->regions[i];
        if (!region->valid) {
            continue;
        }

        ESP_LOGD(TAG, "Region %d: drawing x=%d, y=%d, w=%d, h=%d", 
                 i, region->x, region->y, region->width, region->height);

        // 调用绘制回调
        draw_callback(region, user_data);

        // 如果启用自动刷新，立即局刷这个区域
        if (manager->auto_refresh) {
            ESP_LOGD(TAG, "Region %d: refreshing...", i);
            display_refresh_region(region->x, region->y, 
                                  region->width, region->height, 
                                  REFRESH_MODE_PARTIAL);
        }
    }

    // 如果没有自动刷新，统一刷新一次
    if (!manager->auto_refresh && manager->region_count > 0) {
        ESP_LOGI(TAG, "Refreshing all regions together...");
        display_refresh(REFRESH_MODE_PARTIAL);
    }

    ESP_LOGI(TAG, "All regions drawn and refreshed");
}

int ui_region_manager_get_count(const ui_region_manager_t *manager)
{
    if (manager == NULL) {
        return 0;
    }
    return manager->region_count;
}

const ui_region_t* ui_region_manager_get_region(const ui_region_manager_t *manager, int index)
{
    if (manager == NULL || index < 0 || index >= manager->region_count) {
        return NULL;
    }
    return &manager->regions[index];
}

void ui_region_manager_merge_regions(ui_region_manager_t *manager)
{
    if (manager == NULL || manager->region_count <= 1) {
        return;
    }

    ESP_LOGI(TAG, "Merging %d regions...", manager->region_count);

    // 相邻阈值（像素）
    const int ADJACENT_THRESHOLD = 20;

    bool merged = true;
    while (merged && manager->region_count > 1) {
        merged = false;

        for (int i = 0; i < manager->region_count - 1; i++) {
            if (!manager->regions[i].valid) {
                continue;
            }

            for (int j = i + 1; j < manager->region_count; j++) {
                if (!manager->regions[j].valid) {
                    continue;
                }

                // 检查是否重叠或相邻
                if (regions_overlap(&manager->regions[i], &manager->regions[j]) ||
                    regions_adjacent(&manager->regions[i], &manager->regions[j], ADJACENT_THRESHOLD)) {
                    
                    ESP_LOGD(TAG, "Merging region %d and %d", i, j);
                    merge_two_regions(&manager->regions[i], &manager->regions[j]);
                    
                    // 标记 j 为无效
                    manager->regions[j].valid = false;
                    merged = true;
                }
            }
        }
    }

    // 压缩区域列表（移除无效区域）
    int new_count = 0;
    for (int i = 0; i < manager->region_count; i++) {
        if (manager->regions[i].valid) {
            if (i != new_count) {
                manager->regions[new_count] = manager->regions[i];
            }
            new_count++;
        }
    }

    int old_count = manager->region_count;
    manager->region_count = new_count;

    if (old_count != new_count) {
        ESP_LOGI(TAG, "Merged %d regions into %d", old_count, new_count);
    }
}
