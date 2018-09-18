#ifndef ThreadPool_hpp
#define ThreadPool_hpp

#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdio.h>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Constants.hpp"

// REGARDING ADD EXTRA WORKERS (m_addMoreWorkersIfNeeded == true)
//
// Notice that to add workers (when needed), the task of actually adding this
// task to the thread pool must be done in a separate thread, otherwise the
// calling thread is stuck waiting for the maximum allowed delay time. It is no
// longer asynchronous. This means there is extra overhead required to use this
// feature.
//
// There is therefore a trade off between being flexible or not; specifying an
// exact number of threads (for just the initial thread creation) or letting it
// increase as is needed (every addTask request is handled by a separate thread)
// to meet demand.

namespace SS {

using namespace std::chrono;

template <typename TaskType> class ThreadPool {

  // Wrapper struct around TaskType such that it supports a binary comparison
  // operator
  struct TaskObject {

    TaskObject(TaskType &&_task, std::thread::id _id) : id(_id), task(_task) {}

    ~TaskObject() {}

    TaskObject(const TaskObject &other) : task(other.task), id(other.id) {}

    TaskObject &operator=(const TaskObject &other) {
      if (this != &other) {
        task = other.task;
        id = other.id;
      }
      return *this;
    }

    TaskObject &operator=(TaskObject &&other) {
      task = std::move(other.task);
      id = std::move(other.id);
      return *this;
    }

    TaskObject(TaskObject &&other)
        : task(std::move(other.task)), id(std::move(other.id)) {}

    bool operator==(const TaskObject &other) { return id == other.id; }

    TaskType task;
    std::thread::id id;
  };

private:
  // Exclusive access to task queue
  std::mutex m_mutex;

  // Status of the thread pool workers
  std::atomic<bool> m_running;

  // Used to keep track of task queue status
  std::condition_variable m_consumerCondition;

  // Used to keep track of task queue status
  std::condition_variable m_producerCondition;

  // Container for the threads in the thread pool
  std::vector<std::thread> m_threads;

  // Container for the tasks to be completed by the thread pool workers
  std::deque<TaskObject> m_tasks;

  // Number of worker threads in the thread pool
  std::atomic<unsigned int> m_numberOfWorkerThreads;

  // Whether or not the number of workers is strictly enforced (cannot add
  // additional if needed)
  std::atomic<bool> m_addMoreWorkersIfNeeded;

  std::unordered_map<std::thread::id, bool> m_threadActivityMapper;
  std::atomic<int> m_threadsToRemove;
  std::unique_ptr<std::thread> m_deleterThread;
  std::condition_variable m_deleterCondition;

  void addThreadToPool() {
    Logd("SS", std::string("ENTRY: ") + std::string(__FUNCTION__));
    if (Constants::maximumNumberOfWorkers == 0 ||
        numberOfThreads() < Constants::maximumNumberOfWorkers) {
      Logd("SS", "Attempting to add new thread to pool");
      auto runningState = static_cast<bool>(m_running);
      m_threads.emplace_back(std::thread([this, runningState]() mutable {

        m_threadActivityMapper[std::this_thread::get_id()] = runningState;

        while (runningState) {

          Logsd("SS") << "In thread: " << std::this_thread::get_id()
                         << "- pre lock";

          std::unique_lock<std::mutex> lock(m_mutex);

          Logsd("SS") << "In thread: " << std::this_thread::get_id()
                         << "- post lock";

          if (m_consumerCondition.wait_for(
                  lock, Constants::threadInactiveDurationToTriggerDeletion,
                  [this]() {
                    Logd("SS", "Checking consumer condition");
                    return !m_tasks.empty() || !m_running;
                  })) {
            // Exited early
            Logd("SS", "Consumer was notified - consumer has the lock!");
            if (!m_tasks.empty() && m_running) {

              Logd("SS", "m_tasks.size()=" + std::to_string(m_tasks.size()) +
                                " (before pop)\n");
              auto taskObject = std::move(m_tasks.front());
              m_tasks.pop_front();
              // When the worker consumes a task it needs
              // to notify consumer that it did consume a task
              lock.unlock();
              Logsd("SS") << "Notify_all - Thread consumed a task";
              m_producerCondition.notify_all();
              taskObject.task();
            }

          } else {
            // Timed out
            // Stop the thread and mark it for deletion
            Logd("SS", "Thread timed out -- will mark it self for deletion");
            runningState = false;
            m_threadActivityMapper[std::this_thread::get_id()] = runningState;
            m_threadsToRemove++;
          }
        }
        Logt("SS", "Thread in thread pool stopped");
        Logt("SS", "Notify deleter thread");
        m_deleterCondition.notify_one();
      }));
      m_numberOfWorkerThreads++;
      Logt("SS", "Incremented number of workers: " +
                        std::to_string(m_numberOfWorkerThreads));
    } else {
      Loge("SS",
            "Unable to add more worker threads: Maximum number of allowed "
            "threads, " +
                std::to_string(Constants::maximumNumberOfWorkers) +
                ", has been reached!");
    }
    Logd("SS", std::string("EXIT: ") + std::string(__FUNCTION__));
  }

  void removeThread(std::thread::id id) {
    // std::lock_guard<std::mutex> lock(m_mutex);
    auto iter =
        std::find_if(m_threads.begin(), m_threads.end(),
                     [=](std::thread &t) { return (t.get_id() == id); });
    if (iter != m_threads.end()) {
      if (iter->joinable()) {
        iter->join();
      }
      m_threads.erase(iter);
      m_threadsToRemove--;
      m_numberOfWorkerThreads--;
    }
  }

  void initialiseDeleterThread() {
    m_deleterThread = std::unique_ptr<std::thread>(new std::thread([this]() {
      while (m_running) {

        std::unique_lock<std::mutex> lock(m_mutex);
        m_deleterCondition.wait(lock, [this]() {
          Logd("SS", "Checking deleter condition");
          return m_threadsToRemove > 0 || !m_running;
        });

        for (auto it : m_threadActivityMapper) {
          bool active = it.second;
          auto id = it.first;
          if (!active) {
            removeThread(id);
            Logsd("SS") << "Removed thread: " << id << "\n";
            m_threadActivityMapper.erase(id);
          }
        }
      }
    }));
  }

public:
  
  ThreadPool(unsigned int numberOfWorkers = 1)
      : m_running(true), m_numberOfWorkerThreads(0),
        m_addMoreWorkersIfNeeded(Constants::addMoreWorkersIfNeeded) {
    Logd("SS", std::string("ENTRY: ") + std::string(__FUNCTION__));
    static_assert(Constants::maximumNumberOfWorkers >= 0,
                  "Maximum number of thread pool workers must be greater than "
                  "or equal to 0 (0 = unlimited)");

    // Deleter thread
    if (m_addMoreWorkersIfNeeded) {
      initialiseDeleterThread();
    }

    // Number of workers
    auto workers = std::min(numberOfWorkers, Constants::maximumNumberOfWorkers);
    if (workers != 0 && workers != numberOfWorkers) {
      Loge("SS",
            "The number of requested threads in thread pool, " +
                std::to_string(numberOfWorkers) +
                " is larger than the maximum allowed number of workers, " +
                std::to_string(Constants::maximumNumberOfWorkers) +
                ". Number of workers is set to maximum allowed value!");
    }

    // Add the workers
    for (int i = 0; i < workers; ++i) {
      addThreadToPool();
    }
    Logd("SS", std::string("EXIT: ") + std::string(__FUNCTION__));
  }

  ~ThreadPool() {
    m_running = false;
    m_producerCondition.notify_all();
    m_consumerCondition.notify_all();
    m_deleterCondition.notify_all();

    for (auto &&t : m_threads) {
      if (t.joinable()) {
        t.join();
      }
    }

    if (m_deleterThread->joinable()) {
      m_deleterThread->join();
    }
  }

  auto numberOfThreads() -> unsigned int { return m_numberOfWorkerThreads; }

  // Disable copy constructor, move constructor and copy assignment operators
  ThreadPool(const ThreadPool &other) = delete;
  ThreadPool &operator=(ThreadPool other) = delete;
  ThreadPool(ThreadPool &&other) = delete;
  ThreadPool &operator=(ThreadPool &&other) = delete;

  void clearTasks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks = {}; // Empty the queue
  }

  void addTask(TaskType &&task) {
    if (Constants::asynchAddTaskToThreadPool) {
      // Need to spawn another thread to not hold the calling thread waiting
      std::thread t([this, task]() mutable {
        // M800::CPCore::THREAD_MAP[std::this_thread::get_id()] = "addTask";
        // M800::CPCore::THREAD_NUMBER_MAP[std::this_thread::get_id()] =
        // M800::CPCore::THREAD_COUNT_MAP["addTask"];
        // M800::CPCore::THREAD_COUNT_MAP["addTask"]++;
        addTaskHelper(std::move(task));
      });
      t.detach();
    } else {
      // Use the calling thread to add task
      addTaskHelper(std::move(task));
    }
  }

  void addTaskHelper(TaskType &&task) {
    Logd("SS", std::string("ENTRY: ") + std::string(__FUNCTION__));

    std::unique_lock<std::mutex> lock(m_mutex);
    TaskObject taskObject(std::move(task), std::this_thread::get_id());
    m_tasks.push_back(taskObject);
    auto tasks = m_tasks.size(); // Keep track of how many tasks there are
                                 // Such that we can track if the producer
                                 // did any work
    lock.unlock();
    m_consumerCondition.notify_one(); // Notify one worker thread

    // If the number of workers is not strictly enforced
    // Then, add them freely to ensure there is desired min delay
    // before tasks are executed, as long as the number of
    // workers are below the maximum allowed number.

    if (m_addMoreWorkersIfNeeded) {
      lock.lock();
      if (m_producerCondition.wait_for(
              lock, Constants::minimumThreadPoolTaskDelay,
              [this, tasks, taskObject]() {
                Logd("SS", "Checking producer condition\n");
                return std::find(m_tasks.begin(), m_tasks.end(), taskObject) ==
                           m_tasks.end() ||
                       !m_running;
              })) {
        // Exited early
        Logt("SS", "Exited task wait condition...");
      } else {
        // Timed out

        // If there are tasks to be done, and the particular task the producer
        // submitted has not been done yet. Add a worker to perform the task.
        if (!m_tasks.empty()) {
          std::ostringstream oss;
          oss << "Timed out - no thread starting work on the next task within "
              << duration_cast<milliseconds>(
                     Constants::minimumThreadPoolTaskDelay)
                     .count()
              << "ms. Adding a new worker!";
          Logt("SS", oss.str());
          // Add another worker
          Logd("SS",
                "Will add new thread: " + std::to_string(numberOfThreads()));
          addThreadToPool();
          Logd("SS",
                "After added new thread: " + std::to_string(numberOfThreads()));
          Logd("SS", "Current number of workers: " +
                            std::to_string(numberOfThreads()));
        }
      }
    }
    Logd("SS", std::string("EXIT: ") + std::string(__FUNCTION__));
  }
};
} // namespace SS

#endif /* ThreadPool_hpp */
