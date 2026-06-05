/******************************************************************************
* File Name:   audio.c
*
* Description: This file implements the interface with the PDM, as
*              well as the PDM ISR to feed the pre-processor.
*
* Related Document: See README.md
*
*******************************************************************************
* (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

#include "audio.h"
#include <stdint.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* Define how many samples in a frame */
#define FRAME_SIZE                  (1024)

/* Noise threshold hysteresis */
#define THRESHOLD_HYSTERESIS        3u

/* Volume ratio for noise and print purposes */
#define VOLUME_RATIO                (4*FRAME_SIZE)

/* Desired sample rate. Typical values: 8/16/22.05/32/44.1/48kHz */
#define SAMPLE_RATE_HZ              16000u

/* Decimation Rate of the PDM/PCM block. Typical value is 64 */
#define DECIMATION_RATE             96u

/* Audio Subsystem Clock. Typical values depends on the desire sample rate:
- 8/16/48kHz    : 24.576 MHz
- 22.05/44.1kHz : 22.579 MHz */
#define AUDIO_SYS_CLOCK_HZ          24576000u
#define DETECTCOUNT                 10
#define LED_STOP_COUNT              500

/* Audio signal clipping threshold for diagnostic purposes */
#define AUDIO_CLIP_THRESHOLD        0.95f

/* PDM/PCM Pins */
#define PDM_DATA                    P10_5
#define PDM_CLK                     P10_4

/* RTOS tasks */
#define AUDIO_TASK_NAME                      "audio_task"
#define AUDIO_TASK_STACK_SIZE                (configMINIMAL_STACK_SIZE * 10)
#define AUDIO_TASK_PRIORITY                  (configMAX_PRIORITIES - 1)

#define AUDIO_SAMPLE_NORMALIZATION_FACTOR    (1.0f / (1 << 23))

#define NUM_SCALING_FACTORS         6

const float scaling_factors[NUM_SCALING_FACTORS] = {2.0f, 4.0f, 6.0f, 8.0f, 10.0f, 1.0f};

/* DMA buffers for storing PDM data */
int32_t audio_buffer0[FRAME_SIZE] __attribute__ ((aligned (32)));
int32_t audio_buffer1[FRAME_SIZE] __attribute__ ((aligned (32)));
int32_t* active_rx_buffer = audio_buffer0;

/*******************************************************************************
* Hardware Register Addresses
* Direct memory-mapped register pointers for PDM FIFO, DMA channel interrupt,
* DMA base, PDM trigger control, and peripheral trigger routing.
********************************************************************************/
uint32_t* pdm_fifo_read        = (uint32_t*)(0x40A00308);
volatile uint32_t* DW1_CH_STRUCT26_INTR = (uint32_t*)(0x40298690);
DW_Type* DMA_DW1_base = (DW_Type*)0x40290000;

volatile uint32_t* PDM0_TR_CTL = (uint32_t*)0x40A00040;
#define RX_REQ_EN 0x00010000

volatile uint32_t* PERI_TR_1TO1_GR4_TR_CTL2 = (uint32_t*)0x4000D008;
#define TR_SEL 0x00000001

/*******************************************************************************
* DMA Descriptors
* Array of 16 DMA descriptors (2 sets of 8) used to transfer audio samples
* from the PDM FIFO into the ping-pong audio buffers via DW1 channel 26.
********************************************************************************/
cy_stc_dma_descriptor_t dma_desc[16];

/*******************************************************************************
* Function Prototypes
********************************************************************************/
void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event);
void button_isr_handler(void *arg, cyhal_gpio_event_t event);
cy_rslt_t audio_init(void);
cy_rslt_t button_init(void);
void clock_init(void);
static float preprocess_audio_sample(int32_t sample);

/*******************************************************************************
* Global Variables
********************************************************************************/
/* Interrupt flags */
volatile bool pdm_pcm_flag = true;
/* Flag to indicate whether scaling factor has been printed */
volatile bool scaling_factor_printed = true;
uint32 tick1 = 0;

/* Initialize scaling factor index */
int scaling_index = 0;

/* Initialize scaling factor */
float scaling_factor = 2.0f;

/* Time variables */
uint32_t time_before = 0;
uint32_t infer_time = 0;

/* HAL Object */
cyhal_pdm_pcm_t pdm_pcm;
cyhal_clock_t   audio_clock;

/* Model Output variable */
int data_out[IMAI_DATA_OUT_COUNT] = {0};
static const char* LABELS[IMAI_DATA_OUT_COUNT] = IMAI_SYMBOL_MAP;

/*This structure is used to initialize callback*/
cyhal_gpio_callback_data_t cb_data;
/* Task handler */
static TaskHandle_t audio_task_handler;

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
    tick1++;
}

/*******************************************************************************
* Function Name: get_time_from_millisec_audio
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
void get_time_from_millisec_audio(unsigned long milliseconds, char* timeString)
{
  unsigned int seconds = (milliseconds / 1000) % 60;
  unsigned int minutes = (milliseconds / (1000 * 60)) % 60;
  unsigned int hours = (milliseconds / (1000 * 60 * 60));
  sprintf(timeString, "%02u:%02u:%02u", hours, minutes, seconds);
}

/*******************************************************************************
* Function Name: configure_dma_descriptors
********************************************************************************
* Summary:
*    Initializes the 16 DMA descriptors used for audio capture. Descriptors
*    are arranged as a circular chain: descriptors 0-7 fill audio_buffer0 and
*    descriptors 8-15 fill audio_buffer1, each transferring 128 words per
*    descriptor. An interrupt is raised at the end of each 8-descriptor group
*    (descriptors 7 and 15) to signal a full 1024-sample frame is ready.
*
* Parameters:
*   dma_desc : Pointer to the array of DMA descriptors to configure.
*
* Return:
*   None
*
*******************************************************************************/
static void configure_dma_descriptors( cy_stc_dma_descriptor_t dma_desc[] )
{
    int x = 0;
    for ( x=0;x<16;x++)
    {
        /* Common for all */
        Cy_DMA_Descriptor_SetDescriptorType(&dma_desc[x], CY_DMA_1D_TRANSFER);
        Cy_DMA_Descriptor_SetSrcAddress(&dma_desc[x], pdm_fifo_read);
        Cy_DMA_Descriptor_SetTriggerInType(&dma_desc[x], CY_DMA_DESCR);
        Cy_DMA_Descriptor_SetDataSize(&dma_desc[x], CY_DMA_WORD); /* Changed from halfword to word for 24-bit implementationv*/
        Cy_DMA_Descriptor_SetSrcTransferSize(&dma_desc[x], CY_DMA_TRANSFER_SIZE_WORD);  /* 32 bits */
        Cy_DMA_Descriptor_SetDstTransferSize(&dma_desc[x], CY_DMA_TRANSFER_SIZE_DATA); /* HalfWord - 16 bits */
        Cy_DMA_Descriptor_SetXloopDataCount(&dma_desc[x], 128);
        Cy_DMA_Descriptor_SetXloopSrcIncrement(&dma_desc[x], 0);
        Cy_DMA_Descriptor_SetXloopDstIncrement(&dma_desc[x], 1);

        /* This is for generating an event for each 1024 words fetched. */
        if ( (x==7) || (x==15) )
        {
            Cy_DMA_Descriptor_SetInterruptType(&dma_desc[x], CY_DMA_DESCR );
        }
        else
        {
            /* This sets the interrupt trigger to the end of the chain. */
            /* The chain is closed and we don't have eny end of chain so we won't  */
            /* get any events for this descriptors. */
            Cy_DMA_Descriptor_SetInterruptType(&dma_desc[x], CY_DMA_DESCR_CHAIN );
        }

        if ( x<8 )
        {
            Cy_DMA_Descriptor_SetDstAddress(&dma_desc[x], audio_buffer0+x*128);
        }
        else
        {
            Cy_DMA_Descriptor_SetDstAddress(&dma_desc[x], audio_buffer1+(x-8)*128);
        }

        Cy_DMA_Descriptor_SetNextDescriptor(&dma_desc[x], &dma_desc[x+1] );
    }

    /* Make the descriptors a round robin list by connecting the last to the first. */
    Cy_DMA_Descriptor_SetNextDescriptor(&dma_desc[15], &dma_desc[0] );

}

/*******************************************************************************
* Function Name: enable_pdm_dma
********************************************************************************
* Summary:
*    Configures and enables the DMA channel (DW1, channel 26) for PDM audio
*    capture. Sets the initial descriptor, channel priority, enables the PDM
*    RX trigger and peripheral trigger routing, then activates the channel and
*    the DMA block.
*
* Parameters:
*   dma_desc : Pointer to the array of pre-configured DMA descriptors.
*
* Return:
*   None
*
*******************************************************************************/
static void enable_pdm_dma( cy_stc_dma_descriptor_t dma_desc[] )
{
    configure_dma_descriptors( dma_desc );

    Cy_DMA_Channel_SetDescriptor(DMA_DW1_base, 26, &dma_desc[0]);
    Cy_DMA_Channel_SetPriority(DMA_DW1_base, 26, 0);

    *PDM0_TR_CTL = RX_REQ_EN;
    *PERI_TR_1TO1_GR4_TR_CTL2 = TR_SEL;

    /* Should remember to disable DMA when stopped. */
    Cy_DMA_Channel_Enable(DMA_DW1_base, 26);
    Cy_DMA_Enable(DMA_DW1_base);
}

/*******************************************************************************
* Function Name: dma_init
********************************************************************************
* Summary:
*    Entry point for DMA initialization. Calls enable_pdm_dma to configure
*    the DMA descriptors and start the PDM audio capture DMA channel.
*
* Parameters:
*   None
*
* Return:
*   None
*
*******************************************************************************/
void dma_init(void)
{
    enable_pdm_dma( dma_desc );
}

/*******************************************************************************
* Function Name: pdm_frequency_fix
********************************************************************************
* Summary:
*    This function is a workaround to apply correct clock frequency to the mic.
*    This is to keep the clock frequency in range with mic specification.
*
* Parameters:
*    void
*
* Return:
*    void
*
*
*******************************************************************************/
static void pdm_frequency_fix()
{
    static uint32_t* pdm_reg = (uint32_t*)(0x40A00010);
    uint32_t clk_clock_div_stage_1 = 2;
    uint32_t mclkq_clock_div_stage_2 = 1;
    uint32_t cko_clock_div_stage_3 = 8;
    /* mic_freq / (2*16000) */
    uint32_t needed_sinc_rate = AUDIO_SYS_CLOCK_HZ / ( clk_clock_div_stage_1 *
        mclkq_clock_div_stage_2 * cko_clock_div_stage_3 * 2 * SAMPLE_RATE_HZ);
    uint32_t pdm_data = (clk_clock_div_stage_1 - 1) << 0;
    pdm_data |= (mclkq_clock_div_stage_2 - 1) << 4;
    pdm_data |= (cko_clock_div_stage_3 - 1) << 8;
    pdm_data |= needed_sinc_rate << 16;
    *pdm_reg = pdm_data;
}

/*******************************************************************************
* Function Name: audio_init
********************************************************************************
* Summary:
*    A function used to initialize and configure the PDM based on the shield
*    selected in the Makefile. Starts an asynchronous read which triggers an
*    interrupt when completed.
*
* Parameters:
*   None
*
* Return:
*     The status of the initialization.
*
*
*******************************************************************************/
cy_rslt_t audio_init(void)
{
    /* HAL PDM Configuration */
    const cyhal_pdm_pcm_cfg_t pdm_pcm_cfg =
    {
        .sample_rate     = SAMPLE_RATE_HZ,
        .decimation_rate = DECIMATION_RATE,
        .mode            = CYHAL_PDM_PCM_MODE_LEFT,
        .word_length     = 24,  /* bits */
        .left_gain       = 21,   /* dB */
        .right_gain      = 21,   /* dB */
    };

    /* Init the clocks */
    clock_init();

    /* Initialize LED1 as GPIO for detection indication */
    cyhal_gpio_init(CYBSP_USER_LED, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, CYBSP_LED_STATE_OFF);

    /* Initialize the PDM/PCM block */
    cyhal_pdm_pcm_init(&pdm_pcm, PDM_DATA, PDM_CLK, &audio_clock, &pdm_pcm_cfg);
    cyhal_pdm_pcm_register_callback(&pdm_pcm, pdm_pcm_isr_handler, NULL);
    cyhal_pdm_pcm_enable_event(&pdm_pcm, CYHAL_PDM_PCM_ASYNC_COMPLETE, CYHAL_ISR_PRIORITY_DEFAULT, true);
    cyhal_pdm_pcm_start(&pdm_pcm);

    /*timer set up*/
    Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_IMO , (8000000/1000)-1);
    Cy_SysTick_SetCallback(0, systick_isr);        /* point to SysTick ISR to increment the 1ms count */


    pdm_frequency_fix();

    dma_init();

    /* Initialize DEEPCRAFT pre-processing library */
    IMAI_AED_init();

    /* If the model selected is for cough detection, set the confidence threshold to 0.7 */
    #ifdef COUGH_MODEL
    struct PP_config postprocessing;
    postprocessing.confidence = 0.7;
    IMAI_AED_sensitivity(postprocessing);
    #endif

    return 0;
}

/*******************************************************************************
* Function Name: button_init
********************************************************************************
* Summary:
*    Initializes the user button as a GPIO input with a pull-up resistor and
*    registers the button ISR to handle falling-edge events for cycling the
*    audio scaling factor.
*
* Parameters:
*   None
*
* Return:
*   The status of the initialization.
*
*******************************************************************************/
cy_rslt_t button_init(void)
{
     /* Initialize the User Button */
    cyhal_gpio_init(CYBSP_USER_BTN, CYHAL_GPIO_DIR_INPUT, CYHAL_GPIO_DRIVE_PULLUP, CYBSP_BTN_OFF);
    cb_data.callback = button_isr_handler;
    cyhal_gpio_register_callback(CYBSP_USER_BTN, &cb_data);
    cyhal_gpio_enable_event(CYBSP_USER_BTN, CYHAL_GPIO_IRQ_FALL, CYHAL_ISR_PRIORITY_DEFAULT, true);

    return 0;

}

/*******************************************************************************
* Function Name: preprocess_audio_sample
********************************************************************************
* Summary:
*    Converts a raw 24-bit PDM integer sample to a normalized floating-point
*    value in the range [-1.0, 1.0] by applying the current scaling factor and
*    the fixed normalization constant. The result is clamped to the valid range.
*
* Parameters:
*   sample : Raw 32-bit signed integer sample from the PDM FIFO (24-bit data).
*
* Return:
*   Normalized and scaled floating-point audio sample in [-1.0, 1.0].
*
*******************************************************************************/
static float preprocess_audio_sample(int32_t sample)
{
    const float scale_norm = scaling_factor * AUDIO_SAMPLE_NORMALIZATION_FACTOR;
    float processed_sample = ((float)sample) * scale_norm;

    if (processed_sample > 1.0f)
    {
        processed_sample = 1.0f;
    }
    else if (processed_sample < -1.0f)
    {
        processed_sample = -1.0f;
    }

    return processed_sample;
}
/*******************************************************************************
* Function Name: audio_buffer_signal_properties
********************************************************************************
* Summary:
*    Analyzes a frame of audio samples and computes signal quality metrics:
*    RMS level, clipping count (samples outside ±0.95), and peak absolute value.
*    All metrics are derived from normalized floating-point samples.
*
* Parameters:
*   audio_buffer : Pointer to the raw int32 PDM sample buffer of size FRAME_SIZE.
*   rms_out      : Output pointer for the computed RMS value of the frame.
*   clip_out     : Output pointer for the count of clipped samples (|x| > 0.95).
*   max          : Output pointer for the peak absolute sample value in the frame.
*
* Return:
*   None
*
*******************************************************************************/


void audio_buffer_signal_properties(int32_t* audio_buffer, float* rms_out, int* clip_out, float* max)
{
    float temp_rms = 0;
    int temp_clip = 0;
    float y;
    float highest_value = 0;
    /* Calculate the RMS value & clipping */
    for (int i = 0; i<FRAME_SIZE; i++)
    {
        float x = preprocess_audio_sample(audio_buffer[i]);
        y = x;

        if (fabsf(x) > AUDIO_CLIP_THRESHOLD)        /* Count the number of values outside of clipping threshold */
            temp_clip++;

        x = x*x;            /* Square value */
        temp_rms += x;      /* Add values */

        if (fabsf(y) > highest_value)
        {
            highest_value = fabsf(y);
        }

    }

    *rms_out = (sqrt(temp_rms/FRAME_SIZE)); /* sqrt the sum of squares hence RMS */
    *max = highest_value;                   /* find highest value in buffer */
    *clip_out = temp_clip;              /* count number of values outside of -0.95 < x < 0.95 range */
}



/*******************************************************************************
* Function Name: audio_task
********************************************************************************
* Summary:
* This is the main task.
*    1. Initializes the PDM/PCM block.
*    2. Wait for the frame data available for process.
*    3. Runs the model and provides the result.
* Parameters:
*  pvParameters : unused
*
* Return:
*  none
*
*******************************************************************************/
void audio_task(void *pvParameters)
{
    cy_rslt_t rslt;
    /* LED variables */
    static int led_off = 0;
    static int led_on = 0;

    static int prediction_count = 0;
    static int16_t success_flag = 1;

    rslt = audio_init();
    if(rslt != 0)
    {
        CY_ASSERT(0);
    }
    rslt = button_init();
    if(rslt != 0)
    {
        CY_ASSERT(0);
    }
    unsigned long start_t = tick1;
    for(;;)
    {
        if (!scaling_factor_printed)
        {
            printf("Scaling factor: %.0f\n", scaling_factor);
            scaling_factor_printed = true;
        }
        /* Just check the dma flag if it is set */
        int dma_intr = *DW1_CH_STRUCT26_INTR & 0x01;
        if ( dma_intr ){

            *DW1_CH_STRUCT26_INTR = 0x01; /* Clear the DMA interrupt */
        }
        if ( dma_intr )
        {
            /* Clear the PDM/PCM flag */
            pdm_pcm_flag = 0;

            #ifdef AUDIO_PROPERTIES_ENABLED
            int clip_count = 0;
            float rms = 0.0f;
            float max_value = 0.0f;

            audio_buffer_signal_properties(active_rx_buffer, &rms, &clip_count, &max_value);
            printf("RMS: %f, Clip Count: %d, Max: %f Buff: %d\r\n",rms,clip_count, max_value, active_rx_buffer[200]);  /* Turn this on to see what the parameters of every buffer is */
            #endif


            /* Calculate the volume by summing the absolute value of all the
             * audio data from a frame */
            for (uint32_t index = 0; index < FRAME_SIZE; index++)
            {
                float data_in = preprocess_audio_sample(active_rx_buffer[index]);
                /* Pass audio data for enqueue */
                IMAI_AED_enqueue(&data_in);

                time_before = tick1;
                switch(IMAI_AED_dequeue(data_out))
                {
                    case IMAI_RET_SUCCESS:
                        /*Get time after dequeue*/
#ifdef PROFILING_ENABLED
                        infer_time = tick1 - time_before;
                        printf( "infer_time: %lu\r\n", infer_time);
#endif
                        prediction_count += 1;
                        if (data_out[1] == 1)
                        {
                            /* New line when LED from off to on */
                            if ((led_off - CYBSP_LED_STATE_ON) > 0)
                            {
                                printf("\r\n");
                            }

                            /* Print triggered class and the triggered time since IMAI init.*/
                            unsigned long t = tick1 - start_t;
                            char timeString[9];
                            get_time_from_millisec_audio(t, timeString);
                            printf("%s %s\r\n",LABELS[1],timeString);

                            /* Turn on LED1 for detection indication */
                            /* LED2 continues showing scaling factor brightness via PWM */
                            cyhal_gpio_write((cyhal_gpio_t) CYBSP_USER_LED, CYBSP_LED_STATE_ON);
                            led_off = 0;
                            led_on = tick1;
                        }
                        else
                        {
                            /* Only print non-label class very 10 predictions */
                            if (prediction_count>DETECTCOUNT)
                            {
                                printf(".");
                                fflush( stdout );
                                prediction_count = 0;
                            }
                            /* Turn off LED1 after detection pulse (500ms) */
                            if((tick1 - led_on) > LED_STOP_COUNT)
                            {
                                cyhal_gpio_write((cyhal_gpio_t) CYBSP_USER_LED, CYBSP_LED_STATE_OFF);
                            }
                            led_off = 1;
                        }
                        break;

                    case IMAI_RET_TIMEDOUT:
                        if (success_flag == 1)
                        {
                            printf("The evaluation period has ended. Please rerun the evaluation or purchase a license for the ready model.\r\n");
                        }
                        success_flag = 0;
                        break;
                }
            }

            /* Swap the buffers */
            if ( active_rx_buffer == audio_buffer0 )
            {
                active_rx_buffer = audio_buffer1;
            }
            else
            {
                active_rx_buffer = audio_buffer0;
            }

        }
    }

}


/*******************************************************************************
 * Function Name: create_audio_task
 ********************************************************************************
 * Summary:
 *  Function that creates the audio task.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  CY_RSLT_SUCCESS upon successful creation of the radar sensor task, else a
 *  non-zero value that indicates the error.
 *
 *******************************************************************************/
cy_rslt_t create_audio_task(void)
{
    BaseType_t status;
    printf("****************** DEEPCRAFT Ready Model: %s ****************** \r\n\n", LABELS[1]);

    /* Create the RTOS task */
    status = xTaskCreate(audio_task, AUDIO_TASK_NAME, AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY, &audio_task_handler);


    return (pdPASS == status) ? CY_RSLT_SUCCESS : (cy_rslt_t) status;
}


/*******************************************************************************
* Function Name: pdm_pcm_isr_handler
********************************************************************************
* Summary:
*  PDM/PCM ISR handler. A flag is set when the interrupt is generated.
*
* Parameters:
*  arg: not used
*  event: event that occurred
*
*******************************************************************************/

/* Get interrupt from DMA */
void pdm_pcm_isr_handler(void *arg, cyhal_pdm_pcm_event_t event)
{
    (void) arg;
    (void) event;

    /* when the flag is set, data from the sensor is read in the main function */
    pdm_pcm_flag = true;
}
/*******************************************************************************
* Function Name: button_isr_handler
********************************************************************************
* Summary:
*  Button ISR handler. Set a flag to be processed in the main loop.
*
* Parameters:
*  arg: not used
*  event: event that occurred
*
*******************************************************************************/
void button_isr_handler(void *arg, cyhal_gpio_event_t event)
{
    (void) arg;
    (void) event;

    scaling_index = (scaling_index + 1) % NUM_SCALING_FACTORS;
    scaling_factor = scaling_factors[scaling_index];
    scaling_factor_printed = false;
}

/*******************************************************************************
* Function Name: clock_init
********************************************************************************
* Summary:
*    A function used to initialize and configure PDM clocks.
*
* Parameters:
*   None
*
* Return:
*     None
*
*
*******************************************************************************/
void clock_init(void)
{
    /* HAL Object */
    cyhal_clock_t   pll_clock;

    /* Initialize the PLL */
    cyhal_clock_reserve(&pll_clock, &CYHAL_CLOCK_PLL[1]);
    cyhal_clock_set_frequency(&pll_clock, AUDIO_SYS_CLOCK_HZ, NULL);
    cyhal_clock_set_enabled(&pll_clock, true, true);

    /* Initialize the audio subsystem clock (CLK_HF[1])
    * The CLK_HF[1] is the root clock for the I2S and PDM/PCM blocks */
    cyhal_clock_reserve(&audio_clock, &CYHAL_CLOCK_HF[1]);

    /* Source the audio subsystem clock from PLL */
    cyhal_clock_set_source(&audio_clock, &pll_clock);
    cyhal_clock_set_enabled(&audio_clock, true, true);
}