/* Lang shim for S3 */
#pragma once
namespace Lang {
    extern const char *CODE;
    namespace Strings {
        extern const char *SERVER_NOT_FOUND, *SERVER_NOT_CONNECTED, *SERVER_ERROR, *SERVER_TIMEOUT;
        extern const char *STANDBY, *CONNECTING, *LISTENING, *SPEAKING, *VERSION;
        extern const char *LOADING_PROTOCOL, *CHECKING_NEW_VERSION, *ACTIVATION;
        extern const char *FOUND_NEW_ASSETS, *DOWNLOAD_ASSETS_FAILED, *CHECK_NEW_VERSION_FAILED;
        extern const char *OTA_UPGRADE, *UPGRADING, *NEW_VERSION, *UPGRADE_FAILED, *PLEASE_WAIT;
        extern const char *SCANNING_WIFI, *CONNECT_TO, *CONNECTED_TO;
        extern const char *REGISTERING_NETWORK, *DETECTING_MODULE;
        extern const char *ERROR, *PIN_ERROR, *REG_ERROR, *MODEM_INIT_ERROR;
        extern const char *RTC_MODE_OFF, *RTC_MODE_ON;
    }
    namespace Sounds {
        extern const char *OGG_0, *OGG_1, *OGG_2, *OGG_3, *OGG_4, *OGG_5, *OGG_6, *OGG_7, *OGG_8, *OGG_9;
        extern const char *OGG_ACTIVATION, *OGG_EXCLAMATION, *OGG_POPUP, *OGG_SUCCESS;
        extern const char *OGG_UPGRADE, *OGG_VIBRATION;
    }
}
