#include "cinn/core/function.h"
#include <gtest/gtest.h>
#include "cinn/core/stage.h"
#include "cinn/ir/ops_overload.h"

namespace cinn {
using namespace ir;

TEST(Function, basic) {
  Var i("i", 0, 100);
  Var j("j", 0, 150);

  Expr A("A"), B("B"), C("C");

  Stage s0 = C[i][j].Assign(A[i][j] * B[i][j]);
  Stage s1 = C[i][j].Assign(C[i][j] + 1);

  Expr expr = Function::make("func0", {B, C}, {A}, {s0, s1});
  Function* func0 = expr.As<Function>();

  LOG(INFO) << "func0 " << func0->DumpIslC();
}

TEST(Function, GenerateIslAst) {
  Var i("i", 0, 100);
  Var j("j", 0, 150);

  Expr A("A"), B("B"), C("C");

  Stage s0 = C[i][j].Assign(A[i][j] * B[i][j]);
  Stage s1 = C[i][j].Assign(C[i][j] + 1);

  Expr expr = Function::make("func0", {B, C}, {A}, {s0, s1});
  Function* func0 = expr.As<Function>();
  LOG(INFO) << "iter2 " << func0->iterator_domain();
  func0->GenerateIslAst();
}

}  // namespace cinn