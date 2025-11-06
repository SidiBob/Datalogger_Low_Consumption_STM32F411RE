#ifndef INC_UART_LOGGER_H_
#define INC_UART_LOGGER_H_

#include "stm32f4xx_hal.h" // Adaptez ceci à votre famille de MCU (ex: stm32f4xx_hal.h)
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ==================================================================== */
/* ========================== CONFIGURATION =========================== */

// --- La taille du buffer circulaire (en octets)
// --- DOIT être une puissance de 2 (ex: 64, 128, 256, 512, 1024)
#define UART_LOGGER_BUFFER_SIZE 512

// --- Le handle UART à utiliser (défini dans main.c)
extern UART_HandleTypeDef huart2; // Adaptez ceci (ex: huart2)

/* ==================================================================== */
/* ==================================================================== */


/**
 * @brief Initialise le module de logging UART.
 * @param huart Pointeur vers l'handle UART (ex: &huart1)
 */
void uart_log_init(UART_HandleTypeDef* huart);

/**
 * @brief Fonction de log formaté (type printf), non-bloquante.
 * Écrit les données dans le buffer circulaire. Le DMA les enverra en arrière-plan.
 */
void my_printf(const char* format, ...);

/**
 * @brief Tâche de traitement du log (optionnelle).
 * Peut être appelée dans la boucle principale pour garantir que la transmission
 * démarre si elle a été interrompue.
 */
void uart_log_process(void);

/**
 * @brief Fonction interne appelée par l'interruption de fin de transmission DMA.
 * NE PAS APPELER MANUELLEMENT.
 * @param huart Pointeur vers l'handle UART
 */
void uart_log_dma_tx_complete_callback(UART_HandleTypeDef *huart);

#endif /* INC_UART_LOGGER_H_ */
