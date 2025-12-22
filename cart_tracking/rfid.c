#include "rfid.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
    REAL-TIME FUNCTIONS (As customer is shopping throughout the store)
        - Store an array of CartPathEntry types. 
            - Each CartPathEntry type stores timestamp and the RFID readings it gets from that timestamp
        - Periodically send data to device handling server while shopping to free up space on the STM. 
            - Server does all calculations
*/


CartPathEntry cartPath[MAX_ENTRIES]
int cartIndex = 0;

SemaphoreHandle_t sendMutex; // to lock cartPath during send

int main(void){
    HAL_Init();
    MX_FREERTOS_Init();
    
    sendMutex = xSemaphoreCreateMutex();

    xTaskCreate(Task_AddEntry, "Collector", 512, NULL, 1, NULL);
    vTaskStartScheduler();
    while(1){}
}


//Fills up CartPathEntry slot
CartPathEntry makeEntry(){
   //reads from the scanner
    //gets the time
    //makes an entry
    CartPathEntry e;
    e.timestamp = HAL_GETTick();
    for(int i = 0; i < 5; i++){
        e.rfids[i] = .......;
    }
    return e;  
}

// ======= TASK 1: ADD ENTRIES =======
void Task_AddEntry(void *argument){
    for(;;){
        CartPathEntry entry = makeEntry();
        cartPath[cartIndex] = entry;
        cartIndex++;
        if(cartIndex >= MAX_ENTRIES){
            if(xSemaphoreTake(sendMutex, 0) == pdTRUE){
                xTaskCreate(Task_SendToServer, "Sender", 1024, NULL, 2, NULL);
                cartIndex = 0;
            }
        }
    }
    vTaskDelay(pdMS_TO_TASK(50));
}

// ======= TASK 2: SEND TO SERVER =======
void Task_SendToServer(void *argument){

    CartPathEntry localCopy[MAX_ENTRIES];
    memcpy(localCopy, cartPath, sizeof(cartPath));

    sendToServer(localCopy, MAX_ENTRIES);

    xSemaphoreGive(sendMutex);
    
    vTaskDelete(NULL);
}


void sendToServer(CartPathEntry *data, int length){

    for(int i = 0; i < length; i++){
        printf()
    }
}

