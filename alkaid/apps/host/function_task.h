/** @file function_task.h 将可复制闭包投递到 CEF 指定线程。 */
#ifndef ALKAID_FUNCTION_TASK_H
#define ALKAID_FUNCTION_TASK_H
#include "include/cef_task.h"
#include <functional>
namespace alkaid {
class FunctionTask : public CefTask {
  IMPLEMENT_REFCOUNTING(FunctionTask);
public:
  explicit FunctionTask(std::function<void()> action) : action_(std::move(action)) {}
  void Execute() override { action_(); }
private:
  std::function<void()> action_;
};
}
#endif // ALKAID_FUNCTION_TASK_H
