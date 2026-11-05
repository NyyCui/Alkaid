#ifndef ALKAID_TEST_H
#define ALKAID_TEST_H

#include "alkaid_storage/sys/organization.h"
#include "alkaid_storage/sys/program.h"
#include "alkaid_utils/crypto_util.h"
#include "alkaid_utils/exceptions.h"
#include "app_manage/app_manage.h"
#include "app_manage/yao_pkg.h"

namespace alkaid {
inline void t1() {
  auto key = CryptoUtil::generateSigningKey();
  auto mapper = OrganizationMapper::getMapper();
  OrganizationEntity entity;
  entity.name = "强强集团";
  entity.identifier = "qq-jt.com";
  entity.publicKey = key.publicKey;
  entity.privateKey = key.privateKey;
  entity.registerTime = LocalDateTime::now().format();
  mapper.insert(entity);
  YaoPkg yaoPkg1(
                 R"(D:\DaiMa\Clion\alkaid\tests\html\desktop)",
                 R"(D:\DaiMa\Clion\alkaid\tests\html\desktop.yao)",
                 "强强集团",
                 "qq-jt.com",
                 "桌面",
                 "desktop",
                 "default",
                 key.publicKey,
                 key.privateKey);

  YaoPkg yaoPkg2(
                 R"(D:\DaiMa\Clion\alkaid\tests\html\chat)",
                 R"(D:\DaiMa\Clion\alkaid\tests\html\chat.yao)",
                 "强强集团",
                 "qq-jt.com",
                 "聊天室",
                 "chat",
                 "default",
                 key.publicKey,
                 key.privateKey);

  AppManage::install(R"(D:\DaiMa\Clion\alkaid\tests\html\desktop.yao)");
  AppManage::install(R"(D:\DaiMa\Clion\alkaid\tests\html\chat.yao)");
}

inline void t2() {

  auto mapper = ProgramMapper::getMapper();
  ProgramEntity entity;
  entity.orgId = 238973740212424704LL;
  entity.identifier = "111",
    entity.icon = "default";
  entity.name = "default";
  entity.multiple = false;
  entity.installTime = LocalDateTime::now().format();
  entity.updateTime = LocalDateTime::now().format();
  std::vector entities({entity, entity, entity});
  int32_t insert2 = mapper.insert(entities, Mapper::IdType::ASSIGN, 1);
  int32_t insert1 = mapper.insert(entity);
  int32_t update1 = mapper.updateById(entity);
  int32_t update2 = mapper.update(entity, SQL::Where().eq_("id", 240773305982914560LL));
  int32_t update3 = mapper.updateById(entities);
  auto remove1 = mapper.removeById(240773305982914560LL);
  auto data1 = mapper.getById(240773305982914560LL);
  auto data2 = mapper.getOne(SQL::Where().eq_("a.id", 240773305982914560LL));
  auto data3 = mapper.list();
  auto data4 = mapper.page(Pages::PageParam(), SQL::Where().eq_("a.org_id", 238973740212424704LL));

  LOGGER_DEBUG("insert1：{}", insert1);
  LOGGER_DEBUG("insert2：{}", insert2);
  LOGGER_DEBUG("update1：{}", update1);
  LOGGER_DEBUG("update2：{}", update2);
  LOGGER_DEBUG("update3：{}", update3);
  LOGGER_DEBUG("remove1：{}", remove1);
  LOGGER_DEBUG("查询结果1：{}", nlohmann::json(data1).dump());
  LOGGER_DEBUG("查询结果2：{}", nlohmann::json(data2).dump());
  LOGGER_DEBUG("查询结果3：{}", nlohmann::json(data3).dump());
  LOGGER_DEBUG("查询结果4：{}", nlohmann::json(data4).dump());

}

inline void t3() {
  auto param = SysInvokeParam::create("3");

  auto sss = param.param.get<int32_t>();
  LOGGER_DEBUG("sss:{}", sss);
}

inline void t4() {
  auto key = CryptoUtil::generateSigningKey();
  auto mapper = OrganizationMapper::getMapper();
  OrganizationEntity entity;
  entity.name = "强强集团";
  entity.identifier = "qq-jt.com";
  entity.publicKey = key.publicKey;
  entity.privateKey = key.privateKey;
  entity.registerTime = LocalDateTime::now().format();
  mapper.insert(entity);
}

inline void t5() {
  AppManage::InstallAppParam installAppParam{
    R"(D:\DaiMa\Clion\alkaid\tests\html\desktop)",
    "强强集团",
    "qq-jt.com",
    "桌面",
    "desktop",
    "default",
  };
  auto mapper = OrganizationMapper::getMapper();
  auto data = mapper.getOne(SQL::Where().eq_("identifier", installAppParam.identifier));
  if (!data) {
    LOGGER_DEBUG("数据不存在");
  }

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
  bool install = AppManage::install(bkgPath);
  LOGGER_DEBUG("install:{}", install);
}

inline void t6() {
  AppManage::InstallAppParam installAppParam{
    R"(D:\DaiMa\Clion\alkaid\tests\html\chat)",
    "强强集团",
    "qq-jt.com",
    "聊天室",
    "chat",
    "default",
  };
  auto mapper = OrganizationMapper::getMapper();
  auto data = mapper.getOne(SQL::Where().eq_("identifier", installAppParam.identifier));
  if (!data) {
    LOGGER_DEBUG("数据不存在");
  }

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
  bool install = AppManage::install(bkgPath);
  LOGGER_DEBUG("install:{}", install);
}

inline void t() {
  try {
    // t1();
  } catch (...) {
    LOGGER_ERROR("测试函数执行失败");
  }
}
} // namespace alkaid

#endif // ALKAID_TEST_H
