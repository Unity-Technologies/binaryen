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
// Instrument the build with code to log execution at function entry.
// This differs from the LogExecution pass that it is possible to configure
// which functions should be instrumented and what log message is passed to the
// log function.
//

#include <map>
#include <sstream>
#include <string>
#include "asmjs/shared-constants.h"
#include "shared-constants.h"
#include "pass.h"
#include "support/file.h"
#include "wasm-builder.h"
#include "wasm.h"

namespace wasm {

// function log_execution(functionId: number, labelId: number): void
Name LoggerFunction("log_execution");


struct InstrumentFunctions : public WalkerPass<PostWalker<InstrumentFunctions>> {
    void doWalkModule(Module* curr) {
        Name configuration = getPassRunner()->options.getArgument(
            "instrument-functions",
            "InstrumentFunctions usage: wasm-opt "
            "--instrument-functions=CONFIG");

        parseConfiguration(configuration);
        insertLabelsToDataSection(curr);

        // Add import of external log function
        auto loggerFunctionImport = Builder::makeFunction(LoggerFunction, Signature(Type { Type::i32, Type::i32 }, Type::none), { });        
        loggerFunctionImport->base = LoggerFunction;

        // Add it to "env" module if present
        for (auto& func : curr->functions) {
            if (func->imported() && func->module == ENV) {
                loggerFunctionImport->module = func->module;
                break;
            }
        }

        // If "env" is not present add it to first module with imported functions
        if (!loggerFunctionImport->module) {
            for (auto& func : curr->functions) {
                if (func->imported()) {
                    loggerFunctionImport->module = func->module;
                    break;
                }
            }
        }
        if (!loggerFunctionImport->module) {
            loggerFunctionImport->module = ENV;
        }
        curr->addFunction(std::move(loggerFunctionImport));

        // Create function index map up front
        for (auto& func: curr->functions) {
            m_FunctionIndexMap[func.get()] = m_FunctionIndex++;
        }

        PostWalker<InstrumentFunctions>::doWalkModule(curr);
    }

    void visitFunction(Function* curr) {
        // Don't instrument imported functions
        if (curr->imported()) {
            return;
        }

        // if (auto* block = curr->body->dynCast<Block>()) {

        // }

        Index functionIndex = m_FunctionIndexMap[curr];
        auto it = m_FunctionToLabelMap.find(curr->name);
        if (it == m_FunctionToLabelMap.end()) {
            return;
        }

        std::string label = it->second;
        Index labelIndex = m_LabelOffsetMap[label];
        curr->body = createLogCall(curr->body, functionIndex, labelIndex);
    }
private:
    std::map<std::string, size_t> m_LabelOffsetMap;
    
    Index m_FunctionIndex = 0;
    std::map<Function*, Index> m_FunctionIndexMap;
    std::map<Name, std::string> m_FunctionToLabelMap;

    void parseConfiguration(Name configuration) {
        m_LabelOffsetMap.clear();
        m_FunctionToLabelMap.clear();

        // Load config from file if it starts with @
        if (configuration.startsWith("@")) {
            std::string configFile = read_file<std::string>(configuration.toString().substr(1), Flags::Text);
            std::stringstream stream(configFile);

            // Read config file line by line
            for (std::string line; std::getline(stream, line);) {
                // Find ";" separator
                size_t separatorIdx = line.find_first_of(";");
                if (separatorIdx == std::string::npos) {
                    Fatal() << "Invalid line in config file \"" << configuration << "\": "
                        << line;
                    break;
                }
                std::string functionName = line.substr(0, separatorIdx);
                std::string label = line.substr(separatorIdx + 1, line.length() - separatorIdx - 1);
                                
                // Insert function -> label map entry
                m_FunctionToLabelMap[functionName] = label;

                // Add entry for label with initial data section offset 0 (will be updated)
                if (m_LabelOffsetMap.find(label) == m_LabelOffsetMap.end()) {
                    m_LabelOffsetMap[label] = 0;
                }
            }


            return;
        }

        // Only configuration from File is supported
        Fatal() << "Unable to load configuration --instrument-functions=" << configuration;
    }

    void insertLabelsToDataSection(Module* module) {
        Builder builder(*module);

        // Find end of existing data section
        Name memoryName = "0";
        size_t offset = 0;
        for (size_t i = 0; i < module->dataSegments.size(); ++i) {
            const auto& dataSegment = module->dataSegments[i];

            wasm::Const* offsetExpression = dataSegment->offset->dynCast<wasm::Const>();
            if (offsetExpression) {
                size_t endOfDataSection = offsetExpression->value.geti32() + dataSegment->data.size();
                offset = std::max(offset, endOfDataSection);
            }
            
            // Use memory name of first data section
            if (memoryName == "0")
                memoryName = dataSegment->memory;
        }

        // Insert function labels at the end of existing data section        
        size_t idx = 0;
        for (auto it = m_LabelOffsetMap.begin(); it != m_LabelOffsetMap.end(); it++) {            
            std::stringstream dataSegmentName;
            dataSegmentName << "label_" << idx;
            auto dataSegment = builder.makeDataSegment(
                dataSegmentName.str(),
                memoryName,
                false,
                builder.makeConst(Literal::makeFromInt32(offset, Type::i32)),
                it->first.c_str(),
                it->first.length() + 1
            );            
            module->addDataSegment(std::move(dataSegment));

            // Update label offset map
            m_LabelOffsetMap[it->first] = offset;
            offset += it->first.length() + 1;
            idx++;
        }
    }

    Expression* createLogCall(Expression* curr, Index functionIndex, size_t labelOffset) {
        Builder builder(*getModule());

        // Inject call to log function to original
        return builder.makeSequence(
            builder.makeCall(LoggerFunction, { builder.makeConst(int32_t(functionIndex)), builder.makeConst(int32_t(labelOffset)) }, Type::none),
            curr
        );
    }
};

Pass* createInstrumentFunctionPass() { return new InstrumentFunctions(); }

}