#ifndef ALKAID_VARIANT_UTIL_H
#define ALKAID_VARIANT_UTIL_H

namespace alkaid::VariantUtil {
template<class... Ts>
struct Overloaded: Ts... {
  using Ts::operator()...;
};
}

#endif //ALKAID_VARIANT_UTIL_H
