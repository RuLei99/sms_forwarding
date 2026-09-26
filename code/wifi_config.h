//WIFI - 兜底默认值：设备会优先使用网页「网络测试」页配置的主 WiFi（存 NVS），
// 此宏仅在设备未配置过主 WiFi 时生效。修改后需重新编译烧录。
#define WIFI_SSID "405"
#define WIFI_PASS "15968499913zhihui"

//备用 WIFI - 兜底默认值：主 WiFi 连不上时的备网（网页未配置备用时生效）。
//注意：手机热点需开 2.4GHz 频段，ESP32 不支持 5GHz。
#define WIFI2_SSID "OPPO Find X7 1318"
#define WIFI2_PASS "3026302611"