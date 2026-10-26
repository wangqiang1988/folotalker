// FoloTalker Wi-Fi 配网实现 —— BLE BLUFI(IDF 5.5 API)。
//
// API 用法对齐 ESP-IDF 5.5 的 examples/bluetooth/blufi:
//   esp_blufi_register_callbacks()  注册事件回调与安全协商回调
//   esp_blufi_profile_init()        初始化 BLUFI profile(NimBLE GATT)
//   esp_blufi_adv_start_with_name() 开始广播
//
// 安全协商(DHM + AES + MD5)的代码来自 ESP-IDF 5.5 的 BLUFI 安全示例
// (Unlicense OR CC0-1.0),见 NOTICE。
#include "ft_prov.h"

#include <string.h>

#include "esp_blufi.h"
#include "esp_blufi_api.h"
#include "esp_crc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "host/ble_hs.h"
#include "mbedtls/aes.h"
#include "mbedtls/dhm.h"
#include "mbedtls/md5.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "stdlib.h"

#include "ft_texts.h"
#include "lwip/ip_addr.h"
#include "nvs_flash.h"

static const char *TAG = "ft_prov";

// 广播名。官方 EspBlufi 小程序按 "BLUFI" 前缀过滤设备。
static const char *DEVICE_NAME = "BLUFI_FoloTalker";

#define FT_PROV_AP_LIST_COUNT 10
#define FT_PROV_WIFI_RETRY_MAX 5

// ---------------------------------------------------------------------------
// BLUFI 安全协商(DHM + AES + MD5)
//
// 这部分来自 ESP-IDF 5.5 的 BLUFI 安全示例(Unlicense OR CC0-1.0)。
// ---------------------------------------------------------------------------

#define SEC_TYPE_DH_PARAM_LEN  0x00
#define SEC_TYPE_DH_PARAM_DATA 0x01
#define DH_SELF_PUB_KEY_LEN    128
#define SHARE_KEY_LEN          128
#define DH_PARAM_LEN_MAX       1024
#define PSK_LEN                16

typedef struct {
    uint8_t self_public_key[DH_SELF_PUB_KEY_LEN];
    uint8_t share_key[SHARE_KEY_LEN];
    size_t share_len;
    uint8_t psk[PSK_LEN];
    uint8_t *dh_param;
    int dh_param_len;
    uint8_t iv[16];
    mbedtls_dhm_context dhm;
    mbedtls_aes_context aes;
} blufi_security_t;

static blufi_security_t *s_security;

// BLUFI 内部的上报错误函数(在 bt 组件里实现,这里是它的外部声明)。
extern void btc_blufi_report_error(esp_blufi_error_state_t state);

static int ft_random_bytes(void *state, unsigned char *output, size_t len)
{
    (void)state;
    esp_fill_random(output, len);
    return 0;
}

static void ft_prov_negotiate(uint8_t *data, int len, uint8_t **output_data,
                              int *output_len, bool *need_free)
{
    if (data == NULL || len < 3) {
        btc_blufi_report_error(ESP_BLUFI_DATA_FORMAT_ERROR);
        return;
    }
    if (s_security == NULL) {
        btc_blufi_report_error(ESP_BLUFI_INIT_SECURITY_ERROR);
        return;
    }

    uint8_t type = data[0];
    switch (type) {
    case SEC_TYPE_DH_PARAM_LEN: {
        s_security->dh_param_len = ((data[1] << 8) | data[2]);
        // 限制 DH 参数长度,避免手机端发一个超大长度导致内存耗尽。
        if (s_security->dh_param_len == 0 || s_security->dh_param_len > DH_PARAM_LEN_MAX) {
            s_security->dh_param_len = 0;
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }
        free(s_security->dh_param);
        s_security->dh_param = malloc(s_security->dh_param_len);
        if (s_security->dh_param == NULL) {
            s_security->dh_param_len = 0;
            btc_blufi_report_error(ESP_BLUFI_DH_MALLOC_ERROR);
            return;
        }
        break;
    }

    case SEC_TYPE_DH_PARAM_DATA: {
        if (s_security->dh_param == NULL) {
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }
        if (len < (s_security->dh_param_len + 1)) {
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }
        uint8_t *param = s_security->dh_param;
        memcpy(s_security->dh_param, &data[1], s_security->dh_param_len);
        int ret = mbedtls_dhm_read_params(&s_security->dhm, &param,
                                          &param[s_security->dh_param_len]);
        free(s_security->dh_param);
        s_security->dh_param = NULL;
        if (ret != 0) {
            btc_blufi_report_error(ESP_BLUFI_READ_PARAM_ERROR);
            return;
        }

        const int dhm_len = mbedtls_dhm_get_len(&s_security->dhm);
        if (dhm_len > DH_SELF_PUB_KEY_LEN) {
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }

        ret = mbedtls_dhm_make_public(&s_security->dhm, dhm_len,
                                       s_security->self_public_key,
                                       DH_SELF_PUB_KEY_LEN, ft_random_bytes, NULL);
        if (ret != 0) {
            btc_blufi_report_error(ESP_BLUFI_MAKE_PUBLIC_ERROR);
            return;
        }

        ret = mbedtls_dhm_calc_secret(&s_security->dhm, s_security->share_key,
                                      SHARE_KEY_LEN, &s_security->share_len,
                                      ft_random_bytes, NULL);
        if (ret != 0) {
            btc_blufi_report_error(ESP_BLUFI_DH_PARAM_ERROR);
            return;
        }

        ret = mbedtls_md5(s_security->share_key, s_security->share_len, s_security->psk);
        if (ret != 0) {
            btc_blufi_report_error(ESP_BLUFI_CALC_MD5_ERROR);
            return;
        }

        mbedtls_aes_setkey_enc(&s_security->aes, s_security->psk, PSK_LEN * 8);

        *output_data = &s_security->self_public_key[0];
        *output_len = dhm_len;
        *need_free = false;
        break;
    }

    default:
        btc_blufi_report_error(ESP_BLUFI_DATA_FORMAT_ERROR);
        break;
    }
}

static int ft_prov_encrypt(uint8_t iv8, uint8_t *data, int len)
{
    if (s_security == NULL) {
        return 0;
    }
    s_security->iv[0] = iv8;
    int rc = mbedtls_aes_crypt_cbc(&s_security->aes, MBEDTLS_AES_ENCRYPT, len,
                                   s_security->iv, data, data);
    if (rc != 0) {
        btc_blufi_report_error(ESP_BLUFI_ENCRYPT_ERROR);
    }
    return rc;
}

static int ft_prov_decrypt(uint8_t iv8, uint8_t *data, int len)
{
    if (s_security == NULL) {
        return 0;
    }
    s_security->iv[0] = iv8;
    int rc = mbedtls_aes_crypt_cbc(&s_security->aes, MBEDTLS_AES_DECRYPT, len,
                                   s_security->iv, data, data);
    if (rc != 0) {
        btc_blufi_report_error(ESP_BLUFI_DECRYPT_ERROR);
    }
    return rc;
}

static uint16_t ft_prov_checksum(uint8_t iv8, uint8_t *data, int len)
{
    (void)iv8;
    return esp_crc16_le(0x1021, data, len);
}

static int ft_prov_security_init(void)
{
    if (s_security != NULL) {
        return 0;
    }
    s_security = calloc(1, sizeof(blufi_security_t));
    if (s_security == NULL) {
        return -1;
    }
    mbedtls_dhm_init(&s_security->dhm);
    mbedtls_aes_init(&s_security->aes);
    esp_fill_random(s_security->psk, PSK_LEN);
    return 0;
}

static void ft_prov_security_deinit(void)
{
    if (s_security == NULL) {
        return;
    }
    free(s_security->dh_param);
    mbedtls_dhm_free(&s_security->dhm);
    mbedtls_aes_free(&s_security->aes);
    free(s_security);
    s_security = NULL;
}

// ---------------------------------------------------------------------------
// 配网状态
// ---------------------------------------------------------------------------

static ft_prov_state_t s_state = FT_PROV_IDLE;
static ft_prov_cb_t s_cb;
static bool s_initialized;
static bool s_profile_initialized;
static bool s_ble_connected;
static bool s_wifi_connecting;
static bool s_wifi_got_ip;
static uint8_t s_wifi_retry;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_handler_registered;
static bool s_ip_handler_registered;
static wifi_config_t s_sta_config;
static int s_sta_ssid_len;
static uint8_t s_sta_ssid[32];
static esp_blufi_extra_info_t s_conn_info;

static void ft_prov_notify(ft_prov_state_t state, const char *msg)
{
    s_state = state;
    if (s_cb != NULL) {
        s_cb(state, msg);
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi 凭证持久化
//
// 凭证存进加密 NVS 分区(见 sdkconfig.defaults 的 CONFIG_NVS_ENCRYPTION),
// 重启后自动重连,不用每次重新配网。
//
// ★ 密码绝不能写日志,也绝不能提交到仓库。
// ---------------------------------------------------------------------------

#define FT_WIFI_NVS_NAMESPACE "folotalker"
#define FT_WIFI_NVS_KEY_SSID  "wifi_ssid"
#define FT_WIFI_NVS_KEY_PASS  "wifi_pass"

static void ft_prov_save_credentials(void)
{
    nvs_handle_t nvs;
    if (nvs_open(FT_WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "打开 NVS 失败,凭证未保存");
        return;
    }
    nvs_set_str(nvs, FT_WIFI_NVS_KEY_SSID, (const char *)s_sta_config.sta.ssid);
    nvs_set_str(nvs, FT_WIFI_NVS_KEY_PASS, (const char *)s_sta_config.sta.password);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Wi-Fi 凭证已保存");
}

// 从 NVS 读回凭证。成功返回 true。
static bool ft_prov_load_credentials(wifi_config_t *cfg)
{
    nvs_handle_t nvs;
    if (nvs_open(FT_WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    size_t len = sizeof(ssid);
    bool ok = (nvs_get_str(nvs, FT_WIFI_NVS_KEY_SSID, ssid, &len) == ESP_OK);
    if (ok) {
        len = sizeof(pass);
        ok = (nvs_get_str(nvs, FT_WIFI_NVS_KEY_PASS, pass, &len) == ESP_OK);
    }
    nvs_close(nvs);
    if (!ok || ssid[0] == '\0') {
        return false;
    }
    memset(cfg, 0, sizeof(*cfg));
    memcpy(cfg->sta.ssid, ssid, sizeof(ssid) - 1);
    memcpy(cfg->sta.password, pass, sizeof(pass) - 1);
    cfg->sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    return true;
}

// ---------------------------------------------------------------------------
// Wi-Fi 连接
// ---------------------------------------------------------------------------

static void ft_prov_send_wifi_report(wifi_mode_t mode, esp_blufi_sta_conn_state_t state)
{
    if (!s_ble_connected) {
        return;
    }
    esp_blufi_extra_info_t info = { 0 };
    memcpy(info.sta_bssid, s_conn_info.sta_bssid, 6);
    info.sta_bssid_set = s_conn_info.sta_bssid_set;
    info.sta_ssid = s_sta_ssid;
    info.sta_ssid_len = s_sta_ssid_len;
    esp_blufi_send_wifi_conn_report(mode, state, 0, &info);
}

static void ft_prov_send_ap_list(void)
{
    uint16_t count = FT_PROV_AP_LIST_COUNT;
    wifi_ap_record_t records[FT_PROV_AP_LIST_COUNT] = { 0 };
    esp_blufi_ap_record_t list[FT_PROV_AP_LIST_COUNT] = { 0 };
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        return;
    }
    for (uint16_t i = 0; i < count; i++) {
        list[i].rssi = records[i].rssi;
        memcpy(list[i].ssid, records[i].ssid, sizeof(list[i].ssid));
    }
    if (s_ble_connected) {
        esp_blufi_send_wifi_list(count, list);
    }
}

static void ft_prov_wifi_connect(void)
{
    s_wifi_retry = 0;
    s_wifi_connecting = (esp_wifi_connect() == ESP_OK);
    ft_prov_notify(FT_PROV_CONNECTING, FT_TXT_PROV_SCANNING);
}

static bool ft_prov_wifi_reconnect(void)
{
    bool ret = false;
    if (s_wifi_connecting && s_wifi_retry++ < FT_PROV_WIFI_RETRY_MAX) {
        s_wifi_connecting = (esp_wifi_connect() == ESP_OK);
        ret = true;
    }
    return ret;
}

static void ft_prov_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_get_config(WIFI_IF_STA, &s_sta_config);
    } else if (id == WIFI_EVENT_STA_STOP) {
        s_wifi_connecting = false;
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        s_wifi_connecting = false;
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_got_ip = false;
        if (!ft_prov_wifi_reconnect()) {
            s_wifi_connecting = false;
            ft_prov_notify(FT_PROV_FAILED, FT_TXT_PROV_FAILED);
        }
    }
}

static void ft_prov_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_wifi_got_ip = true;
        s_wifi_connecting = false;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "已获取 IP: %s", ip);
        // 把成功状态回报给手机,官方小程序据此显示"已连接"。
        ft_prov_send_wifi_report(WIFI_MODE_STA, ESP_BLUFI_STA_CONN_SUCCESS);
        ft_prov_notify(FT_PROV_SUCCESS, FT_TXT_PROV_CONNECTED);
    } else if (id == IP_EVENT_STA_LOST_IP) {
        s_wifi_got_ip = false;
    }
}

// ---------------------------------------------------------------------------
// BLUFI 事件回调
// ---------------------------------------------------------------------------

static void ft_prov_blufi_event(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        ft_prov_send_ap_list();
        break;

    case ESP_BLUFI_EVENT_BLE_CONNECT:
        s_ble_connected = true;
        ft_prov_notify(FT_PROV_CONNECTED, NULL);
        break;

    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        s_ble_connected = false;
        ft_prov_notify(FT_PROV_ADVERTISING, NULL);
        break;

    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        ft_prov_wifi_connect();
        break;

    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        esp_wifi_disconnect();
        break;

    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (param != NULL && param->sta_ssid.ssid != NULL) {
            s_sta_ssid_len = param->sta_ssid.ssid_len;
            if (s_sta_ssid_len > (int)sizeof(s_sta_ssid)) {
                s_sta_ssid_len = sizeof(s_sta_ssid);
            }
            memcpy(s_sta_ssid, param->sta_ssid.ssid, s_sta_ssid_len);
            memset(s_sta_config.sta.ssid, 0, sizeof(s_sta_config.sta.ssid));
            memcpy(s_sta_config.sta.ssid, param->sta_ssid.ssid, s_sta_ssid_len);
        }
        break;

    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (param != NULL && param->sta_passwd.passwd != NULL) {
            memset(s_sta_config.sta.password, 0, sizeof(s_sta_config.sta.password));
            int len = param->sta_passwd.passwd_len;
            if (len > (int)sizeof(s_sta_config.sta.password) - 1) {
                len = sizeof(s_sta_config.sta.password) - 1;
            }
            memcpy(s_sta_config.sta.password, param->sta_passwd.passwd, len);
            // 凭证由手机下发,写进 STA 配置后立即连接。
            // 注意:密码不能写日志,也不能提交到仓库。
            esp_wifi_set_config(WIFI_IF_STA, &s_sta_config);
            // 存进 NVS,重启后自动重连,不用每次重新配网。
            // NVS 分区是加密的(见 sdkconfig.defaults 的 CONFIG_NVS_ENCRYPTION)。
            ft_prov_save_credentials();
        }
        break;

    case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        if (param != NULL) {
            memcpy(s_conn_info.sta_bssid, param->sta_bssid.bssid, 6);
            s_conn_info.sta_bssid_set = true;
        }
        break;

    case ESP_BLUFI_EVENT_GET_WIFI_LIST:
        ft_prov_send_ap_list();
        break;

    case ESP_BLUFI_EVENT_REPORT_ERROR:
        ESP_LOGW(TAG, "BLUFI 上报错误");
        ft_prov_notify(FT_PROV_FAILED, FT_TXT_PROV_FAILED);
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// NimBLE GAP 事件
//
// 本版 NimBLE 的 ble_hs_cfg 里没有 gap_event_cb 字段,GAP 回调是
// ble_gap_adv_start() 的参数。BLUFI 组件自己通过
// esp_blufi_adv_start_with_name() 启动广播并处理连接事件,所以这里
// 不再注册自己的 GAP 回调,只依赖 BLUFI 的 event_cb 上报连接状态。
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

bool ft_prov_init(ft_prov_cb_t cb)
{
    if (s_initialized) {
        s_cb = cb;
        return true;
    }
    s_cb = cb;

    if (ft_prov_security_init() != 0) {
        ESP_LOGE(TAG, "安全协商初始化失败");
        return false;
    }

    // esp_netif_init() 在 IDF 5.5 里返回 void。
    esp_netif_init();
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "事件循环创建失败");
        return false;
    }

    if (esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 初始化失败");
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        ESP_LOGE(TAG, "创建默认 STA netif 失败");
        return false;
    }

    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            ft_prov_wifi_event, NULL,
                                            &s_wifi_handler) != ESP_OK) {
        ESP_LOGE(TAG, "注册 Wi-Fi 事件失败");
        return false;
    }
    s_wifi_handler_registered = true;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            ft_prov_ip_event, NULL,
                                            &s_ip_handler) != ESP_OK) {
        ESP_LOGE(TAG, "注册 IP 事件失败");
        return false;
    }
    s_ip_handler_registered = true;

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE 端口初始化失败");
        return false;
    }
    // nimble_port_run() 在本版 IDF 里返回 void。
    nimble_port_run();

    ble_hs_cfg.sync_cb = NULL;
    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // BLE 配网要求加密链路,安全能力按 BLUFI 官方示例配置。
    ble_hs_cfg.sm_io_cap = 4;      // ESP_BLE_IO_CAP_NONE
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sec_lvl = 1;      // BLE_HS_SEC_LVL_1
    ble_hs_cfg.sm_sc = 0;           // 用 Legacy Pairing,兼容官方小程序

    // 注册 BLUFI 回调
    esp_blufi_callbacks_t callbacks = {
        .event_cb = ft_prov_blufi_event,
        .negotiate_data_handler = ft_prov_negotiate,
        .encrypt_func = ft_prov_encrypt,
        .decrypt_func = ft_prov_decrypt,
        .checksum_func = ft_prov_checksum,
    };
    if (esp_blufi_register_callbacks(&callbacks) != ESP_OK) {
        ESP_LOGE(TAG, "注册 BLUFI 回调失败");
        return false;
    }

    if (esp_blufi_profile_init() != ESP_OK) {
        ESP_LOGE(TAG, "BLUFI profile 初始化失败");
        return false;
    }
    s_profile_initialized = true;

    s_initialized = true;
    return true;
}

void ft_prov_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    ft_prov_stop();

    if (s_profile_initialized) {
        esp_blufi_profile_deinit();
        s_profile_initialized = false;
    }
    if (s_ip_handler_registered) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &s_ip_handler);
        s_ip_handler_registered = false;
    }
    if (s_wifi_handler_registered) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &s_wifi_handler);
        s_wifi_handler_registered = false;
    }
    ft_prov_security_deinit();
    s_initialized = false;
    s_state = FT_PROV_IDLE;
}

bool ft_prov_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "配网模块尚未初始化");
        return false;
    }
    // 用带名字的广播启动,官方小程序按名字过滤设备。
    esp_blufi_adv_start_with_name(DEVICE_NAME);
    ft_prov_notify(FT_PROV_ADVERTISING, NULL);
    return true;
}

void ft_prov_stop(void)
{
    if (!s_initialized) {
        return;
    }
    esp_blufi_adv_stop();
    esp_blufi_disconnect();
    s_ble_connected = false;
    s_state = FT_PROV_IDLE;
}

ft_prov_state_t ft_prov_state(void)
{
    return s_state;
}

bool ft_prov_load_stored_credentials(wifi_config_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    return ft_prov_load_credentials(cfg);
}

bool ft_prov_reconnect_stored(void)
{
    wifi_config_t cfg;
    if (!ft_prov_load_credentials(&cfg)) {
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        return false;
    }
    memcpy(&s_sta_config, &cfg, sizeof(cfg));
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "连接已保存的 Wi-Fi 失败: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "正在连接已保存的 Wi-Fi");
    return true;
}

const char *ft_prov_state_text(ft_prov_state_t state)
{
    switch (state) {
    case FT_PROV_IDLE:        return FT_TXT_TITLE_PROV;
    case FT_PROV_ADVERTISING: return FT_TXT_PROV_SCANNING;
    case FT_PROV_CONNECTED:  return FT_TXT_PROV_HINT;
    case FT_PROV_CONNECTING: return FT_TXT_PROV_SCANNING;
    case FT_PROV_SUCCESS:     return FT_TXT_PROV_CONNECTED;
    case FT_PROV_FAILED:      return FT_TXT_PROV_FAILED;
    default:                  return FT_TXT_TITLE_PROV;
    }
}
