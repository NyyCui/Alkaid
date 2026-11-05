/** @file package_verifier.h 有界 ZIP、签名清单和逐文件完整性校验。 */
#ifndef ALKAID_PACKAGE_VERIFIER_H
#define ALKAID_PACKAGE_VERIFIER_H
#include "package_types.h"

namespace alkaid {
class PackageVerifier {
public:
  static RuntimeResult<std::shared_ptr<const VerifiedPackage>> read(const std::filesystem::path &path);
  static RuntimeResult<std::shared_ptr<const VerifiedPackage>> verify(std::string bytes);
};
} // namespace alkaid
#endif // ALKAID_PACKAGE_VERIFIER_H
