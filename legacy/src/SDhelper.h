
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

static const char* TAG = "PumpMonitor";
sdmmc_card_t* card;
static bool SD_enabled = false;
bool initializeSD() {
    Serial.println("Initializing SD card");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // To use 1-line SD mode, uncomment the following line:
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_PROBING;

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and formatted
    // in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            Serial.println("Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
            // ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            Serial.println("Failed to initialize the card (%d). Make sure SD card lines have pull-up resistors in place.");
            // ESP_LOGE(TAG, "Failed to initialize the card (%d). Make sure SD card lines have pull-up resistors in place.", ret);
        }
        SD_enabled = false;
        return SD_enabled;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    Serial.println("Opening file");
    // ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/sdcard/hello.txt", "w");
    if (f == NULL) {
        Serial.println("Failed to open file for writing");
        // ESP_LOGE(TAG, "Failed to open file for writing");
        SD_enabled = false;
        return SD_enabled;
    }
    fprintf(f, "Hello %s!\n", card->cid.name);
    fclose(f);
    Serial.println("File written");
    // ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/sdcard/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/sdcard/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/sdcard/hello.txt", "/sdcard/foo.txt") != 0) {
        Serial.println("Rename failed");
        // ESP_LOGE(TAG, "Rename failed");
        SD_enabled = false;
        return SD_enabled;
    }

    // Open renamed file for reading
    Serial.println("Reading file");
    // ESP_LOGI(TAG, "Reading file");
    f = fopen("/sdcard/foo.txt", "r");
    if (f == NULL) {
        Serial.println("Failed to open file for reading");
        // ESP_LOGE(TAG, "Failed to open file for reading");
        SD_enabled = false;
        return SD_enabled;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    Serial.print("Read from file: ");
    Serial.println(line);

    // ESP_LOGI(TAG, "Read from file: '%s'", line);

    // All done, unmount partition and disable SDMMC host peripheral
    // esp_vfs_fat_sdmmc_unmount();
    // Serial.println("Card unmounted");
    // ESP_LOGI(TAG, "Card unmounted");

    SD_enabled = true;
    return SD_enabled;
}

void writeHistoryFile(String fileName, String values, const char* wt) {
    String path = fileName;
    path += ".txt";
    Serial.println("Crearting file");
    
    // ESP_LOGI(TAG, "Opening file");
    char _dataFileName[path.length()];
    path.toCharArray(_dataFileName, sizeof(_dataFileName) + 1);
    // Check if destination file exists before renaming
    FILE* f;
    struct stat st;
    if (stat(_dataFileName, &st) == 0) {
        f = fopen(_dataFileName, wt);;
    }
    else {
        f = fopen(_dataFileName, "w");
    }
    if (f == NULL) {
        Serial.println("Failed to open file for writing");
        return;
    }
    //fprintf(f, "Hello %s!\n", card->cid.name);
    char _values[values.length()];
    values.toCharArray(_values, sizeof(_values) + 1);
    fprintf(f, _values);

    fclose(f);
    Serial.print("File ");
    Serial.print(_dataFileName);
    Serial.println(" written");
}

String getHistoryFileContent(String fileName, String backupName) {
    String path = fileName;
    path += ".txt";
    char _dataFileName[path.length()];
    path.toCharArray(_dataFileName, sizeof(_dataFileName) + 1);
    FILE* f = fopen(_dataFileName, "r");
    if (f == NULL) {
        // ESP_LOGE(TAG, "Failed to open file for reading");
        return "File not exist";
    }
    String fileInfo = backupName + "&";
    char line[300];
    const int maxReads = 288;
    for (int i = 0; (i < maxReads && !feof(f)); i++) {
        fgets(line, sizeof(line), f);
        // strip newline
        char* pos = strchr(line, '\n');
        if (pos) {
            *pos = '\0';
        }
        
        // if (line[0] == ' ') {
        //     break;
        // }
        // Serial.println(line[0] + "*********");
        fileInfo += line;
        fileInfo += "&";
    }
    fclose(f);
    //Serial.println(fileInfo);
    return fileInfo;
}