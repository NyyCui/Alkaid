#ifndef ALKAID_SYS_EVENT_H
#define ALKAID_SYS_EVENT_H

#include <string>

namespace alkaid {

namespace SysEvent {
enum Event {
  BeforeAppDestroy = 0,
};

inline const std::vector<std::string> &getSysEventNames() {
  static const std::vector<std::string> sysEventNames = {"BeforeAppDestroy"};
  return sysEventNames;
}

}

} // namespace alkaid

#endif // ALKAID_SYS_EVENT_H
