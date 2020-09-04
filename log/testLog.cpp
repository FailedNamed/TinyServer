#include <iostream>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "log.h"

using namespace std;
int m_close_log = 0;
/*
void testSyn(int number){
    Log::get_instance()->init("log_record/syn", 0, 8192, 5000000, 0);
    struct timeval cur1 = {0, 0};
    gettimeofday(&cur1, NULL);
    for(int i = 0; i < number; ++i){
        LOG_INFO("test syn testsyn test syn");
    }
    struct timeval cur2 = {0, 0};
    gettimeofday(&cur2, NULL);
    cout << "Syn time is: " << (cur2.tv_sec - cur1.tv_sec) << ":" << (cur2.tv_usec - cur1.tv_usec) << endl;
}
*/

void* testAsyn(void* args){
    int number = 1000000;
    // Log::get_instance()->init("asyn", 0, 8192, 5000000, 100);
    Log::get_instance()->init("syn", 0, 8192, 5000000, 0);
    struct timeval cur1 = {0, 0};
    gettimeofday(&cur1, NULL);
    cout << "Asyn time1 is: " << cur1.tv_sec << ":" << cur1.tv_usec << endl;
    for(int i = 0; i < number; ++i){
        LOG_INFO("test syn test syn test syn");
    }
    struct timeval cur2 = {0, 0};
    gettimeofday(&cur2, NULL);
    cout << "Asyn time2 is: " << cur2.tv_sec << ":" << cur2.tv_usec << endl;
}

int main(){
    //testSyn(1000000);
    // testAsyn(1000000);
    
    pthread_t tid1;
    pthread_create(&tid1, NULL, testAsyn, NULL);
    pthread_t tid2;
    pthread_create(&tid2, NULL, testAsyn, NULL);
    
    sleep(30);
    return 0;
}