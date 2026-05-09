#include "../ThreadPool.h"
#include<iostream>

using namespace std;

int main() {
    ThreadPoolConfig config(6,11,std::chrono::seconds(11),31);


    ThreadPool* Tpool = new ThreadPool(config);

    cout<<Tpool->max_thread_count()<<endl;
    std::cin.get();
    cout<<"main thread exit"<<endl;
    Tpool->shutdown();
    delete Tpool;
}