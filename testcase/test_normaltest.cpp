#include "../ThreadPool.h"
#include<iostream>

using namespace std;

int main() {
    ThreadPoolConfig config(6,11,std::chrono::seconds(11),31);

    ThreadPool  Tpool(config);

    cout<<Tpool.max_thread_count()<<endl;

}