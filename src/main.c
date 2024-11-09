#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "driver/gpio.h"

#define GPIO_IN GPIO_NUM_25
#define GPIO_OUT GPIO_NUM_26
#define ONBOARD_LED GPIO_NUM_13
#define CHECK_INPUT GPIO_NUM_22
#define CHECK_OUTPUT GPIO_NUM_23

#define MAX_INTR_WDT_DELAY pdMS_TO_TICKS(500)

#define QUEUESIZE 20

#define CAPITAL_OFFSET ('A' - 'a')

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
        if(uxQueueMessagesWaiting(charQueue)){
            i = 0;
            xQueueReceive(charQueue,&readChar,0);
            //printf("Char received: %c\n",readChar);
            for(; i < 36;i++){
                if(readChar == charArray[i] || readChar + CAPITAL_OFFSET == charArray[i]){break;}
            }
            if(uxQueueMessagesWaiting(intQueue)){ //could cause an extra long dash if this returns false before line +22 triggers
                xQueueSend(intQueue,intArray+i,pdMS_TO_TICKS(1000));
            }else{
                uint16_t temp = intArray[i]*2+1;
                xQueueSend(intQueue,&temp,pdMS_TO_TICKS(1000));
            }
        }else{
            vTaskDelay(MAX_INTR_WDT_DELAY);
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
            //printf("Int received: %u",flashSequence);
            for(;mask;mask*=2){
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
    QueueHandle_t sendTo = *((QueueHandle_t*) param);
    assert(sendTo);
    uint32_t buffer = 0;

    for(;;){
        xTaskNotifyWait(0,ULONG_MAX,&buffer,MAX_INTR_WDT_DELAY);
        xQueueSend(sendTo,(char*) &buffer,MAX_INTR_WDT_DELAY);
    }
}

void vHandleInput(void* param){
    int loopNum = 1;
    char buffer = 0;
    bool lastCheck = false;
    TaskHandle_t* sendBufferTask = (TaskHandle_t*) param;

    for(;sendBufferTask;loopNum++){
        if(!lastCheck){
            buffer = (buffer << 1) | gpio_get_level(GPIO_IN);
            if(loopNum%8==0){
                xTaskNotify(*sendBufferTask,buffer,eSetValueWithoutOverwrite);
            }
        }
        lastCheck = gpio_get_level(CHECK_INPUT);
        vTaskDelay(MAX_INTR_WDT_DELAY);
    }
}

void SendTestGPIOInput(){
    const char* testString = "MYNAMEISCAMPBELLHODGE"; //13, 437, 5, 2, 10, 363, 6, 13, 182, 85, 0, 86, 86, 42, 109, 21, 45, 0
    bool check = true;
    for(int index = 0;testString[index];index++){
        for(unsigned char mask = 1;mask;mask = mask << 1){
            gpio_set_level(GPIO_OUT,testString[index] & mask);
            gpio_set_level(CHECK_OUTPUT,check);
            check = !check;
            vTaskDelay(MAX_INTR_WDT_DELAY);
        }
    }
}

void app_main() {
    QueueHandle_t charQueue,intQueue;

    gpio_reset_pin(GPIO_IN);
    gpio_set_direction(GPIO_IN, GPIO_MODE_INPUT);
    gpio_reset_pin(GPIO_OUT);
    gpio_set_direction(GPIO_OUT, GPIO_MODE_OUTPUT);
    gpio_reset_pin(ONBOARD_LED);
    gpio_set_direction(ONBOARD_LED,GPIO_MODE_OUTPUT);
    gpio_reset_pin(CHECK_INPUT);
    gpio_set_direction(CHECK_INPUT,GPIO_MODE_INPUT);
    gpio_reset_pin(CHECK_OUTPUT);
    gpio_set_direction(CHECK_OUTPUT,GPIO_MODE_OUTPUT);

    charQueue = xQueueCreate(QUEUESIZE,sizeof(char));
    intQueue = xQueueCreate(QUEUESIZE,sizeof(uint16_t));
    QueueHandle_t handles[] = {charQueue,intQueue};
    TaskHandle_t sendBufferTask;

    if(charQueue && intQueue){
        xTaskCreate(vSendInputBuffer,"SendBuffer",1024,(void*) &charQueue,tskIDLE_PRIORITY+1,&sendBufferTask);
        xTaskCreate(vPutStringInQueue,"PutStringInQueue",1024,(void*) &charQueue,tskIDLE_PRIORITY+2,NULL);
        xTaskCreate(vStringToMorse,"StringToMorse",2048,(void*) handles,tskIDLE_PRIORITY+1,NULL);
        xTaskCreate(vMorseFlash,"MorseFlash",2048,&intQueue,tskIDLE_PRIORITY+1,NULL); //takes extra memory
        xTaskCreate(vHandleInput,"HandleGPIO",1024,(void*) &sendBufferTask,tskIDLE_PRIORITY+1,NULL);
    }else{
        printf("%s","Error: One or both queues not created\n");
    }
    for(;;){
        SendTestGPIOInput();
        vTaskDelay(10000);
    }

    return;
}