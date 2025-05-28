#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include "driver/gpio.h"
#include "driver/dac_cosine.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_log.h"

#include "env.h"

#define ONBOARD_LED GPIO_NUM_13
#define BUZZER_ANALOG_OUT DAC_CHAN_0

#define QUEUESIZE 20

#define CAPITAL_OFFSET ('A' - 'a')

static dac_cosine_handle_t cw_gen;

void checkRetVal(esp_err_t ret){
    if(ret != ESP_OK){
        ESP_LOGE("check_ret","%s",esp_err_to_name(ret));
    }else{
        ESP_LOGI("check_ret","Ok");
    }
}

void vTCP_Server(void* pvParam){
    char addr_buffer[128];
    struct sockaddr_storage dest_store;
    struct sockaddr_in* dest_addr = (struct sockaddr_in*) &dest_store;
    dest_addr->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr->sin_family = AF_INET;
    dest_addr->sin_port = htons(PORT);
    int ip_protocol = IPPROTO_IP;

    int keepalive = 1000;
    int keepidle = 10;
    int interval = 1;
    int count = 5;

    int listen_sock = socket(AF_INET,SOCK_STREAM,ip_protocol);
    if(listen_sock < 0){
        ESP_LOGE("Server","Unable to create socket: %d",errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    ESP_LOGI("Server","Socket Created");

    int err = bind(listen_sock,(struct sockaddr*) &dest_store,sizeof(dest_store));
    if(err != 0){
        ESP_LOGE("Server","Socket unable to bind: %d",errno);
        ESP_LOGE("Server","IPPROTO: %d",dest_addr->sin_family);
        goto CLEANUP;
    }
    ESP_LOGI("Server", "Socket bound: port %d",PORT);

    err = listen(listen_sock,1);
    if(err != 0){
        ESP_LOGE("Server", "Error listening: %d",errno);
        goto CLEANUP;
    }

    QueueHandle_t* charQueue = ((QueueHandle_t*) pvParam);
    assert(*charQueue);

    for(;;){
        ESP_LOGI("Server","Listening");

        struct sockaddr_storage source;
        socklen_t addr_len = sizeof(source);
        int sock = accept(listen_sock,(struct sockaddr*) &source,&addr_len); //blocking
        if(sock < 0){
            ESP_LOGE("Server","Unable to accept connection: %d",errno);
            break;
        }

        setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,&keepalive,sizeof(keepalive));
        setsockopt(sock,IPPROTO_TCP,TCP_KEEPIDLE,&keepidle,sizeof(keepidle));
        setsockopt(sock,IPPROTO_TCP,TCP_KEEPINTVL,&interval,sizeof(interval));
        setsockopt(sock,IPPROTO_TCP,TCP_KEEPCNT,&count,sizeof(count));
        if(source.ss_family == PF_INET){
            inet_ntoa_r(((struct sockaddr_in*) &source)->sin_addr,addr_buffer,sizeof(addr_buffer) - 1);
        }

        ESP_LOGI("Server","Socket accepted ip: %s",addr_buffer);

        int len;
        char rx_buffer[128];
        do{
            len = recv(sock,rx_buffer,sizeof(rx_buffer) - 1,0);
            if(len < 0){
                ESP_LOGE("Server", "Error receiving: %d",errno);
            }else if(len == 0){
                ESP_LOGW("Server", "Connection closed");
            }else{
                rx_buffer[len] = 0;
                ESP_LOGI("Server", "Received %d bytes: %s",len,rx_buffer);
                int to_write = len;
                while(to_write > 0){
                    if(uxQueueSpacesAvailable(*charQueue)){
                        xQueueSendToBack(*charQueue,rx_buffer + len - to_write,pdMS_TO_TICKS(100));
                        to_write -= 1;
                    }
                }
            }
        }while(len > 0);

        shutdown(sock,0);
        close(sock);
    }

    CLEANUP:
        close(listen_sock);
        vTaskDelete(NULL);
}

static int num_retries = 0;
static EventGroupHandle_t wifi_event_group;
void WifiEventHandler(void* param, esp_event_base_t event_name, int32_t id, void* data){
    ESP_LOGI("event_handle","Param: %p\nName: %s\nID: %li\nData: %p",param,event_name,id,data);
    if(event_name == WIFI_EVENT){
        switch(id){
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                esp_wifi_connect();
                num_retries++;
                break;
            default:
                xEventGroupSetBits(wifi_event_group, BIT1);
                break;
        }
    }else if(event_name == IP_EVENT){
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) data;
        ESP_LOGI("event_handle","Got IP:" IPSTR,IP2STR(&event->ip_info.ip));
        num_retries = 0;
        xEventGroupSetBits(wifi_event_group,BIT0);
    }
}

void vStringToMorse(void* param){
    QueueHandle_t charQueue = ((QueueHandle_t*) param)[0];
    QueueHandle_t intQueue = ((QueueHandle_t*) param)[1];
    assert(charQueue && intQueue);

    const char charArray[] = {'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',' ','\n','\0'};
    const u_int16_t intArray[] = { //https://en.wikipedia.org/wiki/File:International_Morse_Code.svg
        0b011101, //A, 29
        0b0101010111, //B, 343
        0b010111010111, //C, 1495
        0b01010111, //D, 87
        0b01, //E, 1
        0b0101110101, //F, 373
        0b0101110111, //G, 375
        0b01010101, //H, 85
        0b0101, //I, 5
        0b01110111011101, //J, 7645
        0b0111010111, //K, 471
        0b0101011101, //L, 349
        0b01110111, //M, 119
        0b010111, //N, 23
        0b011101110111, //O, 1911
        0b010111011101, //P, 1501
        0b01110101110111, //Q, 7543
        0b01011101, //R, 93
        0b010101, //S, 21
        0b0111, //T, 7
        0b01110101, //U, 117
        0b0111010101, //V, 469
        0b0111011101, //W, 477
        0b011101010111, //X, 1879
        0b01110111010111, //Y, 7639
        0b010101110111, //Z, 1399
        0b10, //' '
        0b100, //'\n'
        0b100 //'\0'
    };
    int i = 0;
    char readChar = 0;

    for(;;){
        if(uxQueueMessagesWaiting(charQueue)){
            i = 0;
            xQueueReceive(charQueue,&readChar,pdMS_TO_TICKS(10));
            
            //printf("Char received: %c\n",readChar);
            for(; i < 27;i++){
                if(readChar == charArray[i] || readChar + CAPITAL_OFFSET == charArray[i]){break;}
            }
            if(uxQueueSpacesAvailable(intQueue)){
                xQueueSendToBack(intQueue,intArray+i,pdMS_TO_TICKS(1000));
            }
        }else{
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
}

void vMorseFlash(void* param){
    QueueHandle_t intQueue = *((QueueHandle_t*) param);
    assert(intQueue);
    const u_int16_t mask = 0x01;
    u_int16_t flashSequence;
    TickType_t baseInteveral = pdMS_TO_TICKS(500);
    bool active = false;

    for(;;){
        if(uxQueueMessagesWaiting(intQueue)){
            xQueueReceive(intQueue,&flashSequence,0);
            //printf("Received: %u\n",flashSequence);
            if(flashSequence & mask){
                for(;flashSequence;flashSequence = flashSequence >> 1){
                    //printf("\tFlash: %u\n\tSequence: %X\n",flashSequence & mask,flashSequence);
                    gpio_set_level(ONBOARD_LED,flashSequence & mask);
                    if(flashSequence & mask && !active){
                        dac_cosine_start(cw_gen);
                        active = true;
                    }else if(active && !(flashSequence & mask)){
                        dac_cosine_stop(cw_gen);
                        active = false;
                    }
                    vTaskDelay(baseInteveral);
                }
                active = false;
                gpio_set_level(ONBOARD_LED,0);
                dac_cosine_stop(cw_gen);
                vTaskDelay(baseInteveral);
            }else{
                switch(flashSequence){
                    case 0x02:
                        vTaskDelay(baseInteveral * 3);
                        break;
                    case 0x04:
                        vTaskDelay(baseInteveral * 7);
                        break;
                    default:
                        break;
                }
            }
        }else{
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void app_main() {
    esp_err_t ret_val;
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    char* ssid = SSID;
    char* pass = PASS;

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ret_val = esp_wifi_init(&wifi_config);
    checkRetVal(ret_val);
    
    wifi_config_t station_config = {};
    memccpy(station_config.sta.ssid,ssid,0,11);
    memccpy(station_config.sta.password,pass,0,13);
    ret_val = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    checkRetVal(ret_val);
    esp_event_handler_instance_t any_id,got_ip;
    ret_val = esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&WifiEventHandler,NULL,&got_ip);
    checkRetVal(ret_val);
    ret_val = esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&WifiEventHandler,NULL,&any_id);
    checkRetVal(ret_val);

    ret_val = esp_wifi_set_mode(WIFI_MODE_STA);
    checkRetVal(ret_val);
    ret_val = esp_wifi_set_config(WIFI_IF_STA,&station_config);
    checkRetVal(ret_val);
    ret_val = esp_wifi_start();
    checkRetVal(ret_val);
    
    EventBits_t bits;
    do{
        bits = xEventGroupWaitBits(wifi_event_group, BIT0 | BIT1,pdFALSE,pdFALSE,portMAX_DELAY);
        if(bits & BIT0){
            ESP_LOGI("ESP_wifi","Connected to SSID:%s",ssid);
        }else if(bits & BIT1){
            ESP_LOGI("ESP_wifi","Failed to connect to SSID:%s",ssid);
            vTaskDelay(pdMS_TO_TICKS(10));
        }else{
            ESP_LOGE("ESP_wifi","Unexpected Event");
        }
    }while(!(bits & BIT0));

    QueueHandle_t charQueue,intQueue;

    const static dac_cosine_config_t cosine_gen_config = {
        .chan_id = BUZZER_ANALOG_OUT,
        .freq_hz = 1024, //max 2048Hz per CEM-1203(42) datasheet
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT, //only clock source supported
        .atten = DAC_COSINE_ATTEN_DB_0,
        .phase = DAC_COSINE_PHASE_0,
        .offset = 0,
        .flags = {
            .force_set_freq = true
        }
    };
    dac_cosine_new_channel(&cosine_gen_config,&cw_gen) == ESP_OK ? printf("Cosine generator configured\n") : printf("Failed to configure cosine generator\n");

    charQueue = xQueueCreate(QUEUESIZE,sizeof(char));
    intQueue = xQueueCreate(QUEUESIZE,sizeof(uint16_t));
    QueueHandle_t handles[] = {charQueue,intQueue};

    if(charQueue && intQueue){
        xTaskCreate(vTCP_Server,"TCPServer",4096,(void*) &charQueue,tskIDLE_PRIORITY + 1,NULL);
        xTaskCreatePinnedToCore(vStringToMorse,"StringToMorse",2048,(void*) handles,tskIDLE_PRIORITY+1,NULL,0);
        xTaskCreatePinnedToCore(vMorseFlash,"MorseFlash",2048,&intQueue,tskIDLE_PRIORITY+1,NULL,1); //takes extra memory
    }else{
        printf("%s","Error: One or both queues not created\n");
    }
    for(;;){
        vTaskDelay(100);
    }

    return;
}