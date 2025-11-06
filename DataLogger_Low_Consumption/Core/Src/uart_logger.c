#include "uart_logger.h"

// --- Handle UART (défini dans main.c, référencé dans l'en-tête)
extern UART_HandleTypeDef huart2;

/* --- Variables Statiques (privées au module) --- */

// Le buffer circulaire et ses pointeurs
static uint8_t g_log_buffer[UART_LOGGER_BUFFER_SIZE];

// Pointeur 'head': où my_printf écrit les données
volatile uint16_t g_head = 0;
// Pointeur 'tail': où le DMA lit les données
volatile uint16_t g_tail = 0;

// Flag pour savoir si le DMA est en cours de transmission
volatile uint8_t g_dma_in_progress = 0;

// Buffer temporaire pour formater les strings (avant de les copier)
#define TEMP_FORMAT_BUFFER_SIZE 128

// Masque pour le calcul rapide du "wrap-around" (BUFFER_SIZE - 1)
static const uint16_t g_buffer_mask = UART_LOGGER_BUFFER_SIZE - 1;


/* --- Fonctions Privées --- */

/**
 * @brief Tente de démarrer une nouvelle transmission DMA.
 * Vérifie si le DMA est libre et s'il y a des données à envoyer.
 */
static void uart_log_check_and_transmit(void)
{
    // Section critique : on vérifie les pointeurs et le flag DMA
    __disable_irq();

    // Si le DMA est déjà en train d'envoyer, ou si le buffer est vide, on sort.
    if (g_dma_in_progress || (g_head == g_tail))
    {
        __enable_irq();
        return;
    }

    // Il y a des données à envoyer et le DMA est libre.
    // 1. Marquer le DMA comme "occupé"
    g_dma_in_progress = 1;

    // 2. Calculer la taille du bloc à envoyer
    uint16_t bytes_to_send;
    if (g_head > g_tail)
    {
        // Cas simple: [ ... tail ... head ... ]
        bytes_to_send = g_head - g_tail;
    }
    else
    {
        // Cas "wrap-around": [ ... head ... tail ... ]
        // On n'envoie que la partie de 'tail' jusqu'à la fin du buffer
        bytes_to_send = UART_LOGGER_BUFFER_SIZE - g_tail;
    }

    // Fin de la section critique
    __enable_irq();

    // 3. Lancer la transmission DMA (non-bloquante)
    HAL_UART_Transmit_DMA(&huart2, &g_log_buffer[g_tail], bytes_to_send);
}

/* --- Fonctions Publiques (API) --- */

void uart_log_init(UART_HandleTypeDef* huart)
{
    g_head = 0;
    g_tail = 0;
    g_dma_in_progress = 0;
}

void my_printf(const char* format, ...)
{
    char temp_buffer[TEMP_FORMAT_BUFFER_SIZE];
    int formatted_len;

    // 1. Formater le message (identique à un printf standard)
    va_list args;
    va_start(args, format);
    formatted_len = vsnprintf(temp_buffer, TEMP_FORMAT_BUFFER_SIZE, format, args);
    va_end(args);

    if (formatted_len <= 0) {
        return; // Erreur ou message vide
    }

    // Si le message est plus grand que le buffer, on le tronque
    if (formatted_len >= TEMP_FORMAT_BUFFER_SIZE) {
        formatted_len = TEMP_FORMAT_BUFFER_SIZE - 1;
    }

    // --- DÉBUT DE LA SECTION CRITIQUE ---
    // On désactive les IRQ AVANT de lire g_head et g_tail
    __disable_irq();

    // 2. Vérifier si on a assez de place (LA BONNE MÉTHODE)
    uint16_t current_size = (g_head - g_tail) & g_buffer_mask;
    uint16_t available_space = (UART_LOGGER_BUFFER_SIZE - 1) - current_size;

    if (formatted_len > available_space)
    {
        // Buffer plein ! Le message est perdu.
        __enable_irq(); // Ne pas oublier de réactiver les IRQ
        return;
    }

    // 3. Copier les données formatées dans le buffer circulaire
    // Nous sommes toujours dans la section critique, g_tail ne peut pas bouger.
    uint16_t new_head = g_head;
    for (int i = 0; i < formatted_len; i++)
    {
        uint16_t index = (new_head + i) & g_buffer_mask;
        g_log_buffer[index] = temp_buffer[i];
    }

    // Mettre à jour g_head (atomiquement)
    g_head = (new_head + formatted_len) & g_buffer_mask;

    // --- FIN DE LA SECTION CRITIQUE ---
    __enable_irq();

    // 4. "Réveiller" le transmetteur DMA (en dehors de la section critique)
    uart_log_check_and_transmit();
}

void uart_log_process(void)
{
    // Fonction utilitaire à appeler dans la boucle principale (while(1))
    // pour s'assurer que la transmission démarre.
    uart_log_check_and_transmit();
}

/* --- Callback d'Interruption --- */

/**
 * @brief Callback appelé par la HAL lorsque la transmission DMA est terminée.
 * C'est le moteur du système.
 */
void uart_log_dma_tx_complete_callback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != huart2.Instance)
    {
        return; // Ne devrait pas arriver si un seul UART est utilisé pour le log
    }

    // 1. Mettre à jour le pointeur 'tail'
    // Le nombre d'octets envoyés est dans la structure HAL
    g_tail = (g_tail + huart->TxXferSize) & g_buffer_mask;

    // 2. Marquer le DMA comme "libre"
    g_dma_in_progress = 0;

    // 3. Relancer immédiatement un transfert s'il reste des données
    // (C'est ce qui gère le "wrap-around" ou les messages en file d'attente)
    uart_log_check_and_transmit();
}
