#include <stdio.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "esp_netif.h"
#include "udp_terminal.h"

struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
int sock;
socklen_t socklen;

void udp_server_task(void *arg)
{
    char rx_buffer[128];
    char addr_str[128];
    int rx_len;
    int addr_family;
    int ip_protocol;

    uint16_t ip_port = 502;

    while (1)
    {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(ip_port);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0)
        {
            printf("Unable to create socket: errno %d\r\n", errno);
            break;
        }
        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0)
        {
            printf("Socket unable to bind: errno %d\r\n", errno);
        }
        printf("Socket bound, port %d\r\n", ip_port);
        socklen = sizeof(source_addr);

        while (1)
        {
            printf("Waiting for data\r\n");
            rx_len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (rx_len < 0)
            {
                printf("recvfrom failed: errno %d\r\n", errno);
                close(sock);
                break;
            }
            else // Data received
            {
                // Get the sender's ip address as string
                if (source_addr.sin6_family == PF_INET)
                {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                }
                else if (source_addr.sin6_family == PF_INET6)
                {
                    inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }
                printf("Received %d bytes from %s\r\n", rx_len, addr_str);
                // com_trans_txuart(udp->rx_buffer, udp->rx_len);
            }
        }

        if (sock != -1)
        {
            printf("Shutting down socket\r\n");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t udp_server_send(uint8_t *src, int len)
{
    // printf("%s", src);
    if (sock != -1)
        return sendto(sock, src, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
    else
        return ESP_FAIL;
}

esp_err_t udp_terminal_init(void)
{
    esp_err_t ret = ESP_OK;

    xTaskCreate(udp_server_task, "udp_server", 2 * 1024, NULL, 5, NULL);

    return ret;
}
