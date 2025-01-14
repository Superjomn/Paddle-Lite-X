#include <gtest/gtest.h>
#include "cinn/backends/code_gen_c.h"
#include "cinn/core/function.h"
#include "cinn/hlir/instruction_layer/use_ops.h"
#include "cinn/hlir/op_registry.h"

namespace cinn {
namespace hlir {
namespace instruction_layer {

TEST(matmul_op, test) {
  SetGlobalContext(new CINNContext);

  auto op = OpRegistry::Global().CreateOp(HlirLayer::kInstructionWise, "matmul");
  ASSERT_TRUE(op);

  Session session;
  auto *input0 = session.NewTensor("x");
  auto *input1 = session.NewTensor("w");
  auto *output = session.NewTensor("out");

  input0->set_ptype(primitive_t::float32);
  input1->set_ptype(primitive_t::float32);
  output->set_ptype(primitive_t::float32);

  input0->set_shape({20, 30});
  input1->set_shape({30, 40});

  op->set_session(&session);

  op->SetInput("X", "x");
  op->SetInput("W", "w");
  op->SetOutput("Out", "out");

  op->Compile();

  Function fn("complex");
  {
    for (auto &stage : output->stages()) {
      fn.AddStage(stage);
    }
    fn.Inputs({input0->expr(), input1->expr()});
    fn.Outputs({output->expr()});
    fn.EndDefinition();
  }

  backends::C_CodeGen gen;
  gen.Print(fn.ir_function());

  std::string target = R"ROC(void complex (cinn_float32_t* x, cinn_float32_t* w, cinn_float32_t* out) {
  for (int c0 = 0; (c0 <= 19); c0 += 1) {
    for (int c1 = 0; (c1 <= 39); c1 += 1) {
      out[c0, c1] = 0;
    }
  }
  for (int c0 = 0; (c0 <= 19); c0 += 1) {
    for (int c1 = 0; (c1 <= 39); c1 += 1) {
      for (int c2 = 0; (c2 <= 29); c2 += 1) {
        out[c0, c1] += (x[c0, c2] * w[c2, c1]);
      }
    }
  }
})ROC";

  LOG(INFO) << "generated code:" << std::endl << gen.compiled_code() << std::endl;
  ASSERT_EQ(gen.compiled_code(), target);
}

TEST(matmul_transposed_op, test) {
  SetGlobalContext(new CINNContext);

  auto op = OpRegistry::Global().CreateOp(HlirLayer::kInstructionWise, "matmul_transposed");
  ASSERT_TRUE(op);

  Session session;
  auto *input0 = session.NewTensor("x");
  auto *input1 = session.NewTensor("w");
  auto *output = session.NewTensor("out");

  input0->set_ptype(primitive_t::float32);
  input1->set_ptype(primitive_t::float32);
  output->set_ptype(primitive_t::float32);

  input0->set_shape({20, 30});
  input1->set_shape({40, 30});

  op->set_session(&session);

  op->SetInput("X", "x");
  op->SetInput("W", "w");
  op->SetOutput("Out", "out");

  op->Compile();

  Function fn("complex");
  {
    for (auto &stage : output->stages()) {
      fn.AddStage(stage);
    }
    fn.Inputs({input0->expr(), input1->expr()});
    fn.Outputs({output->expr()});
    fn.EndDefinition();
  }

  backends::C_CodeGen gen;
  gen.Print(fn.ir_function());
  std::string target = R"ROC(void complex (cinn_float32_t* x, cinn_float32_t* w, cinn_float32_t* out) {
  for (int c0 = 0; (c0 <= 19); c0 += 1) {
    for (int c1 = 0; (c1 <= 39); c1 += 1) {
      out[c0, c1] = 0;
    }
  }
  for (int c0 = 0; (c0 <= 19); c0 += 1) {
    for (int c1 = 0; (c1 <= 39); c1 += 1) {
      for (int c2 = 0; (c2 <= 29); c2 += 1) {
        out[c0, c1] += (x[c0, c2] * w[c1, c2]);
      }
    }
  }
})ROC";

  LOG(INFO) << "generated code:" << std::endl << gen.compiled_code() << std::endl;
  ASSERT_EQ(gen.compiled_code(), target);
}

}  // namespace instruction_layer
}  // namespace hlir
}  // namespace cinn
