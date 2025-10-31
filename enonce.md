# TP: Conception d'un Enregistreur de Données (Data Logger) Basse Consommation sur STM32

## 1. Objectif

L'objectif de ce TP est de développer un enregistreur de données autonome sur une carte Nucleo STM32F411RE. L'appareil doit se réveiller périodiquement (ex: toutes les 5 minutes), collecter des données de capteurs (température, humidité, lumière), les horodater, les enregistrer sur une carte SD, puis retourner dans un mode de très basse consommation pour maximiser la durée de vie de la batterie.

Ce projet vous fera maîtriser la pile HAL, la gestion de l'énergie, les protocoles de communication série et l'intégration d'un middleware (système de fichiers).

## 2. Matériel Requis

* STM32F411RE Nucleo
* Capteur DHT11 (température, humidité)
* Photorésistance 10 kOhm
* Module RTC DS1307 (I2C)
* Adaptateur carte SD (SPI)
* Une carte SD (formatée en FAT32)
* Fils de prototypage et breadboard

---

## 3. Étapes et Concepts à Maîtriser

### Étape 1 : Initialisation du Projet (STM32CubeMX)

Avant d'écrire une ligne de code, la configuration est cruciale.

* **Périphériques à activer :**
    * **I2C :** Pour le RTC externe (DS1307).
    * **SPI :** Pour la carte SD.
    * **ADC :** Pour la photorésistance.
    * **RTC :** Le RTC *interne* du STM32.
    * **GPIO :** Une broche en sortie pour alimenter le DHT11 (Power Gating) et une autre pour la ligne de données.
* **Concepts Clés :**
    * **Middleware FatFs :** Activez `FATFS` dans l'onglet "Middleware" et liez-le au périphérique `SPI` que vous avez choisi.
    * **Horloge LSE :** Pour que le RTC interne (et donc le réveil) fonctionne en mode Stop, vous devez activer l'oscillateur basse vitesse externe (`LSE` - Low Speed External) dans l'onglet `RCC`. C'est l'horloge 32.768 kHz.
    * **Horloge RTC :** Assurez-vous que la source d'horloge du `RTC` (dans l'onglet "Peripherals") est bien réglée sur le `LSE`.

### Étape 2 : Le Concept du "Double RTC"

Ce projet utilise deux horloges temps réel. Il est vital de comprendre leur rôle distinct.

* **RTC Externe (DS1307) :**
    * **Rôle :** "Horloge murale" (Wall Clock). Il conserve l'heure et la date exactes (ex: "31 octobre 2025, 10:30:00") grâce à sa propre pile.
    * **Protocole :** I2C (Chap. 14).
    * **Recherches à faire :**
        * Comment lire/écrire dans les registres d'un esclave I2C ? (Recherchez `HAL_I2C_Master_Transmit` et `HAL_I2C_Master_Receive`).
        * Consultez la datasheet du **DS1307**. Quel est le format des données ? (Indice : **BCD** - Binary-Coded Decimal). Vous aurez besoin de fonctions de conversion `bcd_to_dec` et `dec_to_bcd`.

* **RTC Interne (STM32) :**
    * **Rôle :** "Réveil" (Wake-Up Timer). Son unique but est de sortir le CPU du mode basse consommation.
    * **Protocole :** Périphérique interne (Chap. 18).
    * **Recherches à faire :**
        * Comment configurer le "Periodic Wakeup Unit" ? (Recherchez `HAL_RTCEx_SetWakeUpTimer_IT`).
        * Quelle est la source d'horloge du WakeUp Timer (RTCCLK) et comment calculer le compteur pour obtenir un délai de 5 minutes ?

### Étape 3 : Lecture des Capteurs

* **Photorésistance (Chap. 12 - ADC) :**
    * **Concept :** La photorésistance doit être montée en **pont diviseur de tension** avec une résistance fixe (ex: 10 kOhm) pour que l'ADC puisse lire une tension variable.
    * **Recherches à faire :**
        * Comment lire une seule valeur ADC en mode simple (polling) ? (Recherchez `HAL_ADC_Start`, `HAL_ADC_PollForConversion`, `HAL_ADC_GetValue`).

* **DHT11 (Chap. 6 - GPIO & Chap. 11 - Timers) :**
    * **Concept :** C'est la partie la plus complexe. Le DHT11 n'utilise *pas* un protocole standard (I2C/SPI). Il utilise un protocole 1-wire "maison" basé sur des **timings très précis** (de l'ordre de la microseconde).
    * **Recherches à faire :**
        * Consultez la "datasheet du DHT11" pour comprendre le diagramme de temps (start pulse, response pulse, encodage des bits '0' et '1').
        * `HAL_Delay()` est en millisecondes, ce qui est trop lent. Comment créer une fonction `delay_us(uint32_t us)` précise ? (Indice : **Chap 11.5** - Utiliser un timer TIM de base ou, mieux, le compteur de cycles **DWT** du cœur Cortex-M4. Recherchez "Cortex-M DWT CYCCNT").
        * Comment changer la configuration d'une broche à la volée ? (Recherchez `GPIO_InitTypeDef`). Vous devrez passer la broche de `GPIO_MODE_OUTPUT_PP` (pour envoyer le start pulse) à `GPIO_MODE_INPUT` (pour lire la réponse).

### Étape 4 : Gestion de la Carte SD (Chap. 15 - SPI & FatFs)

* **Concept :** Vous n'allez pas piloter le SPI manuellement. Vous utiliserez la couche d'abstraction **FatFs** (File System) fournie par CubeMX. Votre travail consiste à utiliser les *fonctions FatFs* (ex: `f_open`, `f_puts`).
* **Recherches à faire :**
    * Comment "monter" (initialiser) le système de fichiers ? (Recherchez `f_mount`).
    * Comment ouvrir un fichier texte en mode "ajout" (append) pour ne pas effacer les données précédentes ? (Recherchez `f_open` avec le flag `FA_OPEN_APPEND | FA_WRITE`).
    * Comment formater vos données (température, humidité, etc.) en une chaîne de caractères CSV ? (Recherchez la fonction C standard `sprintf`).
    * **Point Critique :** Comment s'assurer que les données sont physiquement écrites sur la carte *avant* de s'endormir ? (Recherchez `f_close` ou `f_sync`). Ne pas le faire entraînera une perte de données.

### Étape 5 : Gestion de l'Énergie (Cœur du TP) (Chap. 19 - Power)

* **Concept :** Le but est d'arrêter le CPU et la plupart des périphériques, tout en laissant le RTC interne actif. Le mode idéal pour cela est le **Mode STOP**.
* **Recherches à faire :**
    * Quelles sont les différences entre les modes Sleep, Stop et Standby ? (Consultez **Chap 19.3.2**).
    * Comment entrer en mode Stop ? (Recherchez `HAL_PWR_EnterSTOPMode`). L'instruction `WFI` (Wait For Interrupt) sera utilisée.
    * **Point Critique (Le "Gotcha") :** Lorsque le STM32 se réveille du mode STOP (via l'interruption du RTC interne), il redémarre sur une horloge interne (HSI ou MSI), *pas* sur son horloge haute vitesse (PLL).
    * Que devez-vous faire **immédiatement** après le réveil ? (Indice : Vous devez rappeler la fonction `SystemClock_Config()` pour reconfigurer le PLL avant de pouvoir utiliser l'USB, l'I2C ou le SPI à pleine vitesse).

### Étape 6 : L'Intégration (La `main.c`)

Votre boucle `while(1)` dans le `main` doit ressembler à ceci (en pseudo-code) :

1.  **Phase d'Acquisition :**
    * Allumer le VCC du DHT11 (GPIO `HIGH`).
    * Attendre sa stabilisation (ex: `HAL_Delay(2000)`).
    * Lire le DS1307 (I2C) pour obtenir l'horodatage.
    * Lire le DHT11 (Bit-banging GPIO).
    * Éteindre le VCC du DHT11 (GPIO `LOW`) pour économiser l'énergie.
    * Lire la photorésistance (ADC).

2.  **Phase de Stockage :**
    * Formater les données en une chaîne CSV (ex: `"2025-10-31,10:35:00,22.5,45.2,876\r\n"`).
    * `f_mount` la carte SD.
    * `f_open` le fichier "LOG.TXT".
    * `f_puts` la chaîne de données.
    * `f_close` le fichier (TRÈS IMPORTANT).
    * `f_mount(NULL, ...)` pour démonter le volume.

3.  **Phase de Sommeil :**
    * Configurer le réveil RTC interne pour "dans 5 minutes" (`HAL_RTCEx_SetWakeUpTimer_IT`).
    * Désactiver les horloges des périphériques (optimisation).
    * Entrer en mode STOP (`HAL_PWR_EnterSTOPMode`).

4.  **(Le CPU est en pause ici)**

5.  **(Réveil par l'IRQ du RTC)**

6.  **Phase Post-Réveil :**
    * `SystemClock_Config()` (TRÈS IMPORTANT).
    * *La boucle recommence à l'étape 1.*

---

## 4. Pour Aller Plus Loin (Bonus)

* **Chap. 17 - Watchdog :** Intégrez un **IWDG** (Independent Watchdog). Si votre code se bloque (ex: lors de la lecture du DHT11), le watchdog redémarrera le MCU.
* **Chap. 19.8 :** Assurez-vous que l'IWDG est compatible avec le mode Stop (l'IWDG est-il gelé ou continue-t-il de compter ?).
* **Chap. 9 - DMA :** Modifiez la lecture de l'ADC pour utiliser le DMA afin de faire plusieurs mesures et une moyenne, rendant la lecture de la lumière plus stable.
