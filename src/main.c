#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "driver/gpio.h"

#define GPIO_IN GPIO_NUM_25
#define GPIO_OUT GPIO_NUM_26
#define ONBOARD_LED GPIO_NUM_13

#define QUEUESIZE 20

#define CAPITAL_OFFSET ('A' - 'a')

void vPrintString(const char* string){
    printf("%s", string);
    fflush(stdout);
}

void vPrintChar(const char chara){
    printf("\n%c\n", chara);
    fflush(stdout);
}

void vPutStringInQueue(void* param){ //SIMULATE a task that adds characters to the queue DO NOT ACTUALLY USE
    QueueHandle_t charQueue = *((QueueHandle_t*) param);
    assert(charQueue);

    char* inputString = "MYNAMEISCAMPBELLHODGE"; //-- -.-- -. .- -- . .. ... -.-. .- -- .--. -... . .-.. .-.. .... --- -.. --. .
    bool done = false;

    for(int index = 0;;){
        if(inputString[index] && !done){ //easily reads out of bounds
            xQueueSend(charQueue,inputString+index,pdMS_TO_TICKS(1000));
            index++;
        }else if(done){
            vTaskPrioritySet(NULL,0);
        }else{
            done = true;
        }
    }
}

void vStringToMorse(void* param){
    QueueHandle_t charQueue = ((QueueHandle_t*) param)[0];
    QueueHandle_t intQueue = ((QueueHandle_t*) param)[1];
    assert(charQueue && intQueue);

    const char charArray[] = {'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','1','2','3','4','5','6','7','8','9','0'};
    const u_int16_t intArray[] = {6,85,363,21,0,90,45,42,2,438,53,86,13,5,109,182,429,22,10,1,26,106,54,213,437,173,3510,1754,874,426,170,341,685,1389,2925,7021};
    int i = 0;
    char readChar = 0;

    for(;;){
        //vPrintString("Trying to read from Queue\n");
        //printf("Messages in charQueue: %i\n",uxQueueMessagesWaiting(charQueue));
        if(uxQueueMessagesWaiting(charQueue)){
            //vPrintString("Entered Chara conversion\n");
            i = 0;
            xQueueReceive(charQueue,&readChar,0);
            //vPrintChar(readChar);
            for(; i < 36;i++){
                //printf("Index: %i\n",i);
                if(readChar == charArray[i] || readChar + CAPITAL_OFFSET == charArray[i]){break;}
            }
            if(uxQueueMessagesWaiting(intQueue)){ //could cause an extra long dash if this returns false before line +22 triggers
                //printf("\nSending to queue: %u",*(intArray+i));
                xQueueSend(intQueue,intArray+i,pdMS_TO_TICKS(1000));
            }else{
                uint16_t temp = intArray[i]*2+1;
                //printf("\nSending to queue: %u",temp);
                xQueueSend(intQueue,&temp,pdMS_TO_TICKS(1000));
            }
        }else{
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void vMorseFlash(void* param){
    QueueHandle_t intQueue = *((QueueHandle_t*) param);
    assert(intQueue);
    u_int16_t mask,flashSequence;
    TickType_t baseInteveral = pdMS_TO_TICKS(500);

    for(;;){
        if(uxQueueMessagesWaiting(intQueue)){
            mask = 0x01;
            xQueueReceive(intQueue,&flashSequence,0);
            //printf("Int from Queue: %u",flashSequence);
            for(;mask;mask*=2){
                printf("%u & %u : %d\n",flashSequence,mask,flashSequence & mask);
                printf("%u && %u\n",!((flashSequence >> 1) & mask),uxQueueMessagesWaiting(intQueue));
                /*
                if(flashSequence & mask){
                    led_on:
                    gpio_set_level(ONBOARD_LED,1);
                }else if(!((flashSequence >> 1) & mask) && uxQueueMessagesWaiting(intQueue)){ //if current char has been read and there's another in queue, preload it
                    xQueueReceive(intQueue,&flashSequence,0);
                    mask = 0x01;
                    printf("%u characters remaining\n",uxQueueMessagesWaiting(intQueue));
                    goto led_on;
                }else{
                    gpio_set_level(ONBOARD_LED,0);
                }*/
                gpio_set_level(ONBOARD_LED,flashSequence & mask);
                if(!((flashSequence >> 1) & mask) && uxQueueMessagesWaiting(intQueue)){
                    xQueueReceive(intQueue,&flashSequence,0);
                    flashSequence = flashSequence * 2 + 1;
                    mask = 0x01;
                }
                vTaskDelay(baseInteveral);
            }
        }else{
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void vSendInputBuffer(void* param){
    QueueHandle_t* sendTo = (QueueHandle_t*) param;
    assert(sendTo);
    uint32_t buffer = 0;

    for(;;){
        xTaskNotifyWait(0,ULONG_MAX,buffer,portMAX_DELAY);
        xQueueSend(sendTo,(char) buffer,portMAX_DELAY);
    }
}

void vHandleInput(void* param){
    int loopNum = 1;
    char buffer = 0;

    for(;;loopNum++){
        buffer = (buffer << 1) | gpio_get_level(GPIO_IN);
        if(loopNum%8==0){
            xTaskNotify(xTaskGetHandle("SendBuffer"),buffer,eSetValueWithoutOverwrite);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void SendTestGPIOInput(){
    const char* testString = "MYNAMEISCAMPBELLHODGE";
    for(int index = 0;testString[index];index++){
        for(unsigned char mask = 1;mask;mask = mask << 1){
            gpio_set_level(GPIO_OUT,testString[index] & mask);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void app_main() {
    vPrintString("Program Start\n");
    QueueHandle_t charQueue,intQueue;

    gpio_reset_pin(GPIO_IN);
    gpio_set_direction(GPIO_IN, GPIO_MODE_INPUT);
    gpio_reset_pin(GPIO_OUT);
    gpio_set_direction(GPIO_OUT, GPIO_MODE_OUTPUT);
    gpio_reset_pin(ONBOARD_LED);
    gpio_set_direction(ONBOARD_LED,GPIO_MODE_OUTPUT);

    charQueue = xQueueCreate(QUEUESIZE,sizeof(char));
    intQueue = xQueueCreate(QUEUESIZE,sizeof(uint16_t));
    QueueHandle_t handles[] = {charQueue,intQueue};

    if(charQueue && intQueue){
        xTaskCreate(vPutStringInQueue,"PutStringInQueue",2048,(void*) &charQueue,2,NULL);
        xTaskCreatePinnedToCore(vStringToMorse,"StringToMorse",2048,(void*) handles,1,NULL,1);
        xTaskCreatePinnedToCore(vMorseFlash,"MorseFlash",2048,&intQueue,1,NULL,0);
        xTaskCreate(vHandleInput,"HandleGPIO",1024,NULL,tskIDLE_PRIORITY+1,NULL);
        xTaskCreate(vSendInputBuffer,"SendBuffer",1024,handles[0],tskIDLE_PRIORITY+1,NULL);
    }else{
        printf("%s","Error: One or both queues not created\n");
    }
    for(;;){
        SendTestGPIOInput();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    return;
}