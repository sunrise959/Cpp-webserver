// 线程同步机制：互斥锁
#ifndef LOCKER_H    
#define LOCKER_H
#include <pthread.h>
#include <exception>
#include <semaphore.h>

// 互斥锁
class locker{
    public:
        locker(){
            if(pthread_mutex_init(&mutex, NULL) != 0){
                throw std::exception();
            }
        }

        ~locker(){
            pthread_mutex_destroy(&mutex);
        }

        pthread_mutex_t* getlock(){
            return &mutex;
        }

        bool lock(){
            return pthread_mutex_lock(&mutex) == 0;
        }

        bool unlock(){
            return pthread_mutex_unlock(&mutex) == 0; 
        }

    private:
        pthread_mutex_t mutex;
};

// 条件变量: 阻塞或唤醒进程
class condition{
    public:
        condition(){
            if(pthread_cond_init(&cond, NULL) != 0){
                throw std::exception();
            }
        }

        ~condition(){
            pthread_cond_destroy(&cond);
        }

        bool wait(pthread_mutex_t* mutex){
            return pthread_cond_wait(&cond, mutex); 
        }

        bool timeWait(pthread_mutex_t* mutex, struct timespec t){
            return pthread_cond_timedwait(&cond, mutex, &t) == 0; 
        }    
        
        // 唤醒进程
        bool signal(){
            return pthread_cond_signal(&cond) == 0;
        }

        bool broadcast(){
            return pthread_cond_broadcast(&cond) == 0;
        }
    private:
        pthread_cond_t cond; 
};

// 信号量
class semaphore{
    public:
        semaphore(){
            if(sem_init(&sem, 0, 0) != 0){
                throw std::exception();
            }
        }

        semaphore(int &num){
            if(sem_init(&sem, 0, num) != 0){
                throw std::exception();
            }
        }

        ~semaphore(){
            sem_destroy(&sem);
        }

        // PV操作
        bool wait(){
            return sem_wait(&sem) == 0;
        }

        bool signal(){
            return sem_post(&sem) == 0;
        }
    private:
        sem_t sem;
};

#endif