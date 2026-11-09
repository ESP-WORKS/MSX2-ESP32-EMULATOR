#include <thread>
#include <chrono>
typedef void* TaskHandle_t;
#define pdPASS 1
#define pdTRUE 1
static inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*,int,void*,int,TaskHandle_t*h,int){ std::thread(f,nullptr).detach(); if(h)*h=0; return 1;}
static inline unsigned ulTaskNotifyTake(int,unsigned){ std::this_thread::sleep_for(std::chrono::microseconds(30)); return 0;}
static inline void xTaskNotifyGive(TaskHandle_t){}
static inline void taskYIELD(void){ std::this_thread::yield(); }
