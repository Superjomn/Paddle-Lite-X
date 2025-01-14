#pragma once
#include <glog/logging.h>
#include <gtest/gtest_prod.h>
#include <iostream>
#include <memory>
#include "cinn/backends/llvm_headers.h"
#include "cinn/ir/ir_printer.h"
#include "cinn/target.h"

namespace cinn {
namespace backends {

/**
 * @brief Code generator for LLVM.
 */
class CodeGenLLVM : public ir::IRPrinter {
  llvm::LLVMContext *ctx_{};

 public:
  explicit CodeGenLLVM(Target target, llvm::LLVMContext &ctx, llvm::Module *module);

 public:
  void Visit(const ir::Expr *op) override { IRPrinter::Visit(op); }

  void Visit(const ir::Add *op) override;

  void Visit(const ir::Sub *op) override;

  void Visit(const ir::Mul *op) override;

  // TODO(Superjomn) Optimize this.
  void Visit(const ir::Div *op) override;

  void Visit(const ir::Mod *op) override;

  void Visit(const ir::Minus *op) override { IRPrinter::Visit(op); }

  void Visit(const ir::Exp *op) override { IRPrinter::Visit(op); }

  void Visit(const ir::Min *op) override;

  void Visit(const ir::Max *op) override;

  void Visit(const ir::NE *op) override;

  void Visit(const ir::EQ *op) override;

  void Visit(const ir::For *op) override;

  void Visit(const ir::IfThenElse *op) override { IRPrinter::Visit(op); }

  void Visit(const ir::GT *op) override;

  void Visit(const ir::GE *op) override;

  void Visit(const ir::LT *op) override;

  void Visit(const ir::LE *op) override;

  void Visit(const ir::And *op) override;

  void Visit(const ir::Or *op) override;

  void Visit(const ir::Block *op) override;

  void Visit(const ir::IntImm *op) override;

  void Visit(const ir::FloatImm *op) override;

  void Visit(const ir::Tensor *op) override;

  void Visit(const ir::Constant *op) override;

  void Visit(const ir::Var *op) override;

  void Visit(const ir::Reference *op) override;

  void Visit(const ir::Call *op) override { IRPrinter::Visit(op); }

  void Visit(const ir::Assign *op) override;

  void Visit(const ir::Function *op) override;

  void Visit(const ir::Allocate *op) override { IRPrinter::Visit(op); }

 protected:
  /** Some useful llvm types */
  // @{
  llvm::Type *void_t, *i8_t, *i16_t, *i32_t, *i64_t, *f16_t, *f32_t, *f64_t;
  llvm::Type *f16ptr_t, *f32ptr_t, *f64ptr_t, *i32ptr_t;
  // @}

  //! Emit code that evaluates an expression, and return the llvm IR value.
  llvm::Value *Codegen(const ir::Expr &e);

  void ResetValue() { value_ = nullptr; }

  llvm::Function *CreateFunctionPrototype(const ir::Function *op);

  llvm::Module &module() { return *module_; }

  void ReadTensorElement(const ir::Reference &ref);

 private:
  Target target_{};

  llvm::Module *module_{};
  llvm::Function *function_{};
  llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter> *builder_{};
  llvm::Value *value_{};

  // function arguments.
  std::map<std::string, llvm::Argument *> fn_args_;
  // name to llvm pointer.
  std::map<std::string, llvm::Value *> for_iterator_vars_;

  FRIEND_TEST(code_gen_llvm, basic);
  FRIEND_TEST(code_gen_llvm, array);
  FRIEND_TEST(code_gen_llvm, forloop);
  FRIEND_TEST(code_gen_llvm, array_ref);
};

}  // namespace backends
}  // namespace cinn
