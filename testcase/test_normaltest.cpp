#include "../ThreadPool.h"
#include<iostream>

using namespace std;

/**
 * @brief  任务一:输出100pic的task1
 */
void task1() {
    for (int i = 0;i<100;i++) {
        cout<<"TASK_1"<<endl;
    }
    cout<<"TASK_1 FINISH."<<endl;
}

/**
 * @brief 做计算先睡眠再给出计算结果
 */
void task2() {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    cout<<"TASK_2 FINISH."<<endl;
}

/**
 * @brief 任务3，有参数和返回值
 * @param a
 * @param b
 * @return
 */
int task3(int a, int b) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return a + b;
}

int main() {
    ThreadPoolConfig config(6,11,std::chrono::seconds(11),31);


    ThreadPool* Tpool = new ThreadPool(config);

    cout<<Tpool->max_thread_count()<<endl;
    // 向线程池中添加任务
    Tpool->submitTask(task1);
    Tpool->submitTask(task2);
    auto future = Tpool->submit_with_result(task3,1,1);
    int result = future.get();
    cout<<"Task_3 FINISH: 1+1="<<result<<endl;

    Tpool->shutdown();
    delete Tpool;
    cout<<"main thread exit"<<endl;
}