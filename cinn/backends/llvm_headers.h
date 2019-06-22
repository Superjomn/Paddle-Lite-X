#pragma once

#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/JITEventListener.h>

#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include "llvm/Support/ErrorHandling.h"
#include <llvm/Support/FileSystem.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/DataExtractor.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Transforms/IPO/Inliner.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/Transforms/Utils/SymbolRewriter.h>
#include <llvm/Transforms/Instrumentation.h>
