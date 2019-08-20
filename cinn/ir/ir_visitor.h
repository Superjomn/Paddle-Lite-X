#pragma once

namespace cinn {
namespace ir {

struct Var;
struct Stmt;
struct Expr;
struct Add;
struct Sub;
struct Div;
struct Mul;
struct Mod;

struct NE;
struct EQ;
struct LE;
struct LT;
struct GT;
struct GE;
struct For;

struct IntImm;
struct FloatImm;

struct Tensor;
struct Parameter;
struct Reference;
struct Var;

/// Visitor pattern for IR nodes. The default one just visit their children.
class IRVisitor {
 public:
  IRVisitor() = default;

  virtual void Visit(const Expr* op);
  virtual void Visit(const Stmt* op);
  virtual void Visit(const Add* op);
  virtual void Visit(const Sub* op);
  virtual void Visit(const Mul* op);
  virtual void Visit(const Div* op);
  virtual void Visit(const Mod* op);

  virtual void Visit(const NE* op);
  virtual void Visit(const EQ* op);
  virtual void Visit(const For* op);
  virtual void Visit(const GT* op);
  virtual void Visit(const GE* op);
  virtual void Visit(const LT* op);
  virtual void Visit(const LE* op);

  virtual void Visit(const IntImm* op);
  virtual void Visit(const FloatImm* op);

  virtual void Visit(const Tensor* op);
  virtual void Visit(const Parameter* op);
  virtual void Visit(const Var* op);
  virtual void Visit(const Reference* op) {}
};

}  // namespace ir
}  // namespace cinn
