/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2022, 2025 NXP
 * All rights reserved.
 *
 * Practica 2 (Equipo 1): toggle bidireccional de LEDs entre dos FRDM-RW612
 * por MQTT. Cada tarjeta publica su boton y su LED, y se suscribe al boton
 * de la otra tarjeta.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*******************************************************************************
 * Includes
 ******************************************************************************/
#include "mqtt_freertos.h"
#include "practica2_config.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "fsl_gpio.h"
#include "fsl_io_mux.h"

#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/apps/mqtt.h"
#include "lwip/tcpip.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*! @brief Stack size of the application thread. */
#define APP_THREAD_STACKSIZE 1024

/*! @brief Priority of the application thread. */
#define APP_THREAD_PRIO DEFAULT_THREAD_PRIO

/*! @brief Stack size of the button thread. */
#define BUTTON_THREAD_STACKSIZE 512

/*! @brief Priority of the button thread. */
#define BUTTON_THREAD_PRIO DEFAULT_THREAD_PRIO

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

static void connect_to_mqtt(void *ctx);

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*! @brief MQTT client data. */
static mqtt_client_t *mqtt_client;

/*! @brief MQTT client information. */
static const struct mqtt_connect_client_info_t mqtt_client_info = {
    .client_id   = MQTT_CLIENT_ID,
    .client_user = NULL,
    .client_pass = NULL,
    .keep_alive  = 100,
    .will_topic  = NULL,
    .will_msg    = NULL,
    .will_qos    = 0,
    .will_retain = 0,
#if LWIP_ALTCP && LWIP_ALTCP_TLS
    .tls_config = NULL,
#endif
};

/*! @brief MQTT broker IP address. */
static ip_addr_t mqtt_addr;

/*! @brief Indicates connection to MQTT broker. */
static volatile bool connected = false;

/*! @brief The incoming message comes from the peer's button topic. Only used on tcpip_thread. */
static bool s_from_peer = false;

/*! @brief Logical LED state. Only used on tcpip_thread. */
static bool s_led_on = false;

/*******************************************************************************
 * Code
 ******************************************************************************/

/*!
 * @brief Drives the LED pin.
 */
static void led_set(bool on)
{
    GPIO_PinWrite(APP_LED_GPIO, APP_LED_PORT, APP_LED_PIN, on ? APP_LED_ON_LEVEL : (1U - APP_LED_ON_LEVEL));
}

/*!
 * @brief Configures the LED and button pins.
 */
static void app_gpio_init(void)
{
    gpio_pin_config_t led_config = {kGPIO_DigitalOutput, 1U - APP_LED_ON_LEVEL};
    gpio_pin_config_t sw_config  = {kGPIO_DigitalInput, 0U};

    IO_MUX_SetPinMux(IO_MUX_GPIO0);
    IO_MUX_SetPinMux(IO_MUX_GPIO11);

    /* Needed for the internal pull-up of the button (same as gpio input_interrupt example). */
    BOARD_ApplyGpioPullUpWorkaround();

    GPIO_PortInit(APP_LED_GPIO, APP_LED_PORT);
    GPIO_PinInit(APP_LED_GPIO, APP_LED_PORT, APP_LED_PIN, &led_config);
    GPIO_PinInit(APP_SW_GPIO, APP_SW_PORT, APP_SW_PIN, &sw_config);
}

/*!
 * @brief Publishes the LED state (retained). To be called on tcpip_thread.
 */
static void publish_led_state(void)
{
    const char *payload = s_led_on ? "ON" : "OFF";
    err_t err;

    err = mqtt_publish(mqtt_client, TOPIC_MY_LED, payload, strlen(payload), 1, 1, NULL, NULL);
    if (err == ERR_OK)
    {
        PRINTF("TX %s: %s\r\n", TOPIC_MY_LED, payload);
    }
    else
    {
        PRINTF("Failed to publish to the topic \"%s\": %d.\r\n", TOPIC_MY_LED, err);
    }
}

/*!
 * @brief Called when subscription request finishes.
 */
static void mqtt_topic_subscribed_cb(void *arg, err_t err)
{
    const char *topic = (const char *)arg;

    if (err == ERR_OK)
    {
        PRINTF("Subscribed to the topic \"%s\".\r\n", topic);
    }
    else
    {
        PRINTF("Failed to subscribe to the topic \"%s\": %d.\r\n", topic, err);
    }
}

/*!
 * @brief Called when there is a message on a subscribed topic.
 */
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    LWIP_UNUSED_ARG(arg);

    s_from_peer = (strcmp(topic, TOPIC_PEER_BTN) == 0);

    PRINTF("RX %s (%u bytes): \"", topic, tot_len);
}

/*!
 * @brief Called when recieved incoming published message fragment.
 */
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    int i;

    LWIP_UNUSED_ARG(arg);

    for (i = 0; i < len; i++)
    {
        if (isprint(data[i]))
        {
            PRINTF("%c", (char)data[i]);
        }
        else
        {
            PRINTF("\\x%02x", data[i]);
        }
    }

    if (flags & MQTT_DATA_FLAG_LAST)
    {
        PRINTF("\"\r\n");

        /* Any message from the peer's button toggles our LED */
        if (s_from_peer)
        {
            s_led_on = !s_led_on;
            led_set(s_led_on);
            publish_led_state();
        }
    }
}

/*!
 * @brief Subscribe to MQTT topics.
 */
static void mqtt_subscribe_topics(mqtt_client_t *client)
{
    static const char *topic = TOPIC_PEER_BTN;
    err_t err;

    mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb,
                            LWIP_CONST_CAST(void *, &mqtt_client_info));

    err = mqtt_subscribe(client, topic, 1, mqtt_topic_subscribed_cb, LWIP_CONST_CAST(void *, topic));

    if (err == ERR_OK)
    {
        PRINTF("Subscribing to the topic \"%s\" with QoS 1...\r\n", topic);
    }
    else
    {
        PRINTF("Failed to subscribe to the topic \"%s\" with QoS 1: %d.\r\n", topic, err);
    }
}

/*!
 * @brief Called when connection state changes.
 */
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    const struct mqtt_connect_client_info_t *client_info = (const struct mqtt_connect_client_info_t *)arg;

    connected = (status == MQTT_CONNECT_ACCEPTED);

    switch (status)
    {
        case MQTT_CONNECT_ACCEPTED:
            PRINTF("MQTT client \"%s\" connected.\r\n", client_info->client_id);
            mqtt_subscribe_topics(client);
            /* Retained, so the dashboard shows the real LED state right away */
            publish_led_state();
            break;

        case MQTT_CONNECT_DISCONNECTED:
            PRINTF("MQTT client \"%s\" not connected.\r\n", client_info->client_id);
            /* Try to reconnect 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_TIMEOUT:
            PRINTF("MQTT client \"%s\" connection timeout.\r\n", client_info->client_id);
            /* Try again 1 second later */
            sys_timeout(1000, connect_to_mqtt, NULL);
            break;

        case MQTT_CONNECT_REFUSED_PROTOCOL_VERSION:
        case MQTT_CONNECT_REFUSED_IDENTIFIER:
        case MQTT_CONNECT_REFUSED_SERVER:
        case MQTT_CONNECT_REFUSED_USERNAME_PASS:
        case MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_:
            PRINTF("MQTT client \"%s\" connection refused: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;

        default:
            PRINTF("MQTT client \"%s\" connection status: %d.\r\n", client_info->client_id, (int)status);
            /* Try again 10 seconds later */
            sys_timeout(10000, connect_to_mqtt, NULL);
            break;
    }
}

/*!
 * @brief Starts connecting to MQTT broker. To be called on tcpip_thread.
 */
static void connect_to_mqtt(void *ctx)
{
    LWIP_UNUSED_ARG(ctx);

    PRINTF("Connecting to MQTT broker at %s...\r\n", ipaddr_ntoa(&mqtt_addr));

    mqtt_client_connect(mqtt_client, &mqtt_addr, EXAMPLE_MQTT_SERVER_PORT, mqtt_connection_cb,
                        LWIP_CONST_CAST(void *, &mqtt_client_info), &mqtt_client_info);
}

/*!
 * @brief Publishes the button state (not retained). To be called on tcpip_thread.
 *
 * @param ctx button state, passed by value (0 = OFF, 1 = ON)
 */
static void publish_button(void *ctx)
{
    const char *payload = ((uintptr_t)ctx != 0U) ? "ON" : "OFF";
    err_t err;

    if (!mqtt_client_is_connected(mqtt_client))
    {
        PRINTF("Button pressed, but MQTT is not connected.\r\n");
        return;
    }

    err = mqtt_publish(mqtt_client, TOPIC_MY_BTN, payload, strlen(payload), 1, 0, NULL, NULL);
    if (err == ERR_OK)
    {
        PRINTF("TX %s: %s\r\n", TOPIC_MY_BTN, payload);
    }
    else
    {
        PRINTF("Failed to publish to the topic \"%s\": %d.\r\n", TOPIC_MY_BTN, err);
    }
}

/*!
 * @brief Polls the button with debounce and publishes on every press.
 */
static void button_thread(void *arg)
{
    uint32_t stable = 1U; /* pull-up: 1 = released */
    uint32_t last   = 1U;
    uint32_t count  = 0U;
    bool btn_state  = false;
    err_t err;

    LWIP_UNUSED_ARG(arg);

    for (;;)
    {
        uint32_t now = GPIO_PinRead(APP_SW_GPIO, APP_SW_PORT, APP_SW_PIN);

        if (now != last)
        {
            last  = now;
            count = 0U;
        }
        else if (count < APP_SW_STABLE_SAMPLES)
        {
            count++;
        }

        if ((count == APP_SW_STABLE_SAMPLES) && (now != stable))
        {
            stable = now;
            if (stable == 0U) /* falling edge = press */
            {
                btn_state = !btn_state;
                /* lwIP is not thread-safe: publish from tcpip_thread */
                err = tcpip_callback(publish_button, (void *)(uintptr_t)btn_state);
                if (err != ERR_OK)
                {
                    PRINTF("Failed to invoke publishing of the button on the tcpip_thread: %d.\r\n", err);
                }
            }
        }

        sys_msleep(APP_SW_SAMPLE_MS);
    }
}

/*!
 * @brief Application thread.
 */
static void app_thread(void *arg)
{
    struct netif *netif = (struct netif *)arg;
    err_t err;

    PRINTF("\r\nIPv4 Address     : %s\r\n", ipaddr_ntoa(&netif->ip_addr));
    PRINTF("IPv4 Subnet mask : %s\r\n", ipaddr_ntoa(&netif->netmask));
    PRINTF("IPv4 Gateway     : %s\r\n\r\n", ipaddr_ntoa(&netif->gw));

    PRINTF("Board role       : %s (client ID \"%s\")\r\n", MY_ID, MQTT_CLIENT_ID);
    PRINTF("Publishes        : %s, %s\r\n", TOPIC_MY_BTN, TOPIC_MY_LED);
    PRINTF("Subscribes       : %s\r\n\r\n", TOPIC_PEER_BTN);

    /*
     * Check if we have an IP address or host name string configured.
     * Could just call netconn_gethostbyname() on both IP address or host name,
     * but we want to print some info if goint to resolve it.
     */
    if (ipaddr_aton(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr) && IP_IS_V4(&mqtt_addr))
    {
        /* Already an IP address */
        err = ERR_OK;
    }
    else
    {
        /* Resolve MQTT broker's host name to an IP address */
        PRINTF("Resolving \"%s\"...\r\n", EXAMPLE_MQTT_SERVER_HOST);
        err = netconn_gethostbyname(EXAMPLE_MQTT_SERVER_HOST, &mqtt_addr);
    }

    if (err == ERR_OK)
    {
        /* Start connecting to MQTT broker from tcpip_thread */
        err = tcpip_callback(connect_to_mqtt, NULL);
        if (err != ERR_OK)
        {
            PRINTF("Failed to invoke broker connection on the tcpip_thread: %d.\r\n", err);
        }
    }
    else
    {
        PRINTF("Failed to obtain IP address: %d.\r\n", err);
        vTaskDelete(NULL);
    }

    /* Start reading the button once the client is connected */
    while (!connected)
    {
        sys_msleep(100U);
    }

    if (sys_thread_new("button_task", button_thread, NULL, BUTTON_THREAD_STACKSIZE, BUTTON_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("app_thread(): Button task creation failed.", 0);
    }

    vTaskDelete(NULL);
}

/*!
 * @brief Create and run example thread
 *
 * @param netif  netif which example should use
 */
void mqtt_freertos_run_thread(struct netif *netif)
{
    app_gpio_init();
    led_set(s_led_on);

    LOCK_TCPIP_CORE();
    mqtt_client = mqtt_client_new();
    UNLOCK_TCPIP_CORE();
    if (mqtt_client == NULL)
    {
        PRINTF("mqtt_client_new() failed.\r\n");
        while (1)
        {
        }
    }

    if (sys_thread_new("app_task", app_thread, netif, APP_THREAD_STACKSIZE, APP_THREAD_PRIO) == NULL)
    {
        LWIP_ASSERT("mqtt_freertos_start_thread(): Task creation failed.", 0);
    }
}
