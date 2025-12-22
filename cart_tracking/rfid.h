#include <stdint.h>

#define MAX_ENTRIES 100   // store 100 time snapshots


struct CartPathEntry {
    long long timestamp;   // or std::chrono::time_point
    uint8_t rfids[5];      // array 5 (for now) of RFID tag ID 
};

CartPathEntry makeEntry(void);
uint8_t addEntry(CartPathEntry *cart, CartPathEntry *cartPath);
