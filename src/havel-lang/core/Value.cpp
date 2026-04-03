/*
 * Value.cpp
 *
 * Core value type implementation.
 */
#include "Value.hpp"
#include <iomanip>
#include <sstream>

namespace havel::core {

std::string Value::toString() const {
  if (isNull())
    return "null";
  if (isBool())
    return asBool() ? "true" : "false";
  if (isInt())
    return std::to_string(asInt());
  if (isDouble()) {
    std::ostringstream oss;
    oss << std::setprecision(10) << asDouble();
    return oss.str();
  }
  if (isStringId())
    return "<string:" + std::to_string(asStringId()) + ">";
  if (isObjectId())
    return "<object:" + std::to_string(asObjectId()) + ">";
  if (isClosureId())
    return "<closure:" + std::to_string(asClosureId()) + ">";
  if (isArrayId())
    return "<array:" + std::to_string(asArrayId()) + ">";
  if (isSetId())
    return "<set:" + std::to_string(asSetId()) + ">";
  if (isRangeId())
    return "<range:" + std::to_string(asRangeId()) + ">";
  if (isStructId())
    return "<struct:" + std::to_string(asStructId()) + ">";
  if (isClassId())
    return "<class:" + std::to_string(asClassId()) + ">";
  if (isEnumId())
    return "<enum:" + std::to_string(asEnumId()) + ">";
  if (isIteratorId())
    return "<iterator:" + std::to_string(asIteratorId()) + ">";
  if (isHostFuncId())
    return "<hostfunc:" + std::to_string(asHostFuncId()) + ">";
  if (isLazyPipelineId())
    return "<pipeline:" + std::to_string(asLazyPipelineId()) + ">";
  if (isErrorId())
    return "<error:" + std::to_string(asErrorId()) + ">";
  if (isFunctionObjId())
    return "<funcobj:" + std::to_string(asFunctionObjId()) + ">";
  if (isStringValId())
    return "<stringval:" + std::to_string(asStringValId()) + ">";
  if (isPtr())
    return "<ptr:" + std::to_string(reinterpret_cast<uint64_t>(asPtr())) + ">";
  return "<unknown>";
}

} // namespace havel::core
