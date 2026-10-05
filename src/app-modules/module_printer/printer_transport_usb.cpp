// printer_transport_usb.cpp — see printer_transport_usb.h

#include "printer_transport_usb.h"
#include "pal_logger.h"

#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define __TAG__   "PRN_USB "
#define PTUSB_LOG true

#define USB_CLASS_PRINTER_CODE   0x07
#define DAEMON_TASK_PRIO         6
#define CLIENT_TASK_PRIO         6

// ── Lifecycle ───────────────────────────────────────────────────────────────

bool PrinterTransportUsb::begin(uint32_t /*baud*/)
{
    if (_started) return true;
    _done      = xSemaphoreCreateBinary();
    _lock      = xSemaphoreCreateMutex();
    _installed = xSemaphoreCreateBinary();
    if (!_done || !_lock || !_installed) {
        LOG_MSG_ERROR(PTUSB_LOG, "semaphore alloc failed");
        return false;
    }
    if (xTaskCreatePinnedToCore(_daemon_task, "usb_daemon", 4096, this, DAEMON_TASK_PRIO, nullptr, 0) != pdPASS ||
        xTaskCreatePinnedToCore(_client_task, "usb_client", 4096, this, CLIENT_TASK_PRIO, nullptr, 0) != pdPASS) {
        LOG_MSG_ERROR(PTUSB_LOG, "task create failed");
        return false;
    }
    _started = true;
    LOG_MSG_INFO(PTUSB_LOG, "USB host started — waiting for printer");
    return true;
}

void PrinterTransportUsb::_daemon_task(void *arg)
{
    auto *self = static_cast<PrinterTransportUsb *>(arg);
    usb_host_config_t host_cfg = {};
    host_cfg.skip_phy_setup = false;
    host_cfg.intr_flags     = ESP_INTR_FLAG_LEVEL1;
    esp_err_t err = usb_host_install(&host_cfg);
    if (err != ESP_OK) {
        LOG_MSG_ERROR(PTUSB_LOG, "usb_host_install failed: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    xSemaphoreGive(self->_installed);

    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        // The single client stays registered for the device's lifetime, so
        // NO_CLIENTS / ALL_FREE are informational only.
    }
}

void PrinterTransportUsb::_client_task(void *arg)
{
    auto *self = static_cast<PrinterTransportUsb *>(arg);
    xSemaphoreTake(self->_installed, portMAX_DELAY);

    usb_host_client_config_t cc = {};
    cc.is_synchronous    = false;
    cc.max_num_event_msg = 5;
    cc.async.client_event_callback = _client_event_cb;
    cc.async.callback_arg          = self;
    if (usb_host_client_register(&cc, &self->_client) != ESP_OK) {
        LOG_MSG_ERROR(PTUSB_LOG, "client register failed");
        vTaskDelete(nullptr);
        return;
    }
    if (usb_host_transfer_alloc(XFER_SIZE, 0, &self->_xfer) != ESP_OK) {
        LOG_MSG_ERROR(PTUSB_LOG, "transfer alloc failed");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        usb_host_client_handle_events(self->_client, pdMS_TO_TICKS(100));
        if (self->_actions & ACT_OPEN) {
            self->_actions &= ~ACT_OPEN;
            self->_open_device();
        }
        if (self->_actions & ACT_CLOSE) {
            if (self->_close_device()) self->_actions &= ~ACT_CLOSE;
        }
    }
}

void PrinterTransportUsb::_client_event_cb(const usb_host_client_event_msg_t *ev, void *arg)
{
    auto *self = static_cast<PrinterTransportUsb *>(arg);
    switch (ev->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            if (self->_addr == 0) {
                self->_addr     = ev->new_dev.address;
                self->_actions |= ACT_OPEN;
            }
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            if (self->_dev) {
                self->_ready    = false;
                self->_actions  = ACT_CLOSE;
            }
            break;
        default:
            break;
    }
}

// ── Device open / close ─────────────────────────────────────────────────────

static void _utf16_to_ascii(const usb_str_desc_t *d, char *out, size_t out_len)
{
    size_t n = 0;
    if (d && out_len) {
        int chars = (d->bLength - 2) / 2;
        for (int i = 0; i < chars && n + 1 < out_len; i++) {
            uint16_t c = d->wData[i];
            out[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
    }
    if (out_len) out[n] = '\0';
}

void PrinterTransportUsb::_find_bulk_out(const usb_config_desc_t *cfg)
{
    _intf   = FALLBACK_INTF;
    _ep_out = FALLBACK_EP_OUT;
    bool found = false, found_printer = false;

    for (uint8_t i = 0; i < cfg->bNumInterfaces && !found_printer; i++) {
        int offset = 0;
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(cfg, i, 0, &offset);
        if (!intf) continue;
        for (uint8_t e = 0; e < intf->bNumEndpoints; e++) {
            int ep_off = offset;
            const usb_ep_desc_t *ep =
                usb_parse_endpoint_descriptor_by_index(intf, e, cfg->wTotalLength, &ep_off);
            if (!ep) continue;
            bool bulk = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;
            bool out  = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) == 0;
            if (!bulk || !out) continue;
            bool is_printer = intf->bInterfaceClass == USB_CLASS_PRINTER_CODE;
            if (!found || is_printer) {
                _intf   = intf->bInterfaceNumber;
                _ep_out = ep->bEndpointAddress;
                found   = true;
                found_printer = is_printer;
            }
            break;
        }
    }
    LOG_MSG_INFO(PTUSB_LOG, "endpoint: intf=%u ep=0x%02X (%s)", _intf, _ep_out,
                 found_printer ? "printer class" : (found ? "bulk OUT" : "fallback"));
}

void PrinterTransportUsb::_open_device()
{
    if (usb_host_device_open(_client, _addr, &_dev) != ESP_OK) {
        LOG_MSG_ERROR(PTUSB_LOG, "device open failed (addr %u)", _addr);
        _dev = nullptr; _addr = 0;
        return;
    }

    usb_device_info_t info = {};
    if (usb_host_device_info(_dev, &info) == ESP_OK) {
        _utf16_to_ascii(info.str_desc_product, _product, sizeof(_product));
        if (_product[0] == '\0') snprintf(_product, sizeof(_product), "USB device %u", _addr);
    }

    const usb_config_desc_t *cfg = nullptr;
    if (usb_host_get_active_config_descriptor(_dev, &cfg) == ESP_OK && cfg) {
        _find_bulk_out(cfg);
    }

    esp_err_t err = usb_host_interface_claim(_client, _dev, _intf, 0);
    if (err != ESP_OK) {
        LOG_MSG_ERROR(PTUSB_LOG, "interface %u claim failed: %s", _intf, esp_err_to_name(err));
        usb_host_device_close(_client, _dev);
        _dev = nullptr; _addr = 0;
        return;
    }
    _claimed = true;
    _ready   = true;
    LOG_MSG_INFO(PTUSB_LOG, "printer connected: '%s'", _product);
}

bool PrinterTransportUsb::_close_device()
{
    _ready = false;
    if (_in_flight) return false;          // wait for the transfer callback first
    if (xSemaphoreTake(_lock, 0) != pdTRUE) return false;
    if (_dev) {
        if (_claimed) usb_host_interface_release(_client, _dev, _intf);
        usb_host_device_close(_client, _dev);
    }
    _claimed = false;
    _dev     = nullptr;
    _addr    = 0;
    _product[0] = '\0';
    xSemaphoreGive(_lock);
    LOG_MSG_WARNING(PTUSB_LOG, "printer disconnected");
    return true;
}

// ── Data path ───────────────────────────────────────────────────────────────

void PrinterTransportUsb::_transfer_cb(usb_transfer_t *t)
{
    auto *self = static_cast<PrinterTransportUsb *>(t->context);
    self->_xfer_status = (int)t->status;
    self->_xfer_actual = (size_t)t->actual_num_bytes;
    self->_in_flight   = false;
    xSemaphoreGive(self->_done);
}

bool PrinterTransportUsb::write(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (!_ready || !_xfer || !data) return false;
    if (xSemaphoreTake(_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;

    bool ok = true;
    size_t off = 0;
    while (ok && off < len) {
        if (!_ready) { ok = false; break; }
        size_t n = len - off;
        if (n > XFER_SIZE) n = XFER_SIZE;
        memcpy(_xfer->data_buffer, data + off, n);
        _xfer->num_bytes        = (int)n;
        _xfer->device_handle    = _dev;
        _xfer->bEndpointAddress = _ep_out;
        _xfer->callback         = _transfer_cb;
        _xfer->context          = this;
        _xfer->timeout_ms       = 0;

        xSemaphoreTake(_done, 0);          // clear any stale completion
        _in_flight = true;
        if (usb_host_transfer_submit(_xfer) != ESP_OK) {
            _in_flight = false;
            LOG_MSG_ERROR(PTUSB_LOG, "transfer submit failed at %u/%u", (unsigned)off, (unsigned)len);
            ok = false;
            break;
        }
        if (xSemaphoreTake(_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            // Printer is not draining (paper out, cover open, hung) — abort
            // the transfer so the endpoint is usable again.
            LOG_MSG_ERROR(PTUSB_LOG, "transfer timeout at %u/%u — flushing endpoint", (unsigned)off, (unsigned)len);
            usb_host_endpoint_halt(_dev, _ep_out);
            usb_host_endpoint_flush(_dev, _ep_out);
            xSemaphoreTake(_done, pdMS_TO_TICKS(1000));
            usb_host_endpoint_clear(_dev, _ep_out);
            _in_flight = false;
            ok = false;
            break;
        }
        if (_xfer_status != USB_TRANSFER_STATUS_COMPLETED || _xfer_actual != n) {
            LOG_MSG_ERROR(PTUSB_LOG, "transfer status=%d actual=%u/%u",
                          _xfer_status, (unsigned)_xfer_actual, (unsigned)n);
            ok = false;
            break;
        }
        off += n;
    }
    xSemaphoreGive(_lock);
    return ok;
}

void PrinterTransportUsb::detail(char *out, size_t out_len) const
{
    if (_ready) snprintf(out, out_len, "%s", _product);
    else        snprintf(out, out_len, "no printer attached");
}
