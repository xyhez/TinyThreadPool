#include<iostream>
#include"../ThreadPool.h"
using namespace std;
/**
 * @brief 普通测试
 * 测试内容：
 * 1.普通全局函数
 * 2.类成员函数
 * 3.lambda表达式
 * 4.std::function包装后的可调用对象
 * 5.仿函数
 */

// 普通全局函数
void task1_1() {
    cout<<"普通全局函数-无参-无返回值"<<endl;
}
// 普通全局函数-有返回值-有参
int task1_2(int a) {
    cout<<"普通全局函数-有参-有返回值"<<endl;
    a+=1;
    return a;
}

// 类成员函数
class Base {
    public:
    void task2_1();

    int task2_2(int a);
};

int main() {
    ThreadPoolConfig config(10,20,chrono::seconds(20),50);
    ThreadPool* pool = new ThreadPool(config);

    // 普通全局函数
    pool->submitTask(task1_1);
    auto future = pool->submit_with_result(task1_2,1);
    int result = future.get();
    cout<<result<<endl;

    // 成员函数
    Base base;
    pool->submit_with_result(&Base::task2_2,base,1);

    return 0;
}
