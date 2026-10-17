#include "support/TestRunner.h"

#include "hitsimple/parser/Parser.h"
#include "hitsimple/sema/Sema.h"
#include "safety/StaticSafetyAnalyzer.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<hitsimple::stdlib::StandardHeader> allStandardHeaders() {
  const auto headers = hitsimple::stdlib::allStandardHeaders();
  return {headers.begin(), headers.end()};
}

hitsimple::safety::StaticSafetyResult
analyzeSafety(std::string_view source,
              hitsimple::safety::StaticSafetyOptions options = {true, false}) {
  auto parseResult = hitsimple::parser::parseSource(source, "test.hs");
  if (!parseResult.unit || !parseResult.error.empty()) {
    hitsimple::testing::fail(__FILE__, __LINE__,
        "unexpected parse diagnostic: " + parseResult.error + "\n" + std::string(source));
  }

  auto analyzeResult = hitsimple::sema::analyze(
      *parseResult.unit,
      hitsimple::sema::AnalyzeOptions{true, allStandardHeaders()});
  if (!analyzeResult.unit || !analyzeResult.diagnostics.empty()) {
    std::string message = "unexpected semantic diagnostics:";
    for (const auto &diagnostic : analyzeResult.diagnostics) {
      message += "\n" + diagnostic.message;
    }
    hitsimple::testing::fail(__FILE__, __LINE__, message + "\n" + std::string(source));
  }

  return hitsimple::safety::analyzeStaticSafety(*analyzeResult.unit, options);
}

bool hasDiagnostic(const hitsimple::safety::StaticSafetyResult& result,
                   std::string_view message) {
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.message.find(message) != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

HS_TEST(StaticSafetyAnalyzer_ReportsConstantErrorsAsCodegenDiagnostics) {
  constexpr std::string_view source = "func main() -> i32 {\n"
                                      "    new divisor as i32 = 0\n"
                                      "    return 1 / divisor\n"
                                      "}\n";

  const auto unchecked = analyzeSafety(source, {false, false});
  const auto staticChecked = analyzeSafety(source, {true, false});
  const auto checked = analyzeSafety(source, {true, true});

  HS_EXPECT_TRUE(unchecked.diagnostics.empty());
  HS_EXPECT_EQ(staticChecked.diagnostics.size(), 1U);
  HS_EXPECT_TRUE(hasDiagnostic(staticChecked, "division by zero"));
  HS_EXPECT_TRUE(staticChecked.diagnostics.front().stage ==
                 hitsimple::diagnostic::Stage::Codegen);
  HS_EXPECT_TRUE(staticChecked.diagnostics.front().range.has_value());
  HS_EXPECT_EQ(staticChecked.diagnostics.front().range->begin.line, 3U);
  HS_EXPECT_TRUE(hasDiagnostic(checked, "division by zero"));
}

HS_TEST(StaticSafetyAnalyzer_TracksRangeLifetimeAndCStringFacts) {
  const auto outOfBounds = analyzeSafety(
      "func main() -> i32 {\n"
      "    new bytes[1] as bytes\n"
      "    return [2]*&bytes\n"
      "}\n");
  const auto expiredLocal = analyzeSafety(
      "func main() -> i32 {\n"
      "    new p as addr = 0\n"
      "    if (true) {\n"
      "        new value[1] as bytes = 'X'\n"
      "        p = &value\n"
      "    }\n"
      "    return [1]*p\n"
      "}\n");
  const auto unterminatedCString = analyzeSafety(
      "func main() -> i32 {\n"
      "    new raw[1] as bytes = 'A'\n"
      "    set raw as cstr\n"
      "    return raw == \"A\"\n"
      "}\n");

  HS_EXPECT_TRUE(hasDiagnostic(outOfBounds, "memory load out of bounds"));
  HS_EXPECT_TRUE(hasDiagnostic(expiredLocal, "use after scope exit dereference"));
  HS_EXPECT_TRUE(
      hasDiagnostic(unterminatedCString, "missing terminating byte in cstr"));
}

HS_TEST(StaticSafetyAnalyzer_MergesBranchLoopAndGotoState) {
  const auto divergentBranches = analyzeSafety(
      "func divide(flag as i32) -> i32 {\n"
      "    new divisor as i32 = 1\n"
      "    if (flag) {\n"
      "        divisor = 0\n"
      "    } else {\n"
      "        divisor = 1\n"
      "    }\n"
      "    return 1 / divisor\n"
      "}\n"
      "func main() {\n"
      "    return divide(0)\n"
      "}\n");
  const auto loopWrite = analyzeSafety(
      "func divide(flag as i32) -> i32 {\n"
      "    new divisor as i32 = 1\n"
      "    while (flag) {\n"
      "        divisor = 0\n"
      "    }\n"
      "    return 1 / divisor\n"
      "}\n"
      "func main() {\n"
      "    return divide(0)\n"
      "}\n");
  const auto skippedAssignment = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    goto done\n"
      "    p = &large\n"
      "    done: return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(divergentBranches.diagnostics.empty());
  HS_EXPECT_TRUE(loopWrite.diagnostics.empty());
  HS_EXPECT_TRUE(hasDiagnostic(skippedAssignment, "memory load out of bounds"));
}

HS_TEST(StaticSafetyAnalyzer_HonorsLogicalShortCircuitEffects) {
  const auto falseAnd = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (false && (p = &large)) {}\n"
      "    return [2]*p\n"
      "}\n");
  const auto unknownAnd = analyzeSafety(
      "func main(flag as bool) -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    if (flag && (p = &large)) {}\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(hasDiagnostic(falseAnd, "memory load out of bounds"));
  HS_EXPECT_TRUE(unknownAnd.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_InvalidatesFactsAfterOrdinaryCalls) {
  const auto result = analyzeSafety(
      "new large[8] as bytes\n"
      "new small[1] as bytes\n"
      "new p as addr = &large\n"
      "func poison() {\n"
      "    p = &small\n"
      "}\n"
      "func main() -> i32 {\n"
      "    poison()\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_ReportsUseAfterFreeThroughAddressAlias) {
  const auto result = analyzeSafety(
      "func main() -> i32 {\n"
      "    new p as addr = alloc(8)\n"
      "    new alias as addr = p\n"
      "    free(p)\n"
      "    return [1]*alias\n"
      "}\n");

  HS_EXPECT_TRUE(hasDiagnostic(result, "use after free dereference"));
}

HS_TEST(StaticSafetyAnalyzer_MergesDivergentTryCatchAddressRanges) {
  const auto result = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new large[8] as bytes\n"
      "    new p as addr = &small\n"
      "    try {\n"
      "        p = &large\n"
      "    } catch (error[1]) {\n"
      "        p = &small\n"
      "    }\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_InvalidatesFactsAfterIndirectMemoryWrite) {
  const auto result = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1] as bytes\n"
      "    new p as addr = &small\n"
      "    memset(&p, 0, 8)\n"
      "    return [2]*p\n"
      "}\n");

  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_LengthPreservesStaticFactsAndOperandChecks) {
  const auto bounds = analyzeSafety(
      "func main() -> i32 {\n"
      "    new small[1]\n"
      "    new large[8]\n"
      "    new copied = memcpy(small, large, length(large))\n"
      "    return 0\n}\n");
  HS_EXPECT_TRUE(hasDiagnostic(bounds, "out of bounds"));
  const auto division = analyzeSafety(
      "func main() -> i32 {\n"
      "    new divisor as u64 = 0\n"
      "    new value as u64 = 1\n"
      "    new measured = length(resize_bytes(value, 8 / divisor))\n"
      "    return 0\n}\n");
  HS_EXPECT_TRUE(hasDiagnostic(division, "division by zero"));
  const auto assignment = analyzeSafety(
      "func main() -> i32 {\n"
      "    new divisor as i32 = 1\n"
      "    new size = length((divisor = 0))\n"
      "    return 1 / divisor\n}\n");
  HS_EXPECT_TRUE(hasDiagnostic(assignment, "division by zero"));
}

HS_TEST(StaticSafetyAnalyzer_RebindingChecksCurrentBindingAccesses) {
  for (const auto &access : {"return value", "value = 4; return 0"}) {
    const auto result = analyzeSafety(
        "func main() -> i32 { new value as i32 = 1; value &= 0; " +
        std::string(access) + "; }");
    HS_EXPECT_TRUE(hasDiagnostic(result, "null address"));
  }
  const auto shortTarget = analyzeSafety(R"(
    func main() -> i32 {
      new small[1] as bytes
      new value as i32 = 1
      value &= &small
      return value
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(shortTarget, "memory load out of bounds"));
  const auto addressOnly = analyzeSafety(R"(
    func main() -> i32 {
      new value as i32 = 1
      value &= 0
      new current as addr = &value
      return to_i32(length(value))
    }
  )");
  HS_EXPECT_TRUE(addressOnly.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingTracksDynamicAndScopeLifetimes) {
  const auto freed = analyzeSafety(R"(
    func main() -> i32 {
      new p as addr = alloc(4)
      new value as i32
      value &= p
      new alias as addr = &value
      free(p)
      return [4]*alias
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(freed, "use after free dereference"));
  const auto freedName = analyzeSafety(R"(
    func main() -> i32 {
      new p as addr = alloc(4)
      new value as i32
      value &= p
      free(p)
      return value
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(freedName, "use after free load"));
  const auto expired = analyzeSafety(R"(
    func main() -> i32 {
      new value as i32
      if (true) {
        new local as i32 = 7
        value &= &local
      }
      return value
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(expired, "use after scope exit load"));
  const auto definitionLifetime = analyzeSafety(R"(
    func main() -> i32 {
      new outside as i32 = 7
      new saved as addr
      if (true) {
        new local as i32
        local &= &outside
        saved = &local
      }
      return [4]*saved
    }
  )");
  HS_EXPECT_TRUE(definitionLifetime.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingSeparatesPointerContentsFromLocation) {
  const auto result = analyzeSafety(R"(
    func main() -> i32 {
      new small[1] as bytes
      new large[8] as bytes
      new p as addr = &small
      new q as addr = &large
      new original as addr = &p
      p &= &q
      return [2]*p
    }
  )");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto aliases = analyzeSafety(R"(
    func main() -> i32 {
      new zero as i32 = 0
      new value as i32 = 9
      value &= &zero
      return 8 / value
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(aliases, "division by zero"));
  const auto writes = analyzeSafety(R"(
    func main() -> i32 {
      new divisor as i32 = 4
      new value as i32 = 9
      value &= &divisor
      value = 0
      return 8 / divisor
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(writes, "division by zero"));
  const auto savedDefinition = analyzeSafety(R"(
    func main() -> i32 {
      new small[1] as bytes
      new large[8] as bytes
      new p as addr = &small
      new q as addr = &large
      new alias as addr
      alias &= &p
      p &= &q
      return [2]*alias
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(savedDefinition, "memory load out of bounds"));
}

HS_TEST(StaticSafetyAnalyzer_RebindingKeepsCapturedLvalueLocation) {
  const auto originalZero = analyzeSafety(R"(
    func main() -> i32 {
      new zero as i32 = 0
      new nonzero as i32 = 2
      new copied as i32
      copied, _ = zero, (zero &= &nonzero)
      return 8 / copied
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(originalZero, "division by zero"));
  const auto originalNonzero = analyzeSafety(R"(
    func main() -> i32 {
      new nonzero as i32 = 2
      new zero as i32 = 0
      new copied as i32
      copied, _ = nonzero, (nonzero &= &zero)
      return 8 / copied
    }
  )");
  HS_EXPECT_TRUE(originalNonzero.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingMergesUnknownLocationsConservatively) {
  const auto branches = analyzeSafety(R"(
    func main(flag as bool) -> i32 {
      new value as i32 = 1
      new other as i32 = 2
      if (flag) { value &= 0; } else { value &= &other; }
      return value
    }
  )");
  HS_EXPECT_TRUE(branches.diagnostics.empty());
  const auto commonNull = analyzeSafety(R"(
    func main(flag as bool) -> i32 {
      new value as i32 = 1
      if (flag) { value &= 0; } else { value &= 0; }
      return value
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(commonNull, "null address"));
  const auto globalCall = analyzeSafety(R"(
    new value as i32 = 1
    new other as i32 = 2
    func restore() { value &= &other; }
    func main() -> i32 {
      value &= 0
      restore()
      return value
    }
  )");
  HS_EXPECT_TRUE(globalCall.diagnostics.empty());
  const auto unknownWrite = analyzeSafety(R"(
    func main(destination as addr) -> i32 {
      new value as i32 = 0
      new alias as i32
      alias &= &value
      [4]*destination = 1
      return 8 / alias
    }
  )");
  HS_EXPECT_TRUE(unknownWrite.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingChecksOnlyAccessedMemberExtent) {
  const auto validMember = analyzeSafety(R"(
    template Record { word[4] as i32; tail[4] as i32; }
    func main() -> i32 {
      new record as Record
      new small as i32 = 2
      record &= &small
      record.word = 3
      return record.word
    }
  )");
  HS_EXPECT_TRUE(validMember.diagnostics.empty());
  const auto invalidMember = analyzeSafety(R"(
    template Record { word[4] as i32; tail[4] as i32; }
    func main() -> i32 {
      new record as Record
      new small as i32 = 2
      record &= &small
      return record.tail
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(invalidMember, "out of bounds"));
  const auto declaredExtent = analyzeSafety(R"(
    func main() -> i32 {
      new small[4] as bytes
      new large[8] as bytes
      small &= &large
      memset(small, 0, 8)
      return 0
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(declaredExtent, "out of bounds"));
}

HS_TEST(StaticSafetyAnalyzer_RebindingPreservesOriginalDefinitionContents) {
  const auto savedDefinition = analyzeSafety(R"(
    func main() -> i32 {
      new small[1] as bytes
      new large[8] as bytes
      new p as addr = &small
      new q as addr = &large
      new saved as addr = &p
      p &= &q
      new original as addr = [8]*saved
      return [2]*original
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(savedDefinition, "memory load out of bounds"));
  const auto globalAliasWrite = analyzeSafety(R"(
    new global as i32 = 9
    func change() { global = 1; }
    func main() -> i32 {
      new local as i32 = 0
      global &= &local
      change()
      return 8 / local
    }
  )");
  HS_EXPECT_TRUE(globalAliasWrite.diagnostics.empty());
  const auto loopMerge = analyzeSafety(R"(
    func main(flag as bool) -> i32 {
      new local as i32 = 1
      new other as i32 = 2
      local &= 0
      while (flag) { local &= &other; }
      return local
    }
  )");
  HS_EXPECT_TRUE(loopMerge.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingCStringScanUsesActualReadExtent) {
  const auto result = analyzeSafety(R"(
    func main() -> i32 {
      new name[32] as cstr
      new target[2] as cstr = "z"
      name &= &target
      new copy[8] as cstr = name
      return copy == "z"
    }
  )");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto snapshot = analyzeSafety(R"(
    func main() -> i32 {
      new name[32] as cstr
      new target[2] as cstr = "z"
      _ = (name &= &target)
      return 0
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(snapshot, "assignment snapshot out of bounds"));
  const auto rawCopy = analyzeSafety(R"(
    func main() -> i32 {
      new name[32] as cstr
      new target[2] as cstr = "z"
      name &= &target
      new copy[32] as bytes = name as bytes
      return 0
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(rawCopy, "out of bounds"));
  const auto partialWrite = analyzeSafety(R"(
    func main() -> i32 {
      new name[32] as bytes
      new target[2] as bytes
      name &= &target
      memset(name, 0, 1)
      return 0
    }
  )");
  HS_EXPECT_TRUE(partialWrite.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingDiscardedCaptureDoesNotReadBytes) {
  const auto discarded = analyzeSafety(R"(
    func main() -> i32 {
      new value as i32
      value &= 0
      new target as i32 = 1
      (value &=), _ = &target, value
      return value
    }
  )");
  HS_EXPECT_TRUE(discarded.diagnostics.empty());
  const auto consumed = analyzeSafety(R"(
    func main() -> i32 {
      new pointer as addr
      pointer &= 0
      new target as i32 = 1
      target &= pointer
      return 0
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(consumed, "null address"));
}

HS_TEST(StaticSafetyAnalyzer_RebindingCStringBooleanReadsWholeView) {
  for (const auto *consumer : {
           "if (oversized) { return 0; }",
           "new result as bool = oversized;",
           "new copied[32] as bytes = oversized as bytes;"}) {
    const auto result = analyzeSafety(
        "func main() -> i32 { new oversized[32] as cstr; "
        "new tiny[2] as cstr = \"z\"; oversized &= &tiny; " +
        std::string(consumer) + "\n return 1; }");
    HS_EXPECT_TRUE(hasDiagnostic(result, "out of bounds"));
  }
  const auto scans = analyzeSafety(R"(
    func main() -> i32 {
      new oversized[32] as cstr
      new tiny[2] as cstr = "z"
      oversized &= &tiny
      printf("%s", oversized)
      new measured as u64 = strlen(oversized)
      return oversized == "z"
    }
  )");
  HS_EXPECT_TRUE(scans.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingUserOpHonorsCopyAndBorrowContracts) {
  const auto borrowedTarget = analyzeSafety(R"(
    template Box { first[4] as i32; tail[4] as i32; }
    impl Box {
      op = (dst as Box, src as Box) -> Box {
        dst.first = src.first
        return src
      }
    }
    func main() -> i32 {
      new target as Box
      new small as i32 = 3
      new source as Box
      source.first = 9
      source.tail = 7
      target &= &small
      target = source
      return 0
    }
  )");
  HS_EXPECT_TRUE(borrowedTarget.diagnostics.empty());
  const auto copiedSource = analyzeSafety(R"(
    template Box { first[4] as i32; tail[4] as i32; }
    impl Box {
      op = (dst as Box, src as Box) -> Box {
        dst.first = src.first
        return src
      }
    }
    func main() -> i32 {
      new target as Box
      new small as i32 = 3
      new source as Box
      source &= &small
      target = source
      return 0
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(copiedSource, "out of bounds"));
}

HS_TEST(StaticSafetyAnalyzer_RebindingLowByteConsumersReadOneByte) {
  const auto result = analyzeSafety(R"(
    func main() -> i32 {
      new ch as u8 = 65
      new value[16] as bytes
      value &= &ch
      new target[2] as bytes
      memset(target, value, 2)
      new a = is_alpha(value)
      new b = is_digit(value)
      new c = is_alnum(value)
      new d = is_space(value)
      new e = to_upper(value)
      new f = to_lower(value)
      new found = strchr("ABC", value)
      return 0
    }
  )");
  HS_EXPECT_TRUE(result.diagnostics.empty());
  const auto nullByte = analyzeSafety(R"(
    func main() -> i32 {
      new value[16] as bytes
      value &= 0
      return is_alpha(value)
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(nullByte, "null address"));
}

HS_TEST(StaticSafetyAnalyzer_RebindingIndexAndSliceUseDeclaredBounds) {
  for (const auto *selected : {"small[index]", "small[index:+1]"}) {
    for (const auto *consumer : {"new address as addr = &", "new measured = length("}) {
      const auto statement = std::string(consumer) + selected +
          (std::string(consumer).find("length") != std::string::npos ? ");" : ";");
      const auto result = analyzeSafety(
          "func main() -> i32 { new small[1] as bytes; new large[8] as bytes; "
          "small &= &large; new index as i32 = 2; " + statement + " return 0; }");
      HS_EXPECT_TRUE(hasDiagnostic(result, "index or slice out of bounds"));
    }
  }
  const auto unsignedNegative = analyzeSafety(R"(
    func main() -> i32 {
      new bytes[512] as bytes
      new index as u8 = 255
      new address as addr = &bytes[index]
      return 0
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(unsignedNegative, "index or slice out of bounds"));
  const auto plainOffset = analyzeSafety(R"(
    func main() -> i32 {
      new small[1] as bytes
      new large[8] as bytes
      small &= &large
      new address as addr = (&small)? + 2
      return 0
    }
  )");
  HS_EXPECT_TRUE(plainOffset.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingCStringDoesNotInheritLargerTerminationFact) {
  const auto result = analyzeSafety(R"(
    func main() -> i32 {
      new small[2] as cstr
      new large[8] as cstr = "abcdefg"
      small &= &large
      new measured as u64 = strlen(small)
      return 0
    }
  )");
  // We do not yet track the exact zero-byte position. The larger definition's
  // termination flag cannot prove termination within the smaller named View.
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_RebindingMemoryConsumersRespectSelectedViewExtent) {
  for (const auto *selected : {"name[0:+1]", "name[7:+1]",
                               "name[0:+1] as bytes", "[1]*&name"}) {
    const auto prefix =
        "func main() -> i32 { new name[8] as bytes; "
        "new backing[16] as bytes; name &= &backing; ";
    const auto overread = analyzeSafety(std::string(prefix) +
        "return memcmp(" + selected + ", backing, 2); }");
    HS_EXPECT_TRUE(hasDiagnostic(overread, "memory load out of bounds"));
    const auto exactRead = analyzeSafety(std::string(prefix) +
        "return memcmp(" + selected + ", backing, 1); }");
    HS_EXPECT_TRUE(exactRead.diagnostics.empty());
  }
}

HS_TEST(StaticSafetyAnalyzer_UnknownBindingRetainsDeclaredCapacity) {
  for (const auto *selected : {"name[0:+1]", "name[0:+1] as bytes", "[1]*pointer"}) {
    const auto result = analyzeSafety(
        "func main(pointer as addr) -> i32 { new name[8] as bytes; "
        "name &= pointer; return memcmp(" + std::string(selected) +
        ", name, 2); }");
    HS_EXPECT_TRUE(hasDiagnostic(result, "memory load out of bounds"));
  }
}

HS_TEST(StaticSafetyAnalyzer_CapturedSliceRetainsVisibleExtent) {
  using namespace hitsimple::hir;
  for (const auto builtin : {hitsimple::stdlib::BuiltinId::Memcmp,
                            hitsimple::stdlib::BuiltinId::Memset}) {
    const bool write = builtin == hitsimple::stdlib::BuiltinId::Memset;
    for (const auto count : {1U, 2U}) {
      const auto selected = viewSemanticsForTemplate("bytes", 1, true, true);
      std::vector<std::unique_ptr<Stmt>> statements;
      statements.push_back(std::make_unique<LocalMemory>("backing", 16));
      statements.push_back(std::make_unique<CaptureView>("captured",
          std::make_unique<TemplateViewExpr>(
              std::make_unique<DerefExpr>(
                  std::make_unique<AddressOfExpr>("backing", "backing", 16,
                      MemoryStorage::Local, 0,
                      viewSemanticsForTemplate("addr", sizeof(void*))),
                  selected),
              "bytes", true, selected)));
      std::vector<std::unique_ptr<Expr>> arguments;
      arguments.push_back(std::make_unique<CapturedViewRef>("captured", selected));
      if (write) {
        arguments.push_back(std::make_unique<IntegerLiteral>("0",
            viewSemanticsForTemplate("u8", 1)));
      } else {
        arguments.push_back(std::make_unique<VariableRef>("backing",
            viewSemanticsForTemplate("bytes", 16, true, true)));
      }
      arguments.push_back(std::make_unique<IntegerLiteral>(std::to_string(count),
          viewSemanticsForTemplate("u64", 8)));
      statements.push_back(std::make_unique<Call>(write ? "memset" : "memcmp",
          std::move(arguments), builtin));
      std::vector<std::unique_ptr<Function>> functions;
      functions.push_back(std::make_unique<Function>("main",
          std::make_unique<Block>(std::move(statements))));
      TranslationUnit unit(std::move(functions));
      HS_EXPECT_TRUE(verifyHIR(unit).empty());
      const auto result = hitsimple::safety::analyzeStaticSafety(unit, {true, false});
      HS_EXPECT_EQ(hasDiagnostic(result, write ? "memory store out of bounds"
                                              : "memory load out of bounds"),
                   count > 1);
    }
  }
}

HS_TEST(StaticSafetyAnalyzer_RawMemoryAddressesDoNotAcquirePointerWidthCapacity) {
  const auto result = analyzeSafety(R"(
    func main(stream as handle) -> i32 {
      new backing[16] as bytes
      return memcmp((&backing)? + 0, backing, 16)
    }
  )");
  HS_EXPECT_TRUE(result.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_DefaultCStringCopyRequiresTerminatedSource) {
  for (const auto length : {1U, 8U}) {
    const auto result = analyzeSafety(
        "func main() -> i32 { new source[2] as bytes = 'AB'; "
        "set source as cstr; new target[" + std::to_string(length) +
        "] as cstr = source; return 0; }");
    HS_EXPECT_TRUE(hasDiagnostic(result, "missing terminating byte in cstr"));
  }
}

HS_TEST(StaticSafetyAnalyzer_RawStringCopyGuaranteesTerminatedDestination) {
  for (const auto length : {1U, 8U}) {
    const auto result = analyzeSafety(
        "func main() -> i32 { new source[2] as bytes = 'AB'; "
        "new target[" + std::to_string(length) +
        "] as cstr %s= source; return target == \"\"; }");
    HS_EXPECT_TRUE(result.diagnostics.empty());
  }
}

HS_TEST(StaticSafetyAnalyzer_CStringCallsReadAfterLaterArgumentEffects) {
  for (const auto* use : {"return strcmp(alias, repair(&raw));",
                          "return alias == repair(&raw);",
                          "printf(alias, repair(&raw)); return 0;"}) {
    const auto result = analyzeSafety(
        "func repair(p as addr) -> result[2] as cstr { [1]*p = 0; return \"\"; } "
        "func main() -> i32 { new raw[2] as bytes = 'AB'; set raw as cstr; "
        "new alias[2] as cstr; alias &= &raw; " + std::string(use) + " }");
    HS_EXPECT_TRUE(result.diagnostics.empty());
  }
}

HS_TEST(StaticSafetyAnalyzer_DeferredCStringReadsObserveLaterContentWrites) {
  for (const auto* use : {"return strcmp(alias, (raw = 'AB') as cstr);",
                          "return alias == ((raw = 'AB') as cstr);",
                          "printf(\"%s%s\", alias, (raw = 'AB') as cstr); return 0;"}) {
    const auto result = analyzeSafety(
        "func main() -> i32 { new raw[2] as bytes = 'A\\0'; "
        "new alias[2] as cstr; alias &= &raw; " + std::string(use) + " }");
    HS_EXPECT_TRUE(hasDiagnostic(result, "missing terminating byte in cstr"));
  }
  const auto repaired = analyzeSafety(R"(
    func main() -> i32 {
      new raw[2] as bytes = 'AB'
      new alias[2] as cstr
      alias &= &raw
      return strcmp(alias, (raw = 'A\0') as cstr)
    }
  )");
  HS_EXPECT_TRUE(repaired.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_DeferredCStringReadsFreezeBindingLocations) {
  for (const auto* use : {"return strcmp(alias, ((alias &= &good) as cstr));",
                          "return alias == ((alias &= &good) as cstr);",
                          "printf(\"%s%s\", alias, ((alias &= &good) as cstr)); return 0;"}) {
    const auto result = analyzeSafety(
        "func main() -> i32 { new raw[2] as bytes = 'AB'; "
        "new good[2] as cstr = \"x\"; new alias[2] as cstr; alias &= &raw; " +
        std::string(use) + " }");
    HS_EXPECT_TRUE(hasDiagnostic(result, "missing terminating byte in cstr"));
  }
}

HS_TEST(StaticSafetyAnalyzer_MemoryCallsFreezeViewsBeforeLaterRebinding) {
  const auto shortOriginal = analyzeSafety(R"(
    func main() -> i32 {
      new name[8] as bytes
      new tiny[1] as bytes
      new large[8] as bytes
      name &= &tiny
      return memcmp(name, (name &= &large), 2)
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(shortOriginal, "memory load out of bounds"));
  const auto nullOriginal = analyzeSafety(R"(
    func main() -> i32 {
      new name[1] as bytes
      new target[1] as bytes
      name &= 0
      return memcmp(name, (name &= &target), 1)
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(nullOriginal, "null address"));
}

HS_TEST(StaticSafetyAnalyzer_FileCallsUsePointerStorageViewCapacity) {
  for (const auto* builtin : {"fread", "fwrite"}) {
    const auto source = "func main(stream as handle) -> i32 { "
        "new pointer as addr = 0; new result = " + std::string(builtin);
    const auto valid = analyzeSafety(source + "(pointer, 1, " +
        std::to_string(sizeof(void*)) + ", stream); return 0; }");
    HS_EXPECT_TRUE(valid.diagnostics.empty());
    const auto tooLarge = analyzeSafety(source + "(pointer, 1, " +
        std::to_string(sizeof(void*) + 1) + ", stream); return 0; }");
    HS_EXPECT_TRUE(hasDiagnostic(tooLarge, "out of bounds"));
  }
}

HS_TEST(StaticSafetyAnalyzer_RebindingCStringRvaluesKeepTerminationAndSnapshotBounds) {
  for (const auto *destination : {"new result[1] as cstr = (name &= &target); return 0;",
                                  "return (name &= &target);"}) {
    const bool returnsString = std::string(destination).starts_with("return");
    const auto source = std::string("func probe() -> ") +
        (returnsString ? "out[1] as cstr" : "i32") + " { "
        "new name[2] as cstr = \"x\"; new target[2] as bytes = 'AB'; " + destination + " } func main() -> i32 { return 0; }";
    const auto result = analyzeSafety(source);
    HS_EXPECT_TRUE(hasDiagnostic(result, "missing terminating byte in cstr"));
  }
  const auto shortSnapshot = analyzeSafety(R"(
    func main() -> i32 {
      new name[8] as cstr = "old"
      new target[2] as cstr = "x"
      new result[1] as cstr = (name &= &target)
      return 0
    }
  )");
  HS_EXPECT_TRUE(hasDiagnostic(shortSnapshot, "memory assignment snapshot out of bounds"));
}

HS_TEST(StaticSafetyAnalyzer_RebindingAsciiExtensionsReadOnlyLowByte) {
  for (const auto *predicate : {"is_lower", "is_upper", "is_xdigit", "is_blank",
                                "is_control", "is_print", "is_graph", "is_punct"}) {
    const auto source = std::string("func main() -> i32 { new wide as f64 = 0.0; ") +
        "new tiny[1] as bytes = 'a'; wide &= &tiny; return " + predicate + "(wide); }";
    HS_EXPECT_TRUE(analyzeSafety(source).diagnostics.empty());
    const auto nullSource = std::string("func main() -> i32 { new wide as f64 = 0.0; ") +
        "wide &= 0; return " + predicate + "(wide); }";
    HS_EXPECT_TRUE(hasDiagnostic(analyzeSafety(nullSource), "null address"));
  }
}

HS_TEST(StaticSafetyAnalyzer_UnsignedSnapshotReadsCompleteReboundExtent) {
  for (const auto *type : {"bytes", "cstr"}) {
    const auto prefix = "func main() -> i32 { new backing[1] as bytes = '\\0'; new view[8] as " +
                        std::string(type) + "; view &= &backing; ";
    const auto formation = analyzeSafety(prefix + "new ignored = length(view); return 0; }");
    const auto snapshot = analyzeSafety(prefix + "new ignored = length(view?); return 0; }");
    HS_EXPECT_TRUE(formation.diagnostics.empty());
    HS_EXPECT_TRUE(hasDiagnostic(snapshot, "unsigned snapshot out of bounds"));
  }
}

HS_TEST(StaticSafetyAnalyzer_UnsignedSnapshotChecksLifetimeAndIgnoresCStringTermination) {
  const auto expired = analyzeSafety(
      "func main() -> i32 { new pointer as addr = 0; if (true) { new value as u8 = 1; "
      "pointer = &value; }; new ignored = length(([1]*pointer)?); return 0; }");
  HS_EXPECT_TRUE(hasDiagnostic(expired, "use after scope exit unsigned snapshot"));
  const auto raw = analyzeSafety(
      "func main() -> i32 { new text[3] as bytes = 'ABC'; set text as cstr; "
      "new ignored = length(text?); return 0; }");
  HS_EXPECT_TRUE(raw.diagnostics.empty());
}

HS_TEST(StaticSafetyAnalyzer_UnsignedPointerSnapshotHasViewCapacity) {
  const auto source = "func main() -> i32 { new backing[16] as bytes; "
      "new pointer as addr = &backing; return memcmp(pointer?, backing, 16); }";
  const auto result = analyzeSafety(source);
  HS_EXPECT_TRUE(hasDiagnostic(result, "memory load out of bounds"));
}
