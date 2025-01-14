#include "cinn/ir/ir.h"
#include <glog/logging.h>
#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include "cinn/core/cinn_context.h"
#include "cinn/ir/expr.h"
#include "cinn/ir/ir_helper.h"
#include "cinn/ir/ir_printer.h"
#include "cinn/utils/isl_utils.h"
#include "cinn/utils/logging.h"
#include "cinn/utils/macros.h"
#include "cinn/utils/string.h"

namespace cinn {
namespace ir {

//-------------------- Logical expressions -------------------------
Expr EQ::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  CHECK(!b.is_unk());
  auto node = std::make_shared<EQ>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(!node->a.is_unk());
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

Expr NE::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  auto node = std::make_shared<NE>();
  CHECK_EQ(a.ptype(), b.ptype());
  CHECK(!a.is_unk());
  node->a = std::move(a);
  node->b = std::move(b);
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

Expr For::make(Expr iter_init, Expr iter_cond, Expr iter_inc, Expr body, Var iterator) {
  CHECK(iter_inc.valid());
  CHECK(iter_cond.valid());
  CHECK(iter_inc.valid());
  CHECK(body.valid());
  auto node = std::make_shared<For>();
  CHECK(!iter_init.is_unk());
  CHECK(!iter_cond.is_unk());
  CHECK(!iter_inc.is_unk());
  node->iter_init = std::move(iter_init);
  node->iter_cond = std::move(iter_cond);
  node->iter_inc = std::move(iter_inc);
  node->body = std::move(body);
  node->iterator = std::move(iterator);
  node->set_ptype(primitive_t::void_);
  return Expr(node);
}

Expr Mod::make(Expr a, Expr b) {
  CHECK(a.valid());
  CHECK(b.valid());
  auto node = std::make_shared<Mod>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(a.ptype(), b.ptype());
  CHECK(!a.is_unk());
  node->set_ptype(node->a.ptype());
  return Expr(node);
}

//! + - * /
template <typename T>
Expr MakeMathExpr(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  CHECK(!a.is_unk());
  CHECK(!b.is_unk());
  // CHECK(a.is_primitive()) << "a should be a scalar, get " << a.ctype();
  // CHECK(b.is_primitive()) << "b should be a scalar, get "<< b.ctype();

  auto node = std::make_shared<T>();
  node->a = a;
  node->b = b;

  CHECK_EQ(a.ptype(), b.ptype());
  node->set_ptype(a.ptype());
  auto expr = Expr(node);
  SetOprSimdIfAnyOprandIsSimd(&expr, node->a, node->b);
  return expr;
}

// Set the operation as SIMD if any of its oprands is SIMD data.
void SetOprSimdIfAnyOprandIsSimd(ir::Expr *op, ir::Expr &a, ir::Expr &b) {
  if (a.is_simd() && b.is_simd()) {
    CHECK_EQ(a.ctype(), b.ctype());
  }
  if (a.is_simd() || b.is_simd()) op->set_ctype(a.ctype());
}

Expr Add::make(Expr a, Expr b) { return MakeMathExpr<Add>(a, b); }

Expr Sub::make(Expr a, Expr b) { return MakeMathExpr<Sub>(a, b); }

Expr Mul::make(Expr a, Expr b) { return MakeMathExpr<Mul>(a, b); }

Expr Div::make(Expr a, Expr b) { return MakeMathExpr<Div>(a, b); }

Expr Min::make(Expr a, Expr b) {
  CHECK(a.valid());
  CHECK(b.valid());
  auto node = std::make_shared<Min>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(!node->a.is_unk());
  node->set_ptype(a.ptype());
  return Expr(node);
}

Expr Max::make(Expr a, Expr b) {
  CHECK(a.valid());
  CHECK(b.valid());
  auto node = std::make_shared<Max>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(!node->a.is_unk());
  node->set_ptype(node->a.ptype());
  return Expr(node);
}

Expr Minus::make(Expr a) {
  CHECK(a.valid());
  auto node = std::make_shared<Minus>();
  node->a = a;
  CHECK(!node->a.is_unk());
  node->set_ptype(a.ptype());
  return Expr(node);
}

template <>
int32_t Constant::As<int32_t>() const {
  CHECK_EQ(ptype(), primitive_t::int32);
  return int32_val_;
}
template <>
float Constant::As<float>() const {
  CHECK(ptype() == primitive_t::float32);
  return float32_val_;
}
template <>
int64_t Constant::As<int64_t>() const {
  CHECK(ptype() == primitive_t::int64);
  return int64_val_;
}

template <>
double Constant::As<double>() const {
  CHECK(ptype() == primitive_t::float64);
  return float64_val_;
}

unsigned int Constant::counter = 0;

std::string Constant::__str__() const {
  switch (ptype()) {
    case primitive_t::float32:
      return std::to_string(As<float>()) + "fp32";
    case primitive_t::int8:
      return std::to_string(As<int32_t>()) + "i8";
    case primitive_t::int32:
      return std::to_string(As<int32_t>()) + "i32";
    case primitive_t::int64:
      return std::to_string(As<int64_t>()) + "i64";
    default:
      LOG(FATAL) << "not supported type " << static_cast<int>(ptype());
  }
  return "Parameter-UNK";
}

Constant::Constant(const Constant &other) {
  name_ = other.name_;
  value_set_ = other.value_set_;
  set_ptype(other.ptype());
  switch (ptype()) {
    case primitive_t::int8:
      int8_val_ = other.int8_val_;
      break;
    case primitive_t::int32:
      int32_val_ = other.int32_val_;
      break;
    case primitive_t::int64:
      int64_val_ = other.int64_val_;
      break;
    case primitive_t::float32:
      float32_val_ = other.float32_val_;
      break;
    case primitive_t::float64:
      float64_val_ = other.float64_val_;
      break;
    case primitive_t::unk:
      break;
    default:
      LOG(FATAL) << "unsupported type " << static_cast<int>(ptype());
  }
}

Constant::operator Expr() {
  auto node = std::make_shared<Constant>(*this);
  return Expr(node);
}

bool Constant::operator==(const Constant &other) const {
  // If no value is set, check their name.
  if (!name_.empty() && name_ == other.name_) return true;
  // Check the actual value.
  if (ptype() != other.ptype()) return false;

  switch (ptype()) {
    case primitive_t::float32:
      return As<float>() == other.As<float>();
    case primitive_t::int32:
      return As<int32_t>() == other.As<int32_t>();
    case primitive_t::int64:
      return As<int64_t>() == other.As<int64_t>();
    case primitive_t::float64:
      return As<double>() == other.As<double>();
    case primitive_t::unk:
      return true;

    default:
      LOG(FATAL) << "unsupported primitive type: " << ptype();
  }
  return false;
}

int64_t Constant::int_val() const {
  CHECK(is_integer());
  switch (ptype()) {
    case primitive_t::int32:
      return int32_val_;
    case primitive_t::int16:
      return int16_val_;
    case primitive_t::int64:
      return int64_val_;
  }
}

Expr LT::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  auto node = std::make_shared<LT>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(!node->a.is_unk());
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

Expr LE::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  auto node = std::make_shared<LE>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(!node->a.is_unk());
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

Expr GT::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  auto node = std::make_shared<GT>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(!node->a.is_unk());
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

Expr GE::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  auto node = std::make_shared<GE>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(!node->a.is_unk());
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

std::set<std::string> Var::name_set_;

Expr Block::make(std::vector<Expr> &&list) {
  for (auto &v : list) {
    CHECK(v.valid());
  }
  auto node = std::make_shared<Block>();
  node->body = std::move(list);
  node->set_ptype(primitive_t::void_);
  return Expr(node);
}

Expr And::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  auto node = std::make_shared<And>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(node->a.is_boolean());
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

Expr Or::make(Expr a, Expr b) {
  CHECK(a.valid()) << "Expr a not defined";
  CHECK(b.valid()) << "Expr b not defined";
  auto node = std::make_shared<Or>();
  node->a = std::move(a);
  node->b = std::move(b);
  CHECK_EQ(node->a.ptype(), node->b.ptype());
  CHECK(node->a.is_boolean());
  node->set_ptype(primitive_t::boolean);
  return Expr(node);
}

Expr IfThenElse::make(Expr condition, Expr true_block) {
  auto node = std::make_shared<IfThenElse>();
  node->condition = condition;
  node->true_block = true_block;
  node->set_ptype(primitive_t::void_);
  return Expr(node);
}

Expr IfThenElse::make(Expr condition, Expr true_block, Expr false_block) {
  auto node = std::make_shared<IfThenElse>();
  node->condition = condition;
  node->true_block = true_block;
  node->false_block = false_block;
  node->set_ptype(primitive_t::void_);
  return Expr(node);
}

Var::operator Expr() const {
  auto node =
      std::make_shared<Var>(data_->name_, ptype(), data_->interval_.lower_bound(), data_->interval_.upper_bound());
  node->set_ctype(ctype());
  node->set_is_reference(is_reference());
  return Expr(node);
}

bool Var::CheckNameValid(const std::string &name) {
  if (!name_set_.count(name)) {
    name_set_.insert(name);
    return true;
  }
  return false;
}

Var::Var(const std::string &name, int32_t lower_bound, int32_t upper_bound) {
  InitData();
  data_->name_ = name;
  data_->interval_ = Interval(lower_bound, upper_bound);
  set_ptype(primitive_t::int32);
  CheckNameValid(name);
}

Var::Var() {
  InitData();
  // set as iterator by default.
  set_ptype(primitive_t::int32);
  data_->name_ = GlobalContext().name_generator().NewIteratorName();
}

Var::Var(const std::string &name, primitive_t dtype) {
  InitData();
  data_->name_ = name;
  CheckNameValid(name);
  set_ptype(dtype);
}

Var::Var(const std::string &name, primitive_t type, const Interval &interval) {
  InitData();
  data_->name_ = name;
  set_ptype(type);
  data_->interval_ = interval;
  CheckNameValid(data_->name_);
}

Var::Var(const std::string &name, primitive_t type, Constant lower_bound, Constant upper_bound) {
  InitData();
  data_->name_ = name;
  set_ptype(type);
  data_->interval_ = Interval(lower_bound, upper_bound);
  CheckNameValid(name);
}

Expr Call::make(const std::string &caller, std::vector<Expr> arguments) {
  for (auto &v : arguments) {
    CHECK(v.valid());
    CHECK(!v.is_unk());
  }

  auto node = std::make_shared<Call>();
  node->caller = caller;
  node->arguments = arguments;
  node->set_ptype(primitive_t::void_);
  return Expr(node);
}

Expr Reference::make(Expr expr, const std::vector<Expr> &iterators) {
  CHECK(expr.valid());
  auto x = std::make_shared<Reference>();
  x->target = expr;
  CHECK(!expr.is_unk());
  for (const Expr &iterator : iterators) {
    CHECK(iterator.valid());
    x->iterators.push_back(iterator);
    CHECK(!iterator.is_unk());
  }

  x->set_ptype(expr.ptype());
  return Expr(x);
}

Expr Expr::operator=(const Expr &other) {
  if (!valid() || type() != NodeTy::Reference) {
    ptr_ = other.ptr_;
  } else {
    CHECK(other.valid());
    auto assign = Assign::make(*this, other);
    // reset the pointer
    this->ptr_ = assign.ptr_;
  }
  return *this;
}

Expr Expr::operator+=(const Expr &other) {
  if (!valid() || type() != NodeTy::Reference) {
    ptr_ = other.ptr_;
  } else {
    CHECK(other.valid());
    auto assign = SumAssign::make(*this, other);
    // reset the pointer
    this->ptr_ = assign.ptr_;
  }
  return *this;
}

Expr Expr::operator-=(const Expr &other) {
  if (!valid() || type() != NodeTy::Reference) {
    ptr_ = other.ptr_;
  } else {
    CHECK(other.valid());
    auto assign = SubAssign::make(*this, other);
    // reset the pointer
    this->ptr_ = assign.ptr_;
  }
  return *this;
}

Expr Expr::operator*=(const Expr &other) {
  if (!valid() || type() != NodeTy::Reference) {
    ptr_ = other.ptr_;
  } else {
    CHECK(other.valid());
    auto assign = MulAssign::make(*this, other);
    // reset the pointer
    this->ptr_ = assign.ptr_;
  }
  return *this;
}

Expr Expr::operator/=(const Expr &other) {
  if (!valid() || type() != NodeTy::Reference) {
    ptr_ = other.ptr_;
  } else {
    CHECK(other.valid());
    auto assign = DivAssign::make(*this, other);
    // reset the pointer
    this->ptr_ = assign.ptr_;
  }
  return *this;
}

Expr Expr::Assign(Expr other) const { return Assign::make(*this, other); }
Expr Expr::SumAssign(Expr other) const { return SumAssign::make(*this, other); }
Expr Expr::SubAssign(Expr other) const { return SubAssign::make(*this, other); }
Expr Expr::MulAssign(Expr other) const { return MulAssign::make(*this, other); }
Expr Expr::DivAssign(Expr other) const { return DivAssign::make(*this, other); }

template <typename T>
Expr XAssignMake(Expr a, Expr b) {
  CHECK(a.valid());
  CHECK(b.valid());
  auto node = std::make_shared<T>();
  node->a = a;
  node->b = b;
  CHECK(!node->b.is_unk()) << "expr: " << node->b;
  node->a.set_ptype(node->b.ptype());
  node->set_ptype(node->b.ptype());
  return Expr(node);
}

Expr Assign::make(Expr a, Expr b) { return XAssignMake<Assign>(a, b); }
Expr SumAssign::make(Expr a, Expr b) { return XAssignMake<SumAssign>(a, b); }
Expr MulAssign::make(Expr a, Expr b) { return XAssignMake<MulAssign>(a, b); }
Expr DivAssign::make(Expr a, Expr b) { return XAssignMake<DivAssign>(a, b); }
Expr SubAssign::make(Expr a, Expr b) { return XAssignMake<SubAssign>(a, b); }

Expr Expr::operator[](Expr i) {
  LOG_INDENT(6);
  auto vars = CollectVarsFromExpr(i);
  // CHECK_LE(vars.size(), 1UL) << "Currently only support at most one variable in a dimension";

  const bool is_var_iterator = !vars.empty();

  // set iterator's ptype
  if (is_var_iterator) {
    for (auto &var : vars) {
      const_cast<Var *>(var)->set_ptype(primitive_t::int32);
    }
  }

  // The reference node is initialized and has at least one iterator, append the new vars.
  if (ptr() && type() == ir::NodeTy::Reference) {
    As<Reference>()->iterators.push_back(i);
    if (is_var_iterator) InferenceIteratorDomain();
    return *this;
  }

  // The reference node is newly created.
  auto node = Reference::make(*this, {i});
  Expr result(node);

  if (!vars.empty()) result.InferenceIteratorDomain();

  return result;
}

bool Expr::is_op() const {
  CHECK(valid());
#define OP_COND(T) type() == NodeTy::T ||
  return valid() && (OP_2_ARGS_FOR_EACH(OP_COND)  //
                     false);
}

class IntervalExtractor : public IRVisitor {
 public:
  explicit IntervalExtractor(std::vector<Reference::interval_tuple_t> *intervals) : intervals_(intervals) {}

  void Visit(const Expr *op) override { IRVisitor::Visit(op); }

  void Visit(const Var *op) override {
    CHECK(op->interval().lower_bound().ptype() == primitive_t::int32);
    CHECK(op->interval().upper_bound().ptype() == primitive_t::int32);
    auto it = std::find_if(intervals_->begin(), intervals_->end(), [&](const Reference::interval_tuple_t &x) {
      return std::get<0>(x) == op->name();
    });
    if (it == intervals_->end()) {
      intervals_->push_back(std::make_tuple(op->name(), op->interval()));
      LOG(INFO) << "get interval: " << op->name() << " " << op->interval().__str__();
    }
  }

 private:
  std::vector<Reference::interval_tuple_t> *intervals_;
};

std::vector<Reference::interval_tuple_t> Reference::ExtractIntervals() {
  CHECK(!iterators.empty()) << "At least one iterator is required";
  std::vector<Reference::interval_tuple_t> intervals;
  IntervalExtractor extractor(&intervals);
  for (auto &o : iterators) {
    extractor.Visit(&o);
  }
  return intervals;
}

Expr Allocate::make(const std::string &buffer_name, Expr size, primitive_t dtype) {
  auto node = std::make_shared<Allocate>();
  CHECK_EQ(size.ptype(), primitive_t::int32);
  node->buffer_name = buffer_name;
  node->size = size;
  node->dtype = dtype;
  node->set_ptype(primitive_t::void_);
  return Expr(node);
}

void Expr::InferenceIteratorDomain() {
  LOG_INDENT(5);
  CINN_DEBUG(3) << "expr: " << ir::Dump(*this);
  isl::union_set result;
  // extract iterator from the expr.
  if (!this->is_reference()) return;
  CHECK(this->is_reference()) << "type is " << this->type();
  auto *ref = this->As<Reference>();
  if (!ref->target.is_tensor()) return;
  auto *tensor = ref->target.As<Tensor>();
  CHECK_LE(ref->iterators.size(), tensor->dims().size());
  // CINN_DEBUG(6) << "Reference " << ir::Dump(ref->target) << " has " << ref->iterators.size() << " iterators";
  if (ref->iterators.size() == tensor->dims().size()) {
    ref->domain = BuildDomainFromExprWithDimension(ref->iterators, tensor->dims());
    CINN_DEBUG(3) << "set reference's domain: " << ref->domain;
  }
}

Expr Expr::operator()(const std::vector<Expr> &iters) {
  auto node = std::make_shared<Reference>();
  node->target = *this;
  node->iterators = iters;
  // inference the iterators domain

  return Expr(node);
}

Expr::Expr(const std::vector<Constant> &dims, primitive_t ptype = primitive_t::float32, const std::string &name) {
  for (auto &dim : dims) {
    CHECK(dim.is_integer());
  }

  *this = Tensor::make(dims, ptype, name);
}

std::vector<Var> ExtractVarsFromExpr(const Expr &expr) {
  class Collector : public IRVisitor {
   public:
    void Visit(const Expr *op) override { IRVisitor::Visit(op); }
    void Visit(const Var *op) override { IRVisitor::Visit(op); }
  };
  return std::vector<Var>();
}

isl::set BuildDomainFromDimensions(const std::vector<Constant> &dims, const std::vector<std::string> &iterators) {
  LOG_INDENT(0);
  CHECK(!dims.empty());

  std::vector<std::string> constraints;
  std::set<std::string> params;
  for (size_t i = 0; i < dims.size(); i++) {
    // collect constraints
    CHECK(dims[i].is_integer());
    std::string constraint;
    if (dims[i].value_set()) {
      constraint = StringFormat("0<= %s <%d", iterators[i].c_str(), dims[i].int32_val());
    } else {
      constraint = StringFormat("0<= %s <%s", iterators[i].c_str(), dims[i].name().c_str());
      params.insert(dims[i].name());
    }
    CINN_DEBUG(2) << "constraint: " << constraint;
    constraints.push_back(constraint);
  }
  if (params.empty()) params.insert("");

  std::string repr = StringFormat("[%s] -> { [%s] : %s }",
                                  Concat(params, ", ").c_str(),
                                  Concat(iterators, ", ").c_str(),
                                  Concat(constraints, " and ").c_str());
  CINN_DEBUG(3) << "repr: " << repr;

  isl::set result(isl_utils::global_isl_ctx(), repr);
  CINN_DEBUG(3) << "get domain " << result;

  return result;
}

isl::set BuildDomainFromExprWithDimension(const std::vector<Expr> &exprs, const std::vector<Constant> &dimensions) {
  LOG_INDENT(6);
  CHECK_EQ(exprs.size(), dimensions.size());

  std::vector<std::string> iterator_vars;
  std::set<std::string> iterator_var_set;
  std::vector<std::string> dim_alias;
  // collect var for each iterator expr and geneate the statement for each expr.
  for (size_t i = 0; i < exprs.size(); i++) {
    CINN_DEBUG(3) << "expr: " << ir::Dump(exprs[i]);
    auto vars = CollectVarsFromExpr(exprs[i]);
    // std::set<std::string> iter_names;
    for (auto &var : vars) iterator_var_set.insert(var->name());
    // CHECK_EQ(iter_names.size(), 1UL);
    // std::vector<std::string> sorted(iter_names.begin(), iter_names.end());
    // iterator_vars.push_back(sorted.front());
    dim_alias.push_back(GenIndexedIteratorName(i));
  }

  iterator_vars.assign(iterator_var_set.begin(), iterator_var_set.end());

  isl::set alias_domain = BuildDomainFromDimensions(dimensions, dim_alias);
  CINN_DEBUG(3) << "alias domain: " << alias_domain;
  isl::union_map ts;

  std::vector<std::string> iterators, targets, alias, alias_eq;

  for (size_t i = 0; i < dimensions.size(); i++) {
    targets.push_back(ir::Dump(exprs[i]));
    alias.push_back(dim_alias[i]);
    // alias_eq.push_back(targets[i] + "=" + dim_alias[i]);
    alias_eq.push_back(dim_alias[i] + "=" + ir::Dump(exprs[i]));
  }

  std::string repr = StringFormat("{ [%s] -> [%s] : %s }",
                                  Concat(alias, ", ").c_str(),
                                  Concat(iterator_vars, ", ").c_str(),
                                  Concat(alias_eq, " and ").c_str());
  CINN_DEBUG(3) << "repr " << repr;
  isl::map transforms(isl_utils::global_isl_ctx(), repr.c_str());

  isl::set result = alias_domain.apply(transforms);

  CINN_DEBUG(1) << "finial domain: " << result;
  return result;
}

std::string GenIndexedIteratorName(int id) { return StringFormat("ii%d", id); }

std::ostream &operator<<(std::ostream &os, SIMDOpr::Opr opr) {
  switch (opr) {
#define __(type__)              \
  case SIMDOpr::Opr::k##type__: \
    os << "simd-" #type__;      \
    return os;

    __(Add);
    __(Sub);
    __(Mul);
    __(Div);
    __(Min);
    __(Max);
    __(Store);
    __(Load);
    __(ReduceAdd);
    default:
      NOT_IMPLEMENT

#undef __
  }
}

std::string Interval::__str__() const {
  std::stringstream ss;
  ss << "Interval";
  if (lower_bound().valid()) ss << "(" << lower_bound().__str__();
  if (upper_bound().valid()) ss << ", " << upper_bound().__str__() << ")";
  return ss.str();
}

Expr BufferOpr::make(Target target, Expr size, Opr operation, primitive_t type, const std::string &name) {
  auto buffer = std::make_shared<BufferOpr>();
  buffer->target = target;
  buffer->size = size;
  buffer->operation = operation;
  buffer->name = name.empty() ? GlobalContext().name_generator().NewBuffer() : name;
  buffer->set_ptype(type);
  return Expr(buffer);
}

Expr Let::make(Expr a, Expr b) {
  auto node = std::make_shared<Let>();
  node->a = a;
  node->b = b;
  CHECK(!b.is_unk());
  node->set_ptype(b.ptype());
  node->set_ctype(b.ctype());
  a.set_ptype(b.ptype());
  a.set_ctype(b.ctype());
  return Expr(node);
}

#define __(t__)                                                   \
  template <>                                                     \
  void Constant::SetValue<t__##_t>(t__##_t v) {                   \
    if (ptype() == primitive_t::unk) set_ptype(primitive_t::t__); \
    CHECK(ptype() == primitive_t::t__);                           \
    value_set_ = true;                                            \
    t__##_val_ = v;                                               \
  }

__(int32);
__(int64);
__(float32);
__(float64);

Expr Exp::make(Expr a) {
  CHECK(!a.is_unk());
  auto node = std::make_shared<Exp>();
  node->a = a;
  node->set_ptype(a.ptype());
  return Expr(node);
}

Expr Tensor::make(const std::vector<Constant> &dims, primitive_t type, const std::string &name) {
  auto node = std::make_shared<Tensor>(name.empty() ? GlobalContext().name_generator().NewVarName() : name, type, dims);
  return Expr(node);
}

Expr Array::make(Expr size, primitive_t ptype, const std::string &name) {
  auto node = std::make_shared<Array>();
  node->size = size;
  node->set_ptype(ptype);
  node->name = name.empty() ? GlobalContext().name_generator().NewArray() : name;
  CHECK(CheckExprIsConstant(node->size));
  return Expr(node);
}

Expr SIMDOpr::make(int vector_width, SIMDOpr::Opr opr, Expr a, Expr b) {
  auto node = std::make_shared<SIMDOpr>();
  CHECK(vector_width == 4 || vector_width == 8);

  switch (opr) {
    case Opr::kAdd:
    case Opr::kSub:
    case Opr::kMul:
    case Opr::kDiv:
    case Opr::kMax:
    case Opr::kMin:
      node->vector_width = vector_width;
      node->opr = opr;
      node->a = a;
      node->b = b;
      node->set_ptype(node->a.ptype());
      if (node->vector_width == 4)
        node->set_ctype(composite_t::simd128);
      else if (node->vector_width == 8)
        node->set_ctype(composite_t::simd256);
      else
        NOT_IMPLEMENT

      return Expr(node);

    case Opr::kStore:
      return make_store(vector_width, a, b);
    case Opr::kLoad:
      return make_load(vector_width, a);
    default:
      NOT_IMPLEMENT
  }

  // CHECK(a.is_simd()) << "both oprand of SIMD should be SIMD too, get " << a;
  // CHECK(b.is_simd()) << "both oprand of SIMD should be SIMD too, get " << b;
}

Expr SIMDOpr::make_load(int vector_width, Expr a) {
  CHECK(a.is_impl_normal());
  CHECK(a.is_primitive());

  auto node = std::make_shared<SIMDOpr>();
  node->opr = Opr::kLoad;
  node->set_ptype(a.ptype());
  node->a = a;
  node->vector_width = vector_width;
  a.set_impl_as_address();
  node->opr = SIMDOpr::Opr::kLoad;

  // set ctype
  switch (vector_width) {
    case 4:
      node->set_ctype(composite_t::simd128);
      break;
    case 8:
      node->set_ctype(composite_t::simd256);
      break;
    default:
      NOT_IMPLEMENT
  }

  return Expr(node);
}

Expr SIMDOpr::make_store(int vector_width, Expr a, Expr b) {
  CHECK(a.valid());
  CHECK(b.valid());
  CHECK(b.is_impl_normal());
  CHECK(b.is_simd());
  CHECK(a.ptype() == b.ptype());

  auto node = std::make_shared<SIMDOpr>();
  node->opr = Opr::kStore;
  node->a = a;
  node->b = b;
  node->vector_width = vector_width;
  node->set_ptype(b.ptype());
  node->set_ctype(ToSimdType(vector_width));
  return Expr(node);
}

Expr SIMDOpr::make_reduce_add(int vector_width, Expr a) {
  CHECK(a.is_simd());
  CHECK(a.is_impl_normal());

  auto node = std::make_shared<SIMDOpr>();
  node->opr = Opr::kReduceAdd;
  node->a = a;
  node->set_ptype(a.ptype());
  node->set_ctype(composite_t::primitive);
  node->vector_width = vector_width;
  return Expr(node);
}

Expr Cast::make(Expr expr, primitive_t type, composite_t ctype) {
  CHECK(CheckPTypeCastable(expr.ptype(), type));
  CHECK(!(expr.ptype() == type && expr.ctype() == ctype)) << "no necessary cast found";
  CHECK_NE(type, primitive_t::unk);
  auto node = std::make_shared<Cast>();
  node->expr = expr;
  node->set_ptype(type);
  node->set_ctype(ctype);
  return Expr(node);
}

Expr Mark::make(const std::string &content) {
  auto node = std::make_shared<Mark>();
  node->content = content;
  return Expr(node);
}

Expr Identity::make(ir::Expr expr, const std::string &id) {
  auto node = std::make_shared<Identity>();
  node->expr = expr;
  node->id = id;
  node->set_ptype(expr.ptype());
  node->set_ctype(expr.ctype());
  return Expr(node);
}

Expr Identity::GetTrimedExpr(std::vector<std::string> *ids) const {
  if (ids) ids->push_back(id);
  Expr result = expr;
  auto *e = expr.As<ir::Identity>();
  while (e) {
    if (ids) ids->push_back(e->id);
    e = e->expr.As<ir::Identity>();
    result = e->expr;
  }
  return result;
}

bool Identity::marked_as_address() const { return id == expr_ids::reference_address; }

Expr CallOnce::make(Expr block) {
  auto node = std::make_shared<CallOnce>();
  node->block = block;
  node->cond_var_name = GlobalContext().name_generator().NewTmpVar();
  return Expr(node);
}

Expr Module::make(Expr data_section, Expr function_section) {
  auto node = std::make_shared<Module>();
  node->global_data_section = data_section;
  node->function_section = function_section;
  return Expr(node);
}
}  // namespace ir
}  // namespace cinn
