#ifndef ALKAID_APP_MANAGE_H
#define ALKAID_APP_MANAGE_H
#include "alkaid_storage/sys/program.h"
#include "sys_func/sys_func.h"

#include <string>
#include <vector>

#include "include/cef_task.h"

namespace alkaid::AppManage {

bool install(const std::filesystem::path &pkgFile);

bool uninstall(int64_t id);

bool update(int64_t id, const std::filesystem::path &newPkgFilePath);

Pages::PageResult<ProgramData> paging(uint32_t page, uint32_t size, const std::optional<std::string> &name);

std::optional<ProgramData> load(int64_t id);

void registerAppManageModule();

class LaunchAppTask: public CefTask {
  IMPLEMENT_REFCOUNTING(LaunchAppTask);

  int64_t appId_;

public:
  explicit LaunchAppTask(const int64_t &appId);

  void Execute() override;
};

struct AppPagingParam {
  uint32_t page;
  uint32_t size;
  std::optional<std::string> name;

  friend void from_json(const nlohmann::json &j, AppPagingParam &p) {
    if (j.contains("page")) j.at("page").get_to(p.page);
    else p.page = 1;
    if (j.contains("size")) j.at("size").get_to(p.size);
    else p.size = 10;
    if (j.contains("name")) j.at("name").get_to(p.name);
  }
};

struct InstallAppParam {
  std::string sourceDir;
  std::string name;
  std::string identifier;
  std::string programName;
  std::string programIdentifier;
  std::string programIcon;

  friend void from_json(const nlohmann::json &j, InstallAppParam &p) {
    if (j.contains("sourceDir")) j.at("sourceDir").get_to(p.sourceDir);
    else p.sourceDir = "";
    if (j.contains("name")) j.at("name").get_to(p.name);
    else p.name = "";
    if (j.contains("identifier")) j.at("identifier").get_to(p.identifier);
    else p.identifier = "";
    if (j.contains("programName")) j.at("programName").get_to(p.programName);
    else p.programName = "";
    if (j.contains("programIdentifier")) j.at("programIdentifier").get_to(p.programIdentifier);
    else p.programIdentifier = "";
    if (j.contains("programIcon")) j.at("programIcon").get_to(p.programIcon);
    else p.programIcon = "";
  }
};
} // namespace alkaid

#endif // ALKAID_APP_MANAGE_H
