/*
 * Practica 2 - MQTT con FRDM-RW612 (Equipo 1)
 *
 * Configuracion de la aplicacion. El mismo firmware se compila para la
 * Tarjeta A y la Tarjeta B; solo cambia BOARD_ROLE_A.
 */
#ifndef PRACTICA2_CONFIG_H_
#define PRACTICA2_CONFIG_H_

#include "board.h"

/*******************************************************************************
 * Rol de la tarjeta y broker
 ******************************************************************************/
#ifndef BOARD_ROLE_A
#define BOARD_ROLE_A 1 /* 1 = Tarjeta A, 0 = Tarjeta B */
#endif

#ifndef USE_LOCAL_BROKER
#define USE_LOCAL_BROKER 0 /* 0 = nube, 1 = LAN aislada */
#endif

#define TEAM "equipo1"

#if BOARD_ROLE_A
#define MY_ID   "tarjeta_a"
#define PEER_ID "tarjeta_b"
#else
#define MY_ID   "tarjeta_b"
#define PEER_ID "tarjeta_a"
#endif

#define TOPIC_MY_BTN   TEAM "/" MY_ID "/boton"
#define TOPIC_MY_LED   TEAM "/" MY_ID "/led"
#define TOPIC_PEER_BTN TEAM "/" PEER_ID "/boton"
#define MQTT_CLIENT_ID TEAM "_" MY_ID /* unico por tarjeta */

#if USE_LOCAL_BROKER
#define EXAMPLE_MQTT_SERVER_HOST "192.168.0.7" /* IP de la tablet (cambia segun la red) */
#else
#define EXAMPLE_MQTT_SERVER_HOST "broker.hivemq.com"
#endif

#define EXAMPLE_MQTT_SERVER_PORT 1883

/*******************************************************************************
 * Pines (board.h de FRDM-RW612)
 ******************************************************************************/
/* LED azul del LED RGB: GPIO0_0 */
#define APP_LED_GPIO GPIO
#define APP_LED_PORT BOARD_LED_BLUE_GPIO_PORT
#define APP_LED_PIN  BOARD_LED_BLUE_GPIO_PIN

/* Nivel logico que enciende el LED. board.h define LED_BLUE_ON() como nivel alto;
 * si en la tarjeta el LED se ve invertido, cambiar a 0U. */
#ifndef APP_LED_ON_LEVEL
#define APP_LED_ON_LEVEL 1U
#endif

/* Boton SW2: GPIO0_11, activo en bajo (pull-up) */
#define APP_SW_GPIO BOARD_SW2_GPIO
#define APP_SW_PORT BOARD_SW2_GPIO_PORT
#define APP_SW_PIN  BOARD_SW2_GPIO_PIN

/* Anti-rebote: muestreo cada 10 ms, 3 muestras iguales = 30 ms estable */
#define APP_SW_SAMPLE_MS      10U
#define APP_SW_STABLE_SAMPLES 3U

#endif /* PRACTICA2_CONFIG_H_ */
