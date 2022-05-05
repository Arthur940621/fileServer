#pragma once

#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>
#include <condition_variable>
#include <atomic>
#include <future>

#define  THREADPOOL_MAX_NUM 16

class ThreadPool {
public:
    using Task = std::function<void()>;
    using UniqueLock = std::unique_lock<std::mutex>;
    
    // 构造函数
    explicit ThreadPool(int num = std::thread::hardware_concurrency()) : is_exit_(false), task_run_count_(0) {
        addThread(num);
        manager_th_ = std::move(std::thread(&ThreadPool::manager, this));
    }

    // 析构函数
    ~ThreadPool() {
        is_exit_ = true;
        manager_th_.join();
        cv_.notify_all(); // 唤醒所有线程执行
        for (auto& th : worker_th_) {
            th.join(); // 等待任务结束
        }
        UniqueLock lock(mux_);
        worker_th_.clear(); // 清空线程数组
    }

    // 禁止拷贝和移动
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator = (ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator = (const ThreadPool&) = delete;

    int taskRunCount() {
        return task_run_count_;
    }

    // 提交任务
    // commit()是一个模板函数，class... Args是可变模版参数。
    template <class F, class... Args>
    // 这里函数类型的定义用到了尾返回类型推导。
    auto commit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        if (is_exit_) { // stoped
            throw std::runtime_error("commit on ThreadPool is stopped.");
        }
        // std::function进行包装产生了一个特殊函数，这个特殊函数使用std::bind将函数f和参数args绑定起来。
        // std::forward()将会完整保留参数的引用类型进行转发。
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        // std::packaged_task<decltype(f(args...))()>将前面std::function方法声明的特殊函数包装func传入作为std::packaged_task的实例化参数。
        // 智能指针方便对该std::packaged_task对象进行管理。
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);
        // std::function将task_ptr指向的std::packaged_task对象取出并包装为void函数。
        
        {
            UniqueLock lock(mux_);
            tasks_.emplace([task_ptr](){
                (*task_ptr)();
            });
        }
        cv_.notify_one(); // 唤醒一个线程执行
        return task_ptr->get_future();
    }

private:
    std::mutex mux_; // 锁
    std::vector<std::thread> worker_th_; // 工作线程
    std::condition_variable cv_; // 条件变量
    std::queue<Task> tasks_; // 任务队列
    std::atomic<bool> is_exit_; // 线程池是否退出
    std::atomic<int>  task_run_count_; // 当前运行任务的数量
    std::thread manager_th_; // 管理者线程

    void addThread(int size) {
        for (; worker_th_.size() < THREADPOOL_MAX_NUM && size > 0; --size) {
            worker_th_.emplace_back([this](){
                while (true) {
                    Task task;
                    {
                        UniqueLock lock(mux_);
                        cv_.wait(lock, [this](){
                            return is_exit_ || !tasks_.empty();
                        }); // 没有退出且任务队列为空，阻塞
                        // 线程池退出或者队列不为空
                        if (is_exit_ && tasks_.empty()) { // 线程池退出且没有任务
                            return; // 不添加线程
                        }
                        task = std::move(tasks_.front()); // 从队列取一个task
                        tasks_.pop();
                    }
                    ++task_run_count_; // 忙线程数量加1
                    task(); // 执行任务
                    --task_run_count_; // 忙线程数量减1
                }
            });
        }
    }

    void manager() {
        while (!is_exit_) {
            std::this_thread::sleep_for(std::chrono::seconds(1)); // 每隔1s检测一次
            int threads_num = 0;
            int tasks_num = 0;
            {
                UniqueLock lock(mux_);
                threads_num = worker_th_.size();
                tasks_num = tasks_.size();
            }
            if (tasks_num > threads_num && threads_num < THREADPOOL_MAX_NUM) {
                addThread(1);
            }
            if (task_run_count_ * 2 < threads_num && threads_num > 1) {
                cv_.notify_one();
            }
        }
    }
};