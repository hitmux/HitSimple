#include "hitsimple/flowir/FlowIR.h"

namespace hitsimple::flowir {

std::string_view toString(Opcode opcode) {
  switch (opcode) {
  case Opcode::Invalid: return "invalid";
  case Opcode::LifetimeStart: return "lifetime_start";
  case Opcode::LifetimeEnd: return "lifetime_end";
  case Opcode::ConstantInteger: return "const_int";
  case Opcode::ConstantFloat: return "const_float";
  case Opcode::ConstantString: return "const_string";
  case Opcode::Load: return "load";
  case Opcode::AddressOf: return "address_of";
  case Opcode::Dereference: return "dereference";
  case Opcode::Unary: return "unary";
  case Opcode::Binary: return "binary";
  case Opcode::Select: return "select";
  case Opcode::Convert: return "convert";
  case Opcode::ReinterpretView: return "reinterpret_view";
  case Opcode::DynamicView: return "dynamic_view";
  case Opcode::ByteSwap: return "byte_swap";
  case Opcode::Call: return "call";
  case Opcode::Store: return "store";
  case Opcode::Input: return "input";
  case Opcode::Catch: return "catch";
  case Opcode::Branch: return "branch";
  case Opcode::Jump: return "jump";
  case Opcode::Return: return "return";
  case Opcode::Throw: return "throw";
  case Opcode::Unreachable: return "unreachable";
  }
  return "invalid";
}

std::string_view toString(CfgEdgeKind kind) {
  switch (kind) {
  case CfgEdgeKind::Normal: return "normal";
  case CfgEdgeKind::True: return "true";
  case CfgEdgeKind::False: return "false";
  case CfgEdgeKind::LoopBack: return "loop_back";
  case CfgEdgeKind::Exceptional: return "exceptional";
  case CfgEdgeKind::Cleanup: return "cleanup";
  }
  return "normal";
}

std::string_view toString(AccessKind kind) {
  switch (kind) {
  case AccessKind::Read: return "read";
  case AccessKind::Write: return "write";
  case AccessKind::ReadWrite: return "readwrite";
  }
  return "read";
}

std::string_view toString(AliasClass aliasClass) {
  switch (aliasClass) {
  case AliasClass::KnownObject: return "known_object";
  case AliasClass::UnknownExternal: return "unknown_external";
  }
  return "unknown_external";
}

std::string_view toString(ValueCategory category) {
  switch (category) {
  case ValueCategory::RValue: return "rvalue";
  case ValueCategory::LValue: return "lvalue";
  }
  return "rvalue";
}

std::string_view toString(ObjectStorage storage) {
  switch (storage) {
  case ObjectStorage::Global: return "global";
  case ObjectStorage::Local: return "local";
  case ObjectStorage::StaticLocal: return "static_local";
  case ObjectStorage::Parameter: return "parameter";
  case ObjectStorage::Catch: return "catch";
  }
  return "local";
}

Statistics statistics(const Module &module) {
  return Statistics{module.functions.size(), module.blocks.size(),
                    module.instructions.size(), module.edges.size(),
                    module.objects.size(), module.views.size()};
}

} // namespace hitsimple::flowir
