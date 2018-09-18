#ifndef Constants_hpp
#define Constants_hpp

#include <chrono>
#include <stdio.h>
#include <string>

namespace STP {
namespace Constants {

using namespace std::chrono;

// This ensures that this minimum delay before a task is executed is enforced
static constexpr milliseconds minimumThreadPoolTaskDelay{25};

// If workers are added as needed, based on the minimum allowed delay
// this value restricts the maximum possible workers that can be added this way
//
// Select 0 for unrestricted number of possible workers
//
// CAUTION: Thread overload avoidance is NOT guaranteed by the thread pool,
// i.e. creating to many could overwhelm the system and crash ALL threads
// (crashes are unlikely to occur unless hundreds or thousands of threads are
// created)
//
// Therefore a conservative maximum (depending on the expected tasks and their
// frequency, runtime) is recommended as opposed to unlimited.
static constexpr unsigned int maximumNumberOfWorkers{10};

// This determines whether the number of worker threads (consumers) in the
// thread pool can increase to ensure that the <minimumThreadPoolTaskDelay>
// (above) is not exceeded before a task is executed.
//
// addMoreWorkersIfNeeded = false
//
//      NOTE:   The value of <asynchAddTaskToThreadPool> below does influence
//      the runtime performance
//              Because the thread pool will not check if additional consumer
//              threads are needed to meet the deadline set by
//              <minimumThreadPoolTaskDelay>.
//
// addMoreWorkersIfNeeded = true
//
//      NOTE:   The value of <asynchAddTaskToThreadPool> below determines
//      whether the process of adding tasks
//              is executed asynchronously or not. This is required because the
//              thread adding the task has to wait at most
//              <minimumThreadPoolTaskDelay> to see if the task has been
//              consumed by a worker yet or not.
static constexpr bool addMoreWorkersIfNeeded{true};

static constexpr int numberOfInitialWorkers{3};

// The feature to add more workers in the thread pool of the task scheduler (if
// enabled) allows the number of workers to increase to meet the maximum delay
// before a task is started by a consumer thread, as specified by
// <minimumThreadPoolTaskDelay> (above).
//
// NOTE:    If addMoreWorkersIfNeeded is set to false, the value of this
//          variable does not influence the behaviour of the thread pool.
//
// asynchAddTaskToThreadPool = false
//
//      The calling thread will deal with adding the task.
//      This means that this operation is no longer asynchronous!!
//      Meaning that the calling thread may need to wait
//      <minimumThreadPoolTaskDelay> until it exits (or shorter if the consumer
//      thread can consume the task)
//
//      The advantage is that it eliminates N thread creations for N tasks that
//      need to be executed Therefore if the tasks that need to be added are not
//      very time sensitive AND <minimumThreadPoolTaskDelay> is very small (<
//      100 ms). This may be a resonable option to decrease performance
//      overhead, IF variable number of consumer threads are wanted. But for the
//      price of synchronous behaviour when adding tasks to the pool.
//
// asynchAddTaskToThreadPool = true
//
//      The process of adding a new task to the thread pool is executed by a new
//      thread.
//
//      Therefore the calling thread (adding the task) does not need to wait and
//      see if an additional worker thread is needed.
static constexpr bool asynchAddTaskToThreadPool{false};

// Given that addMoreWorkersIfNeeded = true
//
// After a thread has been inactive more the specified period of time below
// the thread will be removed.
static constexpr seconds threadInactiveDurationToTriggerDeletion{10};

} // namespace Constants
} // namespace STP

#endif /* Constants_hpp */
