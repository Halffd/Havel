#include "VM.hpp"
#include "VMInternals.hpp"
#include "../../../utils/Logger.hpp"
#include "../../utils/ErrorPrinter.hpp"

#include <sstream>
#include <regex>
#include <unordered_set>

namespace havel::compiler {

std::string VM::toString(const Value &value) {
 std::unordered_set<uint32_t> visited;
 return toStringInternal(value, visited, 0);
}

const std::string* VM::getStringPtr(const Value &value) const {
  if (value.isStringId()) {
    return heap_.string(value.asStringId());
  }
  if (value.isStringValId() && current_chunk) {
    return &current_chunk->getString(value.asStringValId());
  }
  return nullptr;
}

bool VM::toBoolPublic(const Value &value) {
  return isTruthy(value);
}

std::string VM::toStringInternal(const Value &value, std::unordered_set<uint32_t> &visitedIds, int depth) {
  if (depth > 8) return "...";

  if (value.isNull()) return "null";
  if (value.isBool()) return value.asBool() ? "true" : "false";
  if (value.isInt()) return std::to_string(value.asInt());
  if (value.isDouble()) {
    std::ostringstream out;
    out << value.asDouble();
    return out.str();
  }
  if (value.isStringValId()) {
    if (current_chunk) {
      return current_chunk->getString(value.asStringValId());
    }
    return "<string:" + std::to_string(value.asStringValId()) + ">";
  }
  if (value.isRegexValId()) {
    if (current_chunk) {
      return "r\"" + current_chunk->getString(value.asRegexValId()) + "\"";
    }
    return "<regex:" + std::to_string(value.asRegexValId()) + ">";
  }
  if (value.isStringId()) {
    if (auto *s = heap_.string(value.asStringId())) {
      return *s;
    }
    return "<string:" + std::to_string(value.asStringId()) + ">";
  }
  if (value.isFunctionObjId()) {
    // Function object from bytecode - use metadata for display
    if (current_chunk) {
      uint32_t idx = value.asFunctionObjId();
      if (idx < current_chunk->getFunctionCount()) {
        const auto *bf = current_chunk->getFunction(idx);
        if (bf) {
          std::string result = "<fn " + bf->name;
          if (!bf->param_names.empty()) {
            result += "(";
            for (size_t i = 0; i < bf->param_names.size(); ++i) {
              if (i > 0) result += ", ";
              result += bf->param_names[i];
            }
            result += ")";
          }
          result += ">";
          return result;
        }
      }
    }
    return "<fn:" + std::to_string(value.asFunctionObjId()) + ">";
  }
  if (value.isClosureId()) {
    // Closure - use metadata for display
    auto *closure = heap_.closure(value.asClosureId());
    if (closure && current_chunk) {
      if (closure->function_index < current_chunk->getFunctionCount()) {
        const auto *bf = current_chunk->getFunction(closure->function_index);
        if (bf) {
          std::string result = "<fn " + bf->name;
          if (!bf->param_names.empty()) {
            result += "(";
            for (size_t i = 0; i < bf->param_names.size(); ++i) {
              if (i > 0) result += ", ";
              result += bf->param_names[i];
            }
            result += ")";
          }
          result += ">";
          return result;
        }
      }
    }
    return "<closure:" + std::to_string(value.asClosureId()) + ">";
  }
  if (value.isArrayId()) {
    auto *arr = heap_.array(value.asArrayId());
    if (!arr) return "[]";
    std::string result = "[";
    for (size_t i = 0; i < arr->size(); ++i) {
      if (i > 0) result += ", ";
      result += toStringInternal((*arr)[i], visitedIds, depth + 1);
    }
    result += "]";
    return result;
  }
  if (value.isObjectId()) {
    auto *obj = heap_.object(value.asObjectId());
if (!obj) return "{}";
if (visitedIds.count(value.asObjectId())) return "{...}";
visitedIds.insert(value.asObjectId());

auto* isClass = obj->get("__is_class");
auto* isStruct = obj->get("__is_struct");

if (isClass && isClass->isBool() && isClass->asBool()) {
auto* nameVal = obj->get("__name");
std::string className = (nameVal && nameVal->isStringValId() && current_chunk)
? current_chunk->getString(nameVal->asStringValId()) : "class";
visitedIds.erase(value.asObjectId());
return "<class " + className + ">";
}
if (isStruct && isStruct->isBool() && isStruct->asBool()) {
auto* nameVal = obj->get("__name");
std::string structName = (nameVal && nameVal->isStringValId() && current_chunk)
? current_chunk->getString(nameVal->asStringValId()) : "struct";
visitedIds.erase(value.asObjectId());
return "<struct " + structName + ">";
}

 Value* classOrStruct = obj->get("__class");
 if (!classOrStruct) classOrStruct = obj->get("__struct");

 if (classOrStruct && classOrStruct->isObjectId()) {
 auto* proto = heap_.object(classOrStruct->asObjectId());
 while (proto) {
		for (const char *name : {"op_toString", "toString"}) {
			auto* methodVal = proto->get(name);
			if (methodVal && (methodVal->isFunctionObjId() || methodVal->isClosureId() || methodVal->isHostFuncId())) {
				visitedIds.erase(value.asObjectId());
				try {
					Value result = callFunctionSync(*methodVal, {value});
					visitedIds.insert(value.asObjectId());
					if (result.isStringValId() && current_chunk) {
						return current_chunk->getString(result.asStringValId());
					} else if (result.isStringId()) {
						if (auto* s = heap_.string(result.asStringId())) return *s;
					} else if (result.isInt()) {
						return std::to_string(result.asInt());
					} else if (result.isDouble()) {
						std::ostringstream out;
						out << result.asDouble();
						return out.str();
					} else if (result.isBool()) {
						return result.asBool() ? "true" : "false";
					} else if (!result.isNull()) {
						return toStringInternal(result, visitedIds, depth + 1);
					}
				} catch (...) {
				}
				visitedIds.insert(value.asObjectId());
				break;
			}
		}
 auto* parentVal = proto->get("__parent");
 if (parentVal && parentVal->isObjectId()) {
 proto = heap_.object(parentVal->asObjectId());
 } else {
 break;
 }
 }
 }

 std::string result = "{";
 bool first = true;
 for (const auto &[key, val] : *obj) {
 if (key.size() >= 2 && key[0] == '_' && key[1] == '_') {
 continue;
 }
 if (!first) result += ", ";
 first = false;
 result += key + ": " + toStringInternal(val, visitedIds, depth + 1);
 }
 result += "}";
 return result;
  }
    if (value.isHostFuncId()) {
        uint32_t idx = value.asHostFuncId();
        if (idx < host_function_names_.size()) {
            return "<fn " + host_function_names_[idx] + ">";
        }
        return "<fn:" + std::to_string(idx) + ">";
    }
    if (value.isSetId()) {
        auto *set = heap_.set(value.asSetId());
        if (!set) return "{}";
        std::string result = "{";
    bool first = true;
    for (const auto &pair : *set) {
      if (!first) result += ", ";
      first = false;
      result += pair.first;
    }
        result += "}";
        return result;
    }
    if (value.isEnumId()) {
        uint32_t enumId = value.asEnumId();
        uint32_t typeId = value.asEnumTypeId();
        auto it = heap_.enums_.find(enumId);
        if (it == heap_.enums_.end()) return "<enum:invalid>";
        uint32_t tag = it->second.first;
        const auto &payload = it->second.second;
        std::string typeName = (typeId < heap_.enumTypes_.size()) ? heap_.enumTypes_[typeId].name : "enum";
        std::string variantName = (typeId < heap_.enumTypes_.size() && tag < heap_.enumTypes_[typeId].variantNames.size())
            ? heap_.enumTypes_[typeId].variantNames[tag] : std::to_string(tag);
        std::string result = typeName + "." + variantName;
        if (!payload.empty()) {
            result += "(";
            for (size_t i = 0; i < payload.size(); ++i) {
                if (i > 0) result += ", ";
                result += toStringInternal(payload[i], visitedIds, depth + 1);
            }
            result += ")";
        }
        return result;
    }
    return "unknown";
}

// Type conversion helpers
int64_t VM::toInt(const Value &value) const {
    if (value.isInt()) {
        return value.asInt();
    }
    if (value.isDouble()) {
        return static_cast<int64_t>(value.asDouble());
    }
    if (value.isBool()) {
        return value.asBool() ? 1 : 0;
    }
    if (value.isStringValId()) {
        if (current_chunk) {
            try {
                return std::stoll(current_chunk->getString(value.asStringValId()));
            } catch (...) {
                return 0;
            }
        }
    }
    if (value.isStringId()) {
        if (auto *s = heap_.string(value.asStringId())) {
            try {
                return std::stoll(*s);
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

double VM::toFloat(const Value &value) const {
    if (value.isDouble()) {
        return value.asDouble();
    }
    if (value.isInt()) {
        return static_cast<double>(value.asInt());
    }
    if (value.isBool()) {
        return value.asBool() ? 1.0 : 0.0;
    }
    if (value.isStringValId()) {
        if (current_chunk) {
            try {
                return std::stod(current_chunk->getString(value.asStringValId()));
            } catch (...) {
                return 0.0;
            }
        }
    }
    if (value.isStringId()) {
        if (auto *s = heap_.string(value.asStringId())) {
            try {
                return std::stod(*s);
            } catch (...) {
                return 0.0;
            }
        }
    }
    return 0.0;
}

bool VM::toBool(const Value &value) const {
    if (value.isBool()) {
        return value.asBool();
    }
    if (value.isInt()) {
        return value.asInt() != 0;
    }
    if (value.isDouble()) {
        return value.asDouble() != 0.0;
    }
    if (value.isStringValId()) {
        if (current_chunk) {
            return !current_chunk->getString(value.asStringValId()).empty();
        }
        return true;
    }
    if (value.isStringId()) {
        if (auto *s = heap_.string(value.asStringId())) {
            return !s->empty();
        }
        return true;
    }
    if (value.isRegexValId()) {
        return true;
    }
  // Collections: JavaScript truthiness (all collections are truthy, even empty)
  if (value.isArrayId() || value.isObjectId() || value.isSetId()) {
    return true;
  }
  return !value.isNull();
}

std::optional<std::string> VM::valueAsString(const Value &value) const {
  if (value.isStringValId() || value.isRegexValId()) {
    if (!current_chunk) {
      return std::nullopt;
    }
    uint32_t id = value.isRegexValId() ? value.asRegexValId() : value.asStringValId();
    return current_chunk->getString(id);
  }
  if (value.isStringId()) {
    if (auto *s = heap_.string(value.asStringId())) {
      return *s;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

bool VM::valuesEqualDeep(const Value &left, const Value &right) const {
  std::unordered_set<uint64_t> visited_array_pairs;
  std::unordered_set<uint64_t> visited_object_pairs;
  return valuesEqualDeep(left, right, visited_array_pairs, visited_object_pairs);
}

bool VM::valuesEqualDeep(
    const Value &left, const Value &right,
    std::unordered_set<uint64_t> &visited_array_pairs,
    std::unordered_set<uint64_t> &visited_object_pairs) const {
  if (left.isNull() || right.isNull()) {
    return left.isNull() && right.isNull();
  }

  if ((left.isInt() || left.isDouble()) && (right.isInt() || right.isDouble())) {
    const double l = left.isInt() ? static_cast<double>(left.asInt()) : left.asDouble();
    const double r = right.isInt() ? static_cast<double>(right.asInt()) : right.asDouble();
    return l == r;
  }

  if (left.isBool() && right.isBool()) {
    return left.asBool() == right.asBool();
  }

  if (auto l = valueAsString(left); l.has_value()) {
    auto r = valueAsString(right);
    return r.has_value() && (*l == *r);
  }
  if (left.isStringValId() || left.isStringId() || left.isRegexValId() ||
      right.isStringValId() || right.isStringId() || right.isRegexValId()) {
    return false;
  }

  if (left.isArrayId() && right.isArrayId()) {
    const uint32_t l_id = left.asArrayId();
    const uint32_t r_id = right.asArrayId();
    if (l_id == r_id) {
      return true;
    }

    const uint32_t lo = std::min(l_id, r_id);
    const uint32_t hi = std::max(l_id, r_id);
    const uint64_t pair = (static_cast<uint64_t>(lo) << 32) | hi;
    if (!visited_array_pairs.insert(pair).second) {
      return true;
    }

    const auto *l_arr = heap_.array(l_id);
    const auto *r_arr = heap_.array(r_id);
    if (!l_arr || !r_arr) {
      return false;
    }
    if (l_arr->size() != r_arr->size()) {
      return false;
    }

    for (size_t i = 0; i < l_arr->size(); i++) {
      if (!valuesEqualDeep((*l_arr)[i], (*r_arr)[i], visited_array_pairs,
                           visited_object_pairs)) {
        return false;
      }
    }
    return true;
  }

  if (left.isObjectId() && right.isObjectId()) {
    const uint32_t l_id = left.asObjectId();
    const uint32_t r_id = right.asObjectId();
    if (l_id == r_id) {
      return true;
    }

    const uint32_t lo = std::min(l_id, r_id);
    const uint32_t hi = std::max(l_id, r_id);
    const uint64_t pair = (static_cast<uint64_t>(lo) << 32) | hi;
    if (!visited_object_pairs.insert(pair).second) {
      return true;
    }

    const auto *l_obj = heap_.object(l_id);
    const auto *r_obj = heap_.object(r_id);
    if (!l_obj || !r_obj) {
      return false;
    }
    if (l_obj->size() != r_obj->size()) {
      return false;
    }

    for (const auto &[key, l_value] : *l_obj) {
      auto it = r_obj->find(key);
      if (it == r_obj->end()) {
        return false;
      }
      if (!valuesEqualDeep(l_value, it->second, visited_array_pairs,
                           visited_object_pairs)) {
        return false;
      }
    }
    return true;
  }

  if (left.isRangeId() && right.isRangeId()) {
    return left.asRangeId() == right.asRangeId();
  }
  if (left.isClosureId() && right.isClosureId()) {
    return left.asClosureId() == right.asClosureId();
  }
  if (left.isFunctionObjId() && right.isFunctionObjId()) {
    return left.asFunctionObjId() == right.asFunctionObjId();
  }
    if (left.isHostFuncId() && right.isHostFuncId()) {
        return left.asHostFuncId() == right.asHostFuncId();
    }

    if (left.isEnumId() && right.isEnumId()) {
        if (left.asEnumId() == right.asEnumId()) return true;
        uint32_t lId = left.asEnumId();
        uint32_t rId = right.asEnumId();
        auto lIt = heap_.enums_.find(lId);
        auto rIt = heap_.enums_.find(rId);
        if (lIt == heap_.enums_.end() || rIt == heap_.enums_.end()) return false;
        if (left.asEnumTypeId() != right.asEnumTypeId()) return false;
        if (lIt->second.first != rIt->second.first) return false;
        const auto &lPayload = lIt->second.second;
        const auto &rPayload = rIt->second.second;
        if (lPayload.size() != rPayload.size()) return false;
        for (size_t i = 0; i < lPayload.size(); ++i) {
            if (!valuesEqualDeep(lPayload[i], rPayload[i], visited_array_pairs, visited_object_pairs)) return false;
        }
        return true;
    }

    if (left.isSetId() && right.isSetId()) {
      const auto *lset = heap_.set(left.asSetId());
      const auto *rset = heap_.set(right.asSetId());
      if (!lset || !rset) return false;
      if (lset->size() != rset->size()) return false;
      for (const auto &elem : *lset) {
        bool found = false;
        for (const auto &r_elem : *rset) {
          if (valuesEqualDeep(elem.second, r_elem.second, visited_array_pairs, visited_object_pairs)) {
            found = true;
            break;
          }
        }
        if (!found) return false;
      }
      return true;
    }

    return false;
}

std::optional<std::string> VM::resolveKey(const Value &value) const {
    const auto& cf = currentFrame();
    const BytecodeChunk* chunk = cf.chunk ? cf.chunk : current_chunk;
    return ::havel::compiler::keyFromValue(value, &heap_, chunk);
}
// Value utility functions
bool VM::isNull(const Value &value) const {
  return value.isNull();
}

bool VM::isTruthy(const Value &value) {
  // Step 1: null is always falsy
  if (value.isNull()) {
    return false;
  }

  // Step 2: boolean values follow their own truthiness
  if (value.isBool()) {
    return value.asBool();
  }

  // Step 3: numeric values: 0 is falsy, non-zero is truthy
  if (value.isInt()) {
    return value.asInt() != 0;
  }
  if (value.isDouble()) {
    return value.asDouble() != 0.0;
  }

  // Step 4: empty string is falsy, non-empty is truthy
  if (value.isStringValId()) {
    // TODO: string pool lookup - assume truthy for now
    return true;
  }

  // Step 5: arrays are truthy if non-empty
  if (value.isArrayId()) {
    auto *array = heap_.array(value.asArrayId());
    return array && !array->empty();
  }

  // Step 6: objects are always truthy (even if empty)
  if (value.isObjectId()) {
    return true;
  }

  // Step 7: sets are truthy if non-empty
  if (value.isSetId()) {
    // Note: Sets aren't fully implemented yet, but assume truthy if they exist
    return true;
  }

  // Step 8: functions are always truthy
    if (value.isFunctionObjId() || value.isHostFuncId() || value.isClosureId() || value.isBoundMethodId()) {
    return true;
  }

  // Default: should not reach here, but be conservative
  return false;
}

// Duration parsing utility
std::optional<int64_t> VM::parseDuration(const Value &value) const {
  if (value.isInt()) {
    return value.asInt();
  }

  if (value.isDouble()) {
    return static_cast<int64_t>(value.asDouble());
  }

  if (value.isStringValId() || value.isStringId() || value.isRegexValId()) {
    const std::string duration_str = resolveStringKey(value);

    // Plain numeric strings are milliseconds.
    static const std::regex numeric_regex(R"(^\d+(?:\.\d+)?$)");
    if (std::regex_match(duration_str, numeric_regex)) {
      return static_cast<int64_t>(std::stod(duration_str));
    }

    // Parse duration strings like "1s", "500ms", "2.5m", "1h"
    static const std::regex duration_regex(R"(^(\d+(?:\.\d+)?)(ms|s|m|h)$)");
    std::smatch match;

    if (std::regex_match(duration_str, match, duration_regex)) {
      double number = std::stod(match[1].str());
      std::string unit = match[2].str();

      if (unit == "ms") {
        return static_cast<int64_t>(number);
      } else if (unit == "s") {
        return static_cast<int64_t>(number * 1000.0);
      } else if (unit == "m") {
        return static_cast<int64_t>(number * 60.0 * 1000.0);
      } else if (unit == "h") {
        return static_cast<int64_t>(number * 60.0 * 60.0 * 1000.0);
      }
    }
  }

return std::nullopt;
}
std::string VM::resolveStringKey(const Value &value) const {
    if (value.isStringValId()) {
        uint32_t id = value.asStringValId();
        if (current_chunk) {
            auto& strs = current_chunk->getAllStrings();
            if (id < strs.size()) {
                return strs[id];
            }
        }
        if (main_chunk_) {
            auto& strs = main_chunk_->getAllStrings();
            if (id < strs.size()) {
                return strs[id];
            }
        }
    }
    if (value.isStringId()) {
        if (auto *s = heap_.string(value.asStringId())) {
            return *s;
        }
    }
    return const_cast<VM*>(this)->toString(value);
}

} // namespace havel::compiler
