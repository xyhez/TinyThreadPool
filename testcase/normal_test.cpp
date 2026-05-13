
#include"available_object.h"
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



int main() {
    ThreadPoolConfig config(0, 20, chrono::seconds(20), 50);
    ThreadPool pool(config);
    Base base;

    // =============================================
    // 普通全局函数---压力测试
    // =============================================

    // for (int i = 0; i < 2; i++) {
    //     pool.submit_with_result(stressTest,std::ref(pool));
    // }

    for (int i = 0; i < 10000; i++) {
        pool.submitTask(task1_1);
        cout<<"----------------------当前活跃的线程数:"
        <<pool.active_thread()
        <<"----------------------"
        <<endl;
    }

/*
    // =============================================
    // 1. 普通全局函数
    // =============================================

    // 无参-无返回值 (submitTask)
    pool.submitTask(task1_1);
    cout<<"----------------------当前活跃的线程数:"
    <<pool.active_thread()
    <<"----------------------"
    <<endl;

    // 有参-有返回值 (submit_with_result)
    auto future = pool.submit_with_result(task1_2, 1);
    int result = future.get();
    cout<<result<<endl;
    cout<<"----------------------当前活跃的线程数:"
    <<pool.active_thread()
    <<"----------------------"
    <<endl;

    // =============================================
    // 2. 类成员函数
    // =============================================

    // 传指针
    auto fut = pool.submit_with_result(&Base::task2_2, &base, 10);
    result = fut.get();
    cout<<"class member via ptr:"<<result<<endl;
    cout<<"----------------------当前活跃的线程数:"
    <<pool.active_thread()
    <<"----------------------"
    <<endl;

    // 传 reference_wrapper
    fut = pool.submit_with_result(&Base::task2_2, std::ref(base), 20);
    result = fut.get();
    cout<<"class member via ref:"<<result<<endl;
    cout<<"----------------------当前活跃的线程数:"
    <<pool.active_thread()
    <<"----------------------"
    <<endl;

    // void 成员函数 + submit_with_result -> future<void>
    auto fut_void = pool.submit_with_result(&Base::task2_1, &base);
    fut_void.get();

    // =============================================
    // 3. lambda 表达式
    // =============================================

    // 无返回值 (submitTask)
    pool.submitTask([] {
        cout<<"lambda, no return"<<endl;
    });

    // 有返回值 (submit_with_result)
    fut = pool.submit_with_result([](int x, int y) -> int {
        cout<<"lambda, has return"<<endl;
        return x * y;
    }, 3, 5);
    result = fut.get();
    cout<<"lambda result:"<<result<<endl;

    // 带捕获的 lambda
    int captured = 100;
    fut = pool.submit_with_result([&captured](int x) -> int {
        return captured + x;
    }, 7);
    result = fut.get();
    cout<<"lambda with capture:"<<result<<endl;

    // =============================================
    // 4. std::function 包装
    // =============================================

    // 包装普通函数 -> submitTask
    std::function<void()> func = task1_1;
    pool.submitTask(func);

    // 包装 lambda -> submit_with_result
    std::function<int(int, int)> fn_add = [](int a, int b) -> int {
        return a + b;
    };
    fut = pool.submit_with_result(fn_add, 8, 2);
    result = fut.get();
    cout<<"std::function return:"<<result<<endl;

    // 包装成员函数 (bind)
    std::function<int(int)> fn_member = std::bind(&Base::task2_2, &base, std::placeholders::_1);
    fut = pool.submit_with_result(fn_member, 30);
    result = fut.get();
    cout<<"std::function bind member:"<<result<<endl;

    // =============================================
    // 5. 仿函数
    // =============================================

    // 匿名对象
    future = pool.submit_with_result(Base{}, 10, 20);
    result = future.get();
    cout<<"functor temp:"<<result<<endl;

    // 已存在的对象
    fut = pool.submit_with_result(base, 40, 50);
    result = fut.get();
    cout<<"functor obj:"<<result<<endl;

    // =============================================
    // 6. 异常处理
    // =============================================

    auto fut_ex = pool.submit_with_result([](int x) -> int {
        if (x < 0)
            throw std::runtime_error("negative value");
        return x * 2;
    }, -1);

    try {
        fut_ex.get();
    } catch (const std::exception& e) {
        cout<<"exception caught: "<<e.what()<<endl;
    }

    // =============================================
    // 7. 多任务并发提交
    // =============================================

    std::vector<std::future<int>> futs;
    for (int i = 0; i < 5; ++i) {
        futs.push_back(pool.submit_with_result(task1_2, i));
    }
    cout<<"concurrent results: ";
    for (auto& f : futs) {
        cout<<f.get()<<" ";
    }
    cout<<endl;

*/
    // std::this_thread::sleep_for(std::chrono::seconds(11));
    // cout<<"----------------------progress active_threads:"
    // <<pool.active_thread()
    // <<"----------------------"
    // <<endl;
    //

    pool.shutdown();
    // std::this_thread::sleep_for(std::chrono::seconds(5));
    cout<<"----------------------active_threads:"
    <<pool.active_thread()
    <<"----------------------"
    <<endl;
    return 0;
}