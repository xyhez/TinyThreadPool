//
// Created by adminstrator on 2026/5/13.
// 用于准备通用测试用例-供normal/stress使用
//

#ifndef THREADPOOL_AVAILABLE_OBJECT_H
#define THREADPOOL_AVAILABLE_OBJECT_H

#include<iostream>
#include"../ThreadPool.h"
using namespace std;

// 普通全局函数-无参-无返回值
void task1_1() {
    // cout<<"normal function-no param-no return"<<endl;


}

// 普通全局函数-有参-有返回值
int task1_2(int a) {
    cout<<"normal function-has param-has return"<<endl;
    a+=1;
    return a;
}

// 类
class Base {
public:
    void task2_1() {
        cout<<"Base::task2_1."<<endl;
    }

    int task2_2(int a) {
        cout<<"Base::task2_2."<<endl;
        return a+1;
    }

    // 仿函数
    int operator()(int a, int b) {
        return a+b;
    }
};

int stressTest(ThreadPool& pool) {
    for (int i = 0; i < 10000; i++) {
        pool.SubmitTask(task1_1);
        cout<<"----------------------当前活跃的线程数:"
        <<pool.ActiveThread()
        <<"----------------------"
        <<endl;
    }
    return 0;
}

#endif //THREADPOOL_AVAILABLE_OBJECT_H
