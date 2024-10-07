/******************************************************************************
* File Name:   radar.c
*
* Description: This file implements Gesture detection using radar sensor.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2024, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "radar.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#include "resource_map.h"

#include "xensiv_bgt60trxx_mtb.h"
#include "xensiv_radar_gestures.h"
#include "xensiv_radar_data_management.h"
#define XENSIV_BGT60TRXX_CONF_IMPL
#include "radar_settings.h"
#include "gesture_lib.h"
#include "preprocess.h"
#include "octobertech.h"

/*******************************************************************************
* Macros
********************************************************************************/
#define XENSIV_BGT60TRXX_SPI_FREQUENCY      (12000000UL)

#define NUM_SAMPLES_PER_FRAME               (XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP *\
                                            XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME *\
                                            XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS)

#define NUM_CHIRPS_PER_FRAME                XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME
#define NUM_SAMPLES_PER_CHIRP               XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP

/* RTOS tasks */
#define RADAR_TASK_NAME                     "radar_task"
#define RADAR_TASK_STACK_SIZE               (configMINIMAL_STACK_SIZE * 10)
#define RADAR_TASK_PRIORITY                 (configMAX_PRIORITIES - 1)
#define PROCESSING_TASK_NAME                "processing_task"
#define PROCESSING_TASK_STACK_SIZE          (configMINIMAL_STACK_SIZE * 10)
#define PROCESSING_TASK_PRIORITY            (configMAX_PRIORITIES - 2)

/* Interrupt priorities */
#define GPIO_INTERRUPT_PRIORITY             (6)

#define GESTURE_HOLD_TIME                   (10) /* count value used to hold gesture before evaluating new one */
#define GESTURE_DETECTION_THRESHOLD         (0)

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void radar_task(void *pvParameters);
static void processing_task(void *pvParameters);

static int32_t init_leds(void);
static int32_t radar_init(void);
static void xensiv_bgt60trxx_interrupt_handler(void* args, cyhal_gpio_event_t event);

void get_time_from_millisec_radar(unsigned long milliseconds, char* output);

/*******************************************************************************
 * Data Structure definations
 ********************************************************************************/
/*
 * @typedef typedef struct  ce_state_s
 * Structure containing gesture's result, value of verbose mode and timestamp
 */
typedef struct {
    inference_results_t gesture_result;
    bool verbose;
    uint32_t bookmark_timestamp;
}ce_state_s;

/*******************************************************************************
* Global Variables
********************************************************************************/
static cyhal_spi_t spi_obj;
static xensiv_bgt60trxx_mtb_t bgt60_obj;

static TaskHandle_t radar_task_handler;
static TaskHandle_t processing_task_handler;
radar_data_manager_s mgr;

float32_t gesture_frame[NUM_SAMPLES_PER_CHIRP * NUM_CHIRPS_PER_FRAME * XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS];

preproc_octobertech_work_arrays work_arrays;
frame_cfg f_cfg = {
        .n_channels = 3,
        .n_chirps = 32,
        .n_samples = 64,
        .n_range_bins = 32};

ce_state_s ce_app_state;
volatile bool is_settings_mode = false;
cyhal_lptimer_t lptimer_obj;
uint32_t before;
uint32_t after;

/*System tick variables*/
uint32_t tick = 0;

/*******************************************************************************
* Function Name: systick_isr
********************************************************************************
* Summary: This increments every time the SysTick counter decrements to 0.
*
* Parameters:
*   None
*
* Return:
*   None
*
*
*******************************************************************************/
void systick_isr(void)

{
    tick++;
}


/*******************************************************************************
* Function Name: read_radar_data
********************************************************************************
* Summary:
* Function that reads the data from radar hardware buffer.
* This function is supplied to software buffer manager.
*
* Parameters:
*  * data: pointer to radar data
*  *num_samples: pointer to number of samples per frame
*  samples_ub: maximum number of samples to be copied at a time from owner task/caller
*
* Return:
*  int32_t: 0 if success
*
*******************************************************************************/
int32_t read_radar_data(uint16_t* data, uint32_t *num_samples, uint32_t samples_ub)
{
    if (xensiv_bgt60trxx_get_fifo_data(&bgt60_obj.dev,
            data,
            NUM_SAMPLES_PER_FRAME) == XENSIV_BGT60TRXX_STATUS_OK)
    {
        *num_samples = NUM_SAMPLES_PER_FRAME *2; /* in bytes */

        if (samples_ub < NUM_SAMPLES_PER_FRAME *2)
        {
            xensiv_bgt60trxx_soft_reset(&bgt60_obj.dev, XENSIV_BGT60TRXX_RESET_FIFO);
        }
    }

    return 0;
}


/*******************************************************************************
* Function Name: deinterleave_antennas
********************************************************************************
* Summary:
* This function de-interleaves multiple antennas data from single radar HW FIFO
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
void deinterleave_antennas(uint16_t * buffer_ptr)
{
    uint8_t antenna = 0;
    int32_t index = 0;
    static const float norm_factor = 1.0f;

    for (int i = 0; i < (NUM_SAMPLES_PER_CHIRP * NUM_CHIRPS_PER_FRAME * XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS); ++i)
    {
        gesture_frame[index + antenna * NUM_SAMPLES_PER_CHIRP * NUM_CHIRPS_PER_FRAME] = buffer_ptr[i] * norm_factor;
        antenna++;
        if (antenna == XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS)
        {
            antenna = 0;
            index++;
        }
    }
}


/*******************************************************************************
* Function Name: radar_task
********************************************************************************
* Summary:
* This is the main task.
*    1. Creates a timer to toggle user LED
*    2. Create the processing RTOS task
*    3. Initializes the hardware interface to the sensor and LEDs
*    4. Initializes the radar device
*    5. Initializes gesture library
*    6. In an infinite loop
*       - Waits for interrupt from radar device indicating availability of data
*       - Read from software buffer the raw radar frame
*       - De-interleaves the radar data frame
*       - Acknowledges the radar data manager the consumption of read data
*       - Sends notification to processing task
* Parameters:
*  pvParameters: unused
*
* Return:
*  none
*
*******************************************************************************/
void radar_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t sz;

    uint16_t *data_buff = NULL;

    if (xTaskCreate(processing_task, PROCESSING_TASK_NAME, PROCESSING_TASK_STACK_SIZE, NULL, PROCESSING_TASK_PRIORITY, &processing_task_handler) != pdPASS)
    {
        CY_ASSERT(0);
    }

    if (radar_init() != 0)
    {
        CY_ASSERT(0);
    }

    if (init_leds () != 0)
    {
        CY_ASSERT(0);
    }

    mgr.subscribe(radar_task_handler);

    /* Initialize the initial state of ce_app_state */
    ce_app_state.gesture_result.idx = 0;
    ce_app_state.gesture_result.score = 0;
    ce_app_state.bookmark_timestamp = 0;
    ce_app_state.verbose = false;

    if (xensiv_bgt60trxx_start_frame(&bgt60_obj.dev, true) != XENSIV_BGT60TRXX_STATUS_OK)
    {
        CY_ASSERT(0);
    }

    /* Init Imagimob AI model */
    IMAI_RED_init();
    /* Init preprocessing */
    work_arrays = new_preproc_octobertech_work_arrays(&f_cfg);

    /* Inference time measurement */ 
    Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_IMO , (8000000/1000)-1);
    Cy_SysTick_SetCallback(0, systick_isr);

    for(;;)
    {
        /* Wait for the GPIO interrupt to indicate that another slice is available */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        mgr.read_from_buffer(1, &data_buff, &sz);

        deinterleave_antennas(data_buff);

        mgr.ack_data_read(1);

        /* Tell processing task to take over */
        xTaskNotifyGive(processing_task_handler);

    }
}


/*******************************************************************************
* Function Name: processing_task
********************************************************************************
* Summary:
* This is the data processing task.
*    1. It creates a console task to handle parameter configuration for the library
*    2. In a loop
*       - wait for the frame data available for process
*       - Runs the Gesture algorithm and provides the result
*       - Interprets the results
*
* Parameters:
*  pvParameters: unused
*
* Return:
*  None
*
*******************************************************************************/
void processing_task(void *pvParameters)
{
    (void)pvParameters;
    int model_out[IMAI_DATA_OUT_COUNT];
    const char* class_map[] = IMAI_SYMBOL_MAP;
    const float norm_mean[IMAI_DATA_OUT_COUNT] = {9.26814552650607, 4.391583164927378, 0.27332462978312866, -0.02838213175529301, 0.00026668613549266876};
    const float norm_scale[IMAI_DATA_OUT_COUNT] = {5.801363069954616, 7.547439540930497, 0.5629401789624862, 0.41502512890635995, 0.0007474111364241666};

    for(;;)
    {
        /* Wait for frame data available to process */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* pass on the de-interleaved data on to Algorithmic kernel */

        float model_in[IMAI_DATA_IN_COUNT];
        uint16_t min_range_bin = 3;
        slim_algo_output res;
        slim_algo(&res, gesture_frame, &f_cfg, min_range_bin, &work_arrays);
        model_in[0] = ((float)res.detection.range_bin - norm_mean[0]) / norm_scale[0];
        model_in[1] = ((float)res.detection.doppler_bin - norm_mean[1]) / norm_scale[1];
        model_in[2] = ((float)res.detection.azimuth - norm_mean[2]) / norm_scale[2];
        model_in[3] = ((float)res.detection.elevation - norm_mean[3]) / norm_scale[3];
        model_in[4] = ((float)res.detection.value - norm_mean[4]) / norm_scale[4];

        int imai_result_enqueue = IMAI_RED_enqueue(model_in);
        if (IMAI_RET_SUCCESS != imai_result_enqueue)
        {
            printf("Insufficient memory to enqueue sensor data. Inferencing is not keeping up.\n");
        }
        int imai_result = IMAI_RED_dequeue(model_out);
        int pred_idx = 0;
        static int prediction_count = 0;

        /* LED variables */
        static int led_off = 0;
        int ledon_t = 0;

        switch (imai_result)
        {
            static uint8_t success_flag;
            case IMAI_RET_SUCCESS:
                success_flag = 1;
                prediction_count += 1;

                for (uint8_t i = 0; i < IMAI_DATA_OUT_COUNT; i++)
                {
                    if (model_out[i] == 1)
                    {
                        pred_idx = i;
                    }
                }

                if (pred_idx != 0)
                {
                    if ((led_off - CYBSP_LED_STATE_ON) > 0)
                    {
                        printf("\r\n");
                    }
                    /* print triggered class and the triggered time since IMAI Initial. */
                    printf("%s\n", class_map[pred_idx]);
                    cyhal_gpio_write(LED_RGB_GREEN, true); /* turn on green LED */

                    led_off = 0;
                    ledon_t = tick;
                }
                else               
                {
                    /* only print non-label class very 10 predictions */
                    if (prediction_count>9)
                    {
                        printf(".");
                        fflush( stdout );
                        prediction_count = 0;
                    }
                    /* turn off LED after the LED is on for 500ms */
                    if((tick - ledon_t) > 500)
                    {
                        /* turn on green LED */
                        cyhal_gpio_write(LED_RGB_GREEN, false);
                    }
                    led_off = 1;
                }

                break;
            case IMAI_RET_NODATA:
                /* Sleep until more data is available */
                cyhal_syspm_sleep();
                break;
            case IMAI_RET_NOMEM:
                /* Something went wrong, stop the program */
                printf("Unable to perform inference. Internal memory error.\n");
                break;
            case IMAI_RET_TIMEDOUT:
                if (success_flag == 1)
                    {
                    printf("The evaluation period has ended. Please rerun the evaluation or purchase a license for the ready model.\r\n");
                    }
                    success_flag = 0;
                break;
            default:
                /* Something went wrong, stop the program */
                printf("Unable to perform inference. Unknown error occurred.\n");
                break;
        }
    }
}


/*******************************************************************************
* Function Name: get_time_from_millisec_radar
********************************************************************************
* Summary: This function prints the time when a output class is detected.
*    
* Parameters:
*   milliseconds : time when a output class is detected.
*   timeString   : time of detected class in hr:m:s format.
*
* Return:
*   None
*
*
*******************************************************************************/
void get_time_from_millisec_radar(unsigned long milliseconds, char* timeString) {
  unsigned int seconds = (milliseconds / 1000) % 60;
  unsigned int minutes = (milliseconds / (1000 * 60)) % 60;
  unsigned int hours = (milliseconds / (1000 * 60 * 60));
  sprintf(timeString, "%02u:%02u:%02u", hours, minutes, seconds);
}


/*******************************************************************************
* Function Name: radar_init
********************************************************************************
* Summary:
* This function configures the SPI interface, initializes radar and interrupt
* service routine to indicate the availability of radar data.
*
* Parameters:
*  void
*
* Return:
*  Success or error
*
*******************************************************************************/
static int32_t radar_init(void)
{
    if (cyhal_spi_init(&spi_obj,
                       PIN_XENSIV_BGT60TRXX_SPI_MOSI,
                       PIN_XENSIV_BGT60TRXX_SPI_MISO,
                       PIN_XENSIV_BGT60TRXX_SPI_SCLK,
                       NC,
                       NULL,
                       8,
                       CYHAL_SPI_MODE_00_MSB,
                       false) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: cyhal_spi_init failed\n");
        return -1;
    }

    /* Reduce drive strength to improve EMI */
    Cy_GPIO_SetSlewRate(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_MOSI), CY_GPIO_DRIVE_1_8);
    Cy_GPIO_SetSlewRate(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CY_GPIO_SLEW_FAST);
    Cy_GPIO_SetDriveSel(CYHAL_GET_PORTADDR(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CYHAL_GET_PIN(PIN_XENSIV_BGT60TRXX_SPI_SCLK), CY_GPIO_DRIVE_1_8);

    /* Set the data rate to 25 Mbps */
    if (cyhal_spi_set_frequency(&spi_obj, XENSIV_BGT60TRXX_SPI_FREQUENCY) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: cyhal_spi_set_frequency failed\n");
        return -1;
    }

    if (xensiv_bgt60trxx_mtb_init(&bgt60_obj,
                                  &spi_obj,
                                  PIN_XENSIV_BGT60TRXX_SPI_CSN,
                                  PIN_XENSIV_BGT60TRXX_RSTN,
                                  register_list,
                                  XENSIV_BGT60TRXX_CONF_NUM_REGS) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: xensiv_bgt60trxx_mtb_init failed\n");
        return -1;
    }

    if (xensiv_bgt60trxx_mtb_interrupt_init(&bgt60_obj,
                                            NUM_SAMPLES_PER_FRAME*2,
                                            PIN_XENSIV_BGT60TRXX_IRQ,
                                            GPIO_INTERRUPT_PRIORITY,
                                            xensiv_bgt60trxx_interrupt_handler,
                                            NULL) != CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: xensiv_bgt60trxx_mtb_interrupt_init failed\n");
        return -1;
    }

    return 0;
}


/*******************************************************************************
* Function Name: xensiv_bgt60trxx_interrupt_handler
********************************************************************************
* Summary:
* This is the interrupt handler to react on sensor indicating the availability
* of new data
*    1. Triggers the radar data manager for buffering radar data into software buffer.
*
* Parameters:
*  void
*
* Return:
*  none
*
*******************************************************************************/
#if defined(CYHAL_API_VERSION) && (CYHAL_API_VERSION >= 2)
static void xensiv_bgt60trxx_interrupt_handler(void *args, cyhal_gpio_event_t event)
#else
static void xensiv_bgt60trxx_interrupt_handler(void *args, cyhal_gpio_irq_event_t event)
#endif
{
    CY_UNUSED_PARAMETER(args);
    CY_UNUSED_PARAMETER(event);

    mgr.run(true);
}


/*******************************************************************************
* Function Name: init_leds
********************************************************************************
* Summary:
* This function initializes the GPIOs for LEDs and set them to off state.
* Parameters:
*  void
*
* Return:
*  Success or error
*
*******************************************************************************/
static int32_t init_leds(void)
{

    if(cyhal_gpio_init(LED_RGB_RED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, false)!= CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: GPIO initialization for LED_RGB_RED failed\n");
        return -1;
    }

    if( cyhal_gpio_init(LED_RGB_GREEN, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, false)!= CY_RSLT_SUCCESS)
    {
        printf("[MSG] ERROR: GPIO initialization for LED_RGB_GREEN failed\n");
        return -1;
    }

    return 0;
}


/*******************************************************************************
 * Function Name: create_radar_task
 ********************************************************************************
 * Summary:
 *  Function that creates the radar sensor task.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  CY_RSLT_SUCCESS upon successful creation of the radar sensor task, else a
 *  non-zero value that indicates the error.
 *
 *******************************************************************************/
cy_rslt_t create_radar_task(void)
{
    BaseType_t status;
    printf("****************** IMAGIMOB Ready Model Gesture Code Example ****************** \r\n\n");

    mgr.in_read_radar_data = read_radar_data;
    radar_data_manager_init(&mgr, NUM_SAMPLES_PER_FRAME *6, NUM_SAMPLES_PER_FRAME *2);
    radar_data_manager_set_malloc_free(pvPortMalloc, vPortFree);

    /* Create the RTOS task */
    status = xTaskCreate(radar_task, RADAR_TASK_NAME,
                         RADAR_TASK_STACK_SIZE, NULL,
                         RADAR_TASK_PRIORITY, &radar_task_handler);

    return (pdPASS == status) ? CY_RSLT_SUCCESS : (cy_rslt_t) status;
}
