/** Sub0h264 ESP-IDF hello-world
 *
 * Prints library version, platform name, and chip info.
 */
#include <sub0h264/sub0h264.hpp>

#include "esp_chip_info.h"
#include "esp_log.h"

static const char* cTag = "hello_h264";

extern "C" void app_main()
{
    ESP_LOGI(cTag, "Sub0h264 v%s", sub0h264::getVersionString());
    ESP_LOGI(cTag, "Platform: %s", sub0h264::platformName());

    const sub0h264::Version v = sub0h264::getVersion();
    ESP_LOGI(cTag, "Version struct: %u.%u.%u", v.major_, v.minor_, v.patch_);

    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);
    ESP_LOGI(cTag, "Chip: model=%d, cores=%d, revision=%d",
             chipInfo.model, chipInfo.cores, chipInfo.revision);

    ESP_LOGI(cTag, "Hello from Sub0h264!");
}
