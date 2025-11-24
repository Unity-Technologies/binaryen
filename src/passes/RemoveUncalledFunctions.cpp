/*
 * Copyright 2017 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// Removes all functions and possibly even basic blocks that were not
// executed during a runtime code coverage traced run.
//

#include "asmjs/shared-constants.h"
#include "shared-constants.h"
#include "support/file.h"
#include <pass.h>
#include <wasm-builder.h>
#include <wasm.h>

namespace wasm {

static int numFunctionsRemoved = 0;
static int numFunctionsPreserved = 0;
static int numBlocksRemoved = 0;
static int numBlocksPreserved = 0;

extern Name LOGGER; // in LogExecution.cpp

struct RemoveUncalledFunctions
  : public WalkerPass<
      PostWalker<RemoveUncalledFunctions,
                 UnifiedExpressionVisitor<RemoveUncalledFunctions>>> {

  Expression* stripLogExecutionCall(Function* func, Block* block, Call* call) {
    size_t idx = call->operands[0]->dynCast<Const>()->value.geti32();

    if (idx >= blocksToRemove.size() << 3 ||
        !(blocksToRemove[idx >> 3] & (1 << (idx & 7)))) {
      const Type returns = block->type;
      if (returns == Type::unreachable) {
        return builder->makeUnreachable();
      } else if (returns == Type::none) {
        return builder->makeNop();
      } else if (returns == Type::i32) {
        return builder->makeConst(Literal(int32_t(0)));
      } else if (returns == Type::i64) {
        return builder->makeConst(Literal(int64_t(0)));
      } else if (returns == Type::f32) {
        return builder->makeConst(Literal(float(0)));
      } else if (returns == Type::f64) {
        return builder->makeConst(Literal(double(0)));
      } else {
        std::cerr << "unable to remove block in function " << func->name
                  << " since the function returns a " << returns << "\n";
      }
    }
    return nullptr;
  }

  void visitExpression(Expression* e) {
    if (auto* block = e->dynCast<Block>()) {
      for (size_t i = 0; i < block->list.size(); ++i) {
        if (auto* call = block->list[i]->dynCast<Call>()) {
          if (call->target == LOGGER || call->target == coverageLogFunction) {
            ++numBlocksPreserved;
            block->list.erase(block->list.begin() + i);
            --i;
          }
        }
      }
    }
  }

  void run(Module* module) override {
    Name name = getArgument(
      "remove-uncalled-functions",
      "RemoveUncalledFunctions usage:  wasm-opt "
      "--remove-uncalled-functions=COVERAGE.DAT");

    std::ifstream infile;
    std::ios_base::openmode flags = std::ifstream::in | std::ifstream::binary;
    infile.open(name.toString().c_str(), flags);
    if (!infile.is_open()) {
      std::cerr << "Failed opening '" << name << "'" << std::endl;
      exit(EXIT_FAILURE);
    }
    infile.seekg(0, std::ios::end);
    std::streampos insize = infile.tellg();
    blocksToRemove.resize(insize);
    infile.seekg(0);
    infile.read((char*)&blocksToRemove[0], insize);

    // Find the imported function that represents the execution coverage logger
    for (auto& func : module->functions) {
      if (func->imported() && func->base == LOGGER) {
        std::cerr << "Import " << func->name
                  << " is the execution coverage logger.\n\n";
        coverageLogFunction = func->name;
      }
    }

    builder = new Builder(*module);

    for (auto& func : module->functions) {
      if (func->imported()) {
        continue;
      }

      if (auto* block = func->body->dynCast<Block>()) {
        if (!block->list.empty()) {
          if (auto* call = block->list.front()->dynCast<Call>()) {
            if (call->target == LOGGER || call->target == coverageLogFunction) {
              Expression* replacedExpression =
                stripLogExecutionCall(func.get(), block, call);
              if (replacedExpression) {
                block->list.clear();
                if (replacedExpression->dynCast<Nop>()) {
                  block->list.push_back(builder->makeReturn());
                } else {
                  block->list.push_back(
                    builder->makeReturn(replacedExpression));
                }
                ++numFunctionsRemoved;
              } else {
                // Cannot remove this block, only remove the call to the
                // coverage log function
                ++numFunctionsPreserved;
                block->list.erase(block->list.begin());
              }
            }
          }
        }
      }
    }

    WalkerPass<PostWalker<RemoveUncalledFunctions,
                          UnifiedExpressionVisitor<RemoveUncalledFunctions>>>::
      run(getPassRunner(), module);

    delete builder;

    std::cerr << "Removed " << numFunctionsRemoved << "/"
              << numFunctionsRemoved + numFunctionsPreserved << " ("
              << numFunctionsRemoved * 100.0 /
                   (numFunctionsRemoved + numFunctionsPreserved)
              << "%) of functions as unreachable.\n";
    std::cerr << "Removed " << numBlocksRemoved << "/"
              << numBlocksRemoved + numBlocksPreserved << " ("
              << numBlocksRemoved * 100.0 /
                   (numBlocksRemoved + numBlocksPreserved)
              << "%) of blocks as unreachable.\n";

    // Remove unneeded things.
    PassRunner postRunner(getPassRunner());
    postRunner.add("inlining-optimizing");
    postRunner.add("dce");
    postRunner.add("remove-unused-module-elements");
    postRunner.setIsNested(true);
    postRunner.run();
  }

private:
  std::vector<uint8_t> blocksToRemove;
  Name coverageLogFunction;
  Builder* builder;
};

Pass* createRemoveUncalledFunctionsPass() {
  return new RemoveUncalledFunctions();
}

} // namespace wasm
