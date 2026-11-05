#include "app_manage.h"

#include "alkaid_storage/sys/organization.h"
#include "alkaid_storage/sys/program.h"
#include "alkaid_utils/file_util.h"
#include "alkaid_utils/local_date_time.h"
#include "alkaid_utils/process_util.h"
#include "yao_pkg.h"
#include "alkaid_utils/crypto_util.h"
#include "alkaid_utils/module_registry.h"
#include "client/client.h"
#include "include/cef_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "window/browser_view_delegate.h"
#include "window/window_delegate.h"

namespace alkaid::AppManage {
bool install(const std::filesystem::path &pkgFile) {
  auto res = OrganizationMapper::getMapper().transaction<bool>([&pkgFile](std::shared_ptr<Database> db)-> tl::expected<bool, std::string> {
      const YaoPkg yaoPkg(pkgFile.string());
      auto [name, identifier, publicKey] = yaoPkg.getProvider();
      auto [programName, programIdentifier, icon] = yaoPkg.getProgram();
      OrganizationMapper organizationMapper(db);

      auto organization = organizationMapper.getOne(SQL::Where().eq_("identifier", identifier));
      if (!organization) {
        LOGGER_DEBUG("组织不存在");
        return false;
      }
      int64_t orgId = organization->id;
      ProgramMapper programMapper(db);
      if (programMapper.exists(SQL::Where()
        .eq_("a.org_id", orgId)
        .eq_("a.identifier", programIdentifier)
      )) {
        LOGGER_DEBUG("应用已存在");
        return true;
      }

      ProgramEntity program;
      program.orgId = orgId;
      program.identifier = programIdentifier;
      program.icon = icon;
      program.name = programName;
      program.multiple = true;
      program.installTime = LocalDateTime::now().format();
      program.updateTime = program.installTime;
      programMapper.insert(program);

      // 复制文件到 home/app/orgIdentifier/programIdentifier/programIdentifier.yao
      auto installPath = ProcessUtil::homeDir() / APP_PATH / identifier / programIdentifier;

      return FileUtil::mkdir(installPath) && FileUtil::move(pkgFile, installPath / (programIdentifier + ".yao"));
    }
  );

  if (res) return *res;
  return false;
}

bool uninstall(const int64_t id) {
  const auto programMapper = ProgramMapper::getMapper();
  const auto programInfo = programMapper.getById(id);
  if (!programInfo) return false;
  const auto rootPath = ProcessUtil::homeDir() / APP_PATH / programInfo->orgIdentifier;
  const auto pkgPath = rootPath / programInfo->programIdentifier;
  FileUtil::delDir(pkgPath, true);
  const auto organizationMapper = OrganizationMapper::getMapper();
  if (!programMapper.exists(SQL::Where().eq_("a.org_id", programInfo->orgId))) {
    organizationMapper.removeById(programInfo->orgId);
    FileUtil::delDir(rootPath, true);
  }
  return programMapper.removeById(id);
}

bool update(const int64_t id, const std::filesystem::path &newPkgFilePath) {
  const auto programMapper = ProgramMapper::getMapper();
  const auto programInfo = programMapper.getById(id);
  if (!programInfo) return false;

  const YaoPkg yaoPkg(newPkgFilePath.string());
  auto [name, identifier, publicKey] = yaoPkg.getProvider();
  auto [programName, programIdentifier, icon] = yaoPkg.getProgram();

  if (programInfo->orgIdentifier != identifier || programInfo->programIdentifier != programIdentifier) return false;

  ProgramEntity program;
  program.id = id;
  program.icon = icon;
  program.name = programName;
  program.updateTime = LocalDateTime::now().format();
  programMapper.updateById(program);

  const auto installPath = ProcessUtil::homeDir() / APP_PATH / identifier / programIdentifier;

  return FileUtil::mkdir(installPath) && FileUtil::copy(newPkgFilePath, installPath / (programIdentifier + ".yao"));
}

Pages::PageResult<ProgramData> paging(const uint32_t page, const uint32_t size, const std::optional<std::string> &name) {
  return ProgramMapper::getMapper().page(Pages::PageParam(size, page),
    SQL::Where()
    .if_(name.has_value() && !name.value().empty(), [&name](auto &it) {
        it.like_("a.name", name);
      }
    )
  );
}

std::optional<ProgramData> load(const int64_t id) {
  return ProgramMapper::getMapper().getById(id);
}

void registerAppManageModule() {
  ModuleRegistry::registerModule({
    "AppManage",
    "",
    {"Log"},
    []() -> tl::expected<void, std::string> {
      SysFunc::registerSysInvoke(
        "AppPaging",
        [](const SysInvokeParam &param) -> SysInvokeResult {
          LOGGER_DEBUG("分页查询应用列表");
          const auto [page, size, name] = param.get<AppPagingParam>();
          const nlohmann::json pagingResult = paging(page, size, name);
          return SysInvokeResult::ok(pagingResult);
        }
      );

      SysFunc::registerSysInvoke(
        "LaunchApp",
        [](const SysInvokeParam &param) -> SysInvokeResult {
          LOGGER_DEBUG("正在启动应用...");
          auto appId = param.get<int64_t>();
          if (CefPostTask(TID_UI, new LaunchAppTask(appId))) return SysInvokeResult::ok();
          LOGGER_DEBUG("应用启动失败：{}", appId);
          return SysInvokeResult::fail("应用启动失败");
        }
      );
      SysFunc::registerSysInvoke(
        "InstallApp",
        [](const SysInvokeParam &param) -> SysInvokeResult {
          const auto installAppParam = param.get<InstallAppParam>();
          auto mapper = OrganizationMapper::getMapper();
          auto data = mapper.getOne(SQL::Where().eq_("identifier", installAppParam.identifier));
          if (!data) return SysInvokeResult::fail("应用安装失败");
          const auto bkgPath = installAppParam.sourceDir + ".yao";
          YaoPkg yaoPkg(
            installAppParam.sourceDir,
            bkgPath,
            installAppParam.name,
            installAppParam.identifier,
            installAppParam.programName,
            installAppParam.programIdentifier,
            installAppParam.programIcon,
            data->publicKey,
            data->privateKey);
          if (install(bkgPath)) return SysInvokeResult::ok();
          return SysInvokeResult::fail("应用安装失败");
        }
      );
      SysFunc::registerSysInvoke(
        "UninstallApp",
        [](const SysInvokeParam &param) -> SysInvokeResult {
          LOGGER_DEBUG("正在卸载应用...");
          auto appId = param.get<int64_t>();
          if (uninstall(appId)) return SysInvokeResult::ok();
          LOGGER_DEBUG("应用卸载失败：{}", appId);
          return SysInvokeResult::fail("应用卸载失败");
        }
      );
      return {};
    },
    [] {}
  });
}

LaunchAppTask::LaunchAppTask(const int64_t &appId) : appId_(appId) {}

void LaunchAppTask::Execute() {
  auto programInfo = AppManage::load(appId_);
  if (!programInfo) {
    LOGGER_ERROR("应用不存在：{}", appId_);
    return;
  }
  const std::string scheme = RESOURCES_SCHEME;
  const auto url = scheme + "://" + programInfo->orgIdentifier + "-" + programInfo->programIdentifier;
  CefBrowserSettings settings;
  settings.windowless_frame_rate = 60;
  const CefRefPtr<CefClient> client(new Client());
  const CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
    client, url, settings, nullptr, nullptr, new BrowserViewDelegate());
  // 普通应用窗口，标记 isDesktop = false
  CefWindow::CreateTopLevelWindow(new WindowDelegate(browser_view, url, false));
  LOGGER_DEBUG("应用启动成功：{}", programInfo->programName);
}

} // namespace alkaid
