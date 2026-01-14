/**
 * @file epub_cache.c
 * @brief EPUB Flash/LittleFS 缓存实现
 */

#include "epub_cache.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

static const char *TAG = "EPUB_CACHE";

#define CACHE_DIR "/littlefs/epub_cache"
#define CACHE_PREFIX "ec_"

static uint32_t fnv1a32(const char *s, uint32_t h)
{
    const uint32_t FNV_PRIME = 16777619u;
    if (s == NULL) {
        return h;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= (uint32_t)(*p);
        h *= FNV_PRIME;
    }
    return h;
}

static uint32_t make_key_hash(const epub_cache_key_t *key)
{
    uint32_t h = 2166136261u;
    if (key == NULL) {
        return h;
    }
    h = fnv1a32(key->epub_path, h);
    h = fnv1a32("|", h);
    h = fnv1a32(key->content_path, h);
    h = fnv1a32("|", h);
    // type
    char type_buf[16];
    (void)snprintf(type_buf, sizeof(type_buf), "%d", (int)key->type);
    h = fnv1a32(type_buf, h);
    return h;
}

static bool ensure_cache_dir(void)
{
    struct stat st;
    if (stat(CACHE_DIR, &st) == 0) {
        return true;
    }

    // 兼容：父目录可能尚未创建
    (void)mkdir("/littlefs", 0755);

    if (mkdir(CACHE_DIR, 0755) != 0) {
        if (errno == EEXIST) {
            return true;
        }
        ESP_LOGE(TAG, "Failed to create cache dir: %s (errno=%d)", CACHE_DIR, errno);
        return false;
    }
    return true;
}

bool epub_cache_get_file_path(const epub_cache_key_t *key, char *out_path, size_t out_size)
{
    if (key == NULL || out_path == NULL || out_size < 16) {
        return false;
    }
    uint32_t h = make_key_hash(key);
    int n = snprintf(out_path, out_size, "%s/%s%08x_%u.bin", CACHE_DIR, CACHE_PREFIX,
                     (unsigned)h, (unsigned)key->type);
    return (n > 0 && (size_t)n < out_size);
}

static size_t get_file_size(const char *path)
{
    struct stat st;
    if (path == NULL) {
        return 0;
    }
    if (stat(path, &st) != 0) {
        return 0;
    }
    return (size_t)st.st_size;
}

bool epub_cache_get_usage(size_t *used, size_t *total)
{
    if (used) {
        *used = 0;
    }
    if (total) {
        *total = EPUB_CACHE_MAX_SIZE;
    }

    if (!ensure_cache_dir()) {
        return false;
    }

    DIR *dir = opendir(CACHE_DIR);
    if (dir == NULL) {
        return false;
    }

    size_t sum = 0;
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (strncmp(name, CACHE_PREFIX, strlen(CACHE_PREFIX)) != 0) {
            continue;
        }
        char full[256];
        int n = snprintf(full, sizeof(full), "%s/%s", CACHE_DIR, name);
        if (n <= 0 || (size_t)n >= sizeof(full)) {
            continue;
        }
        sum += get_file_size(full);
    }

    closedir(dir);

    if (used) {
        *used = sum;
    }
    return true;
}

bool epub_cache_init(void)
{
    bool ok = ensure_cache_dir();
    if (ok) {
        size_t used = 0;
        size_t total = 0;
        (void)epub_cache_get_usage(&used, &total);
        ESP_LOGI(TAG, "Cache ready: used=%u total=%u", (unsigned)used, (unsigned)total);
    }
    return ok;
}

bool epub_cache_exists(const epub_cache_key_t *key)
{
    if (key == NULL) {
        return false;
    }
    char path[256];
    if (!epub_cache_get_file_path(key, path, sizeof(path))) {
        return false;
    }
    struct stat st;
    return (stat(path, &st) == 0);
}

int epub_cache_read(const epub_cache_key_t *key, void *buffer, size_t buffer_size)
{
    if (key == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    char path[256];
    if (!epub_cache_get_file_path(key, path, sizeof(path))) {
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }

    size_t n = fread(buffer, 1, buffer_size, f);
    fclose(f);
    return (int)n;
}

bool epub_cache_clear(void)
{
    if (!ensure_cache_dir()) {
        return false;
    }

    DIR *dir = opendir(CACHE_DIR);
    if (dir == NULL) {
        return false;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (strncmp(name, CACHE_PREFIX, strlen(CACHE_PREFIX)) != 0) {
            continue;
        }

        char full[256];
        int n = snprintf(full, sizeof(full), "%s/%s", CACHE_DIR, name);
        if (n <= 0 || (size_t)n >= sizeof(full)) {
            continue;
        }
        (void)remove(full);
    }

    closedir(dir);
    return true;
}

bool epub_cache_delete(const epub_cache_key_t *key)
{
    if (key == NULL) {
        return false;
    }
    char path[256];
    if (!epub_cache_get_file_path(key, path, sizeof(path))) {
        return false;
    }
    return (remove(path) == 0);
}

bool epub_cache_write(const epub_cache_key_t *key, const void *data, size_t data_size)
{
    if (key == NULL || data == NULL || data_size == 0) {
        return false;
    }

    if (!ensure_cache_dir()) {
        return false;
    }

    size_t used = 0;
    size_t total = 0;
    if (epub_cache_get_usage(&used, &total)) {
        if (used + data_size > EPUB_CACHE_MAX_SIZE) {
            ESP_LOGW(TAG, "Cache full (used=%u add=%u), clearing", (unsigned)used, (unsigned)data_size);
            (void)epub_cache_clear();
        }
    }

    char path[256];
    if (!epub_cache_get_file_path(key, path, sizeof(path))) {
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to write cache file: %s", path);
        return false;
    }

    size_t n = fwrite(data, 1, data_size, f);
    fclose(f);
    return (n == data_size);
}

bool epub_cache_precache_chapter(const char *epub_path,
                                const char *chapter_path,
                                const void *data,
                                size_t data_size)
{
    if (epub_path == NULL || chapter_path == NULL) {
        return false;
    }

    epub_cache_key_t key;
    memset(&key, 0, sizeof(key));
    key.type = EPUB_CACHE_CHAPTER;
    strncpy(key.epub_path, epub_path, sizeof(key.epub_path) - 1);
    strncpy(key.content_path, chapter_path, sizeof(key.content_path) - 1);

    return epub_cache_write(&key, data, data_size);
}
