# FERP receipt printer — USB model

See [../ferp-printer-com-v1/PRODUCT_SPEC.md](../ferp-printer-com-v1/PRODUCT_SPEC.md) — both printer products share one specification.
This product differs only in `app/app_hw_config.h` (USB host transport, OTA target `printer-usb-main`), `app/version.h` (45.x) and the ESP32-S3 IDF project (`espressif/usb` component).
