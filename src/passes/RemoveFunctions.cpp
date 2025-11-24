/*
 * Copyright 2016 WebAssembly Community Group participants
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

// Removes a given set of functions from code.
//
// This pass will run --remove-unused-module-elements automatically for you, in
// order to remove as many things as possible.

#include <cctype>

#include "pass.h"
#include "support/file.h"
#include "wasm-builder.h"
#include "wasm.h"

namespace wasm {

static std::vector<Name> parseFunctionList(const IString& functionList,
                                           Module* module) {
  std::vector<Name> functions;
  std::string input = functionList.toString().c_str();

  // If --remove-functions=* is passed, remove everything possible. (track this
  // as an empty function list)
  if (functionList == "*") {
    return functions;
  }

  // Read function list from a file if prefixed with '@'
  if (functionList.startsWith(IString("@"))) {
    input = read_file<std::string>(input.substr(1), Flags::Text);
  }

  // Split string to a string list, delimited by ; and \n
  size_t begin = 0;
  for (size_t end = 1; end <= input.length(); ++end) {
    if (input[end] == ';' || input[end] == '\n' || end == input.length()) {
      // Trim \r and whitespace
      size_t trimEnd = end;
      while (trimEnd > 0 && input[trimEnd - 1] <= 32) {
        --trimEnd;
      }
      size_t trimBegin = begin;
      while (trimBegin < input.length() && input[trimBegin] <= 32) {
        ++trimBegin;
      }
      if (trimBegin < trimEnd) {
        std::string name = input.substr(trimBegin, trimEnd - trimBegin);
        if (std::isdigit(name[0])) {
          Index i = std::stoi(name);
          if (i >= module->functions.size()) {
            Fatal() << "Out of bounds function index " << i
                    << "! (module has only " << module->functions.size()
                    << " functions)";
          }
          // Assumes imports are at the beginning
          functions.push_back(module->functions[i]->name);
        } else {
          functions.push_back(name);
        }
      }
      begin = end + 1;
    }
  }
  if (functions.empty()) {
    Fatal() << "Unable to parse argument --remove-functions=" << functionList;
  }
  return functions;
}

static void remove(PassRunner* runner,
                   Module* module,
                   std::vector<Name> functionsToRemove) {

  Builder builder(*module);

  for (auto& func : module->functions) {
    if (!func->imported() &&
        (functionsToRemove.empty() ||
         std::find(functionsToRemove.begin(),
                   functionsToRemove.end(),
                   func->name) != functionsToRemove.end())) {
      const Type returns = func->getResults();

      if (returns == Type::unreachable) {
        std::cerr << "removing unreachable function " << func->name << "\n";
        func->vars.clear();
        func->body = builder.makeUnreachable();
      } else if (returns == Type::none) {
        std::cerr << "removing void function " << func->name << "\n";
        func->vars.clear();
        func->body = builder.makeReturn();
      } else if (returns == Type::i32) {
        std::cerr << "removing i32 function " << func->name << "\n";
        func->vars.clear();
        func->body = builder.makeConst(Literal(int32_t(0)));
      } else if (returns == Type::i64) {
        std::cerr << "removing i64 function " << func->name << "\n";
        func->vars.clear();
        func->body = builder.makeConst(Literal(int64_t(0)));
      } else if (returns == Type::f32) {
        std::cerr << "removing f32 function " << func->name << "\n";
        func->vars.clear();
        func->body = builder.makeConst(Literal(float(0.0f)));
      } else if (returns == Type::f64) {
        std::cerr << "removing f64 function " << func->name << "\n";
        func->vars.clear();
        func->body = builder.makeConst(Literal(double(0.0)));
      } else if (returns == Type::v128) {
        std::cerr << "removing v128 function " << func->name << "\n";
        func->vars.clear();
        std::array<uint8_t, 16> bytes;
        bytes.fill(0);
        func->body = builder.makeConst(Literal(bytes.data()));
      } else {
        std::cerr << "unable to remove function " << func->name
                  << "since it returns a " << returns << "\n";
      }
    }
  }

  // Remove unneeded things.
  PassRunner postRunner(runner);
  //  postRunner.add("inlining-optimizing");
  postRunner.add("remove-unused-module-elements");
  postRunner.setIsNested(true);
  postRunner.run();
}

struct RemoveFunctions : public Pass {
  void run(Module* module) override {
    Name name = getArgument(
      "remove-functions",
      "RemoveFunctions usage:  wasm-opt "
      "--remove-functions=FUNCTION_NAME"); // todo: multiple functions via
                                           // --remove-functions=name1;name2;index3;...
                                           // or
                                           // --remove-functions=@filename.txt
    std::vector<Name> functionsToRemove = parseFunctionList(name, module);
    remove(getPassRunner(), module, functionsToRemove);
  }
};

// declare passes

Pass* createRemoveFunctionsPass() { return new RemoveFunctions(); }

} // namespace wasm
