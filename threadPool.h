// 改进点：线程池数量根据并发量动态变化
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <pthread.h>
#include "locker.h"
#include <list>
#include <cstdio>

// T: 任务类型 本项目中为http连接
template<typename T>
class threadPool{
    public:
        threadPool(int _threadNum = 8, int _maxRequest = 10000);

        ~threadPool();

        bool append(T* request);
    private:
        /*工作线程运行的函数，从工作队列中取出任务并执行。
        线程的工作函数需定义为静态成员函数，无this指针*/
        static void* worker(void* arg);
        //访问非静态成员
        void run();
    private:
        // 线程数量
        int threadNum;
        // 请求队列最大请求数量
        int maxRequest;
        // 线程池数组
        pthread_t* myThreads;
        // 请求队列
        std::list<T*> workQueue;
        // 互斥锁，互斥访问请求队列
        locker queueLock; 
        // 信号量，记录请求队列任务数量
        semaphore queueState;
        // 结束线程标志
        bool stop;
};

template <typename T>
threadPool<T>::threadPool(int _threadNum, int _maxRequest) : 
threadNum(_threadNum), maxRequest(_maxRequest), myThreads(NULL), stop(false)
{
        if(_threadNum <= 0 || _maxRequest <= 0){
            throw std::exception();
        }

        myThreads = new pthread_t[threadNum];
        if(!myThreads){
            throw std::exception();
        }

        //创建线程，设置线程脱离
        for(int i=0; i < threadNum; i++){
            printf("Create the %dth thread.\n", i);
            if(pthread_create(myThreads + i, NULL, worker, this) != 0){
                delete [] myThreads;
                throw std::exception();
            }

            if(pthread_detach(myThreads[i]) != 0){
                delete [] myThreads;
                throw std::exception();
            }
        }
}

template<typename T>
threadPool<T>::~threadPool(){
    delete [] myThreads;
    stop = true;
}

template<typename T>
bool threadPool<T>::append(T* request){
    queueLock.lock();
    if(workQueue.size() > maxRequest){
        queueLock.unlock();
        return false;
    }
    workQueue.push_back(request);
    queueLock.unlock();
    //V操作
    queueState.signal(); 
    return true;
}

template<typename T>
void* threadPool<T>::worker(void* arg){
    threadPool* pool = (threadPool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadPool<T>::run(){
    while(!stop){
        queueState.wait();
        queueLock.lock();
        /*
        if(workQueue.empty()){
            queueLock.unlock();
            continue;
        }
        */
        T* request =  workQueue.front();
        workQueue.pop_front();
        queueLock.unlock();
        if(!request){
            continue;
        }
        //执行任务
        request->process();
    }
}


#endif