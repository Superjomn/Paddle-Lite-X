#include "cinn/core/isl_code_gen.h"
#include <stack>
#include <utility>
#include "cinn/core/stage.h"
#include "cinn/ir/expr.h"
#include "cinn/ir/ir.h"
#include "cinn/ir/ir_helper.h"
#include "cinn/ir/ir_mutator.h"
#include "cinn/ir/ir_printer.h"
#include "cinn/utils/logging.h"
#include "cinn/utils/macros.h"

namespace cinn {

using ir::Expr;

std::vector<std::string> forloop_indice_stack;

//! Eat an isl block node.
void EatBlock(const isl::ast_node& node, ir::Expr* expr);
//! Eat an isl user node.
void EatUser(const isl::ast_node& node, ir::Expr* expr);
//! Eat an isl for node.
void EatFor(const isl::ast_node& node, ir::Expr* expr);
//! Eat an isl `if` node.
void EatIf(const isl::ast_node& node, ir::Expr* expr);
//! Eat an isl mark node.
void EatMark(const isl::ast_node& node, ir::Expr* expr);

void IslAstNodeToCinnExpr(const isl::ast_node& node, ir::Expr* expr) {
  LOG_INDENT(6);
  CHECK(!node.is_null());
  CHECK(expr);

  switch (isl_ast_node_get_type(node.get())) {
    case isl_ast_node_block: {
      CINN_DEBUG(3) << "get isl block node";
      EatBlock(node, expr);
    } break;
    case isl_ast_node_for: {
      CINN_DEBUG(3) << "get isl for node";
      EatFor(node, expr);
    } break;
    case isl_ast_node_if: {
      CINN_DEBUG(3) << "get isl if node";
      EatIf(node, expr);
    } break;
    case isl_ast_node_user: {
      CINN_DEBUG(3) << "get isl user node";
      EatUser(node, expr);
    } break;
    case isl_ast_node_mark: {
      CINN_DEBUG(3) << "get isl mark";
      EatMark(node, expr);
    } break;
    default:
      LOG(FATAL) << "Unexpected ISL node type " << isl_ast_node_get_type(node.get());
      break;
  }
}

// Eat an isl block node.
void EatBlock(const isl::ast_node& node, ir::Expr* expr) {
  VLOG(2) << "get isl ast body node";
  CHECK(!node.is_null());
  CHECK(expr);
  CHECK_EQ(isl_ast_node_get_type(node.get()), isl_ast_node_block);
  isl::ast_node_list list = isl::manage(isl_ast_node_block_get_children(node.get()));
  std::vector<ir::Expr> exprs;
  // for (int i = isl_ast_node_list_n_ast_node(list.get()) - 1; i >= 0; i--) {
  for (int i = 0; i < isl_ast_node_list_n_ast_node(list.get()); i++) {
    isl::ast_node child = isl::manage(isl_ast_node_list_get_ast_node(list.get(), i));
    // visit child
    ir::Expr child_expr;
    IslAstNodeToCinnExpr(child, &child_expr);
    exprs.push_back(child_expr);
  }
  *expr = ir::Block::make(std::move(exprs));
}
// Eat an isl user node.
void EatUser(const isl::ast_node& node, ir::Expr* expr) {
  CHECK_EQ(isl_ast_node_get_type(node.get()), isl_ast_node_user);
  isl::ast_expr isl_expr = isl::manage(isl_ast_node_user_get_expr(node.get()));
  IslAstExprToCinnExpr(isl_expr, expr);
}
// Eat an isl `for` node.
void EatFor(const isl::ast_node& node, ir::Expr* expr) {
  LOG_INDENT(6);
  CHECK_EQ(isl_ast_node_get_type(node.get()), isl_ast_node_for);
  CINN_DEBUG(5) << "get isl ast for node";

  // iter name
  isl::ast_expr iter = isl::manage(isl_ast_node_for_get_iterator(node.get()));
  isl::id iter_id = isl::manage(isl_ast_expr_get_id(iter.get()));
  std::string iter_name = iter_id.name();
  CINN_DEBUG(5) << "For iter: " << iter_name;

  // get condition
  isl::ast_expr condition = isl::manage(isl_ast_node_for_get_cond(node.get()));
  isl::ast_expr incrementor = isl::manage(isl_ast_node_for_get_inc(node.get()));
  isl::ast_expr initializer = isl::manage(isl_ast_node_for_get_init(node.get()));
  isl::ast_node body = isl::manage(isl_ast_node_for_get_body(node.get()));

  ir::Expr ir_body;
  IslAstNodeToCinnExpr(body, &ir_body);
  ir_body = ir::Block::make({ir_body});
  CINN_DEBUG(5) << "for get body " << ir_body;

  ir::Expr ir_initializer;
  IslAstExprToCinnExpr(initializer, &ir_initializer);
  CINN_DEBUG(5) << "for get initializer " << ir_initializer;

  ir::Expr ir_condition;
  IslAstExprToCinnExpr(condition, &ir_condition);
  ir::Expr tmp;

  isl::ast_expr arg = isl::manage(isl_ast_expr_get_op_arg(condition.get(), 1));
  IslAstExprToCinnExpr(arg, &tmp);
  CINN_DEBUG(5) << "for get condition " << ir_condition;

  ir::Expr ir_inc;
  IslAstExprToCinnExpr(incrementor, &ir_inc);
  CINN_DEBUG(5) << "for get inc " << ir_inc;

  ir::Var ir_iter(iter_name, primitive_t::float32);
  CINN_DEBUG(5) << "for get iter  " << ir_iter;

  *expr = ir::For::make(ir_initializer, ir_condition, ir_inc, ir_body, ir_iter);
}

void EatIf(const isl::ast_node& node, ir::Expr* expr) {
  CHECK_EQ(isl_ast_node_get_type(node.get()), isl_ast_node_if);
  isl::ast_node then_body = isl::manage(isl_ast_node_if_get_then(node.get()));
  isl::ast_expr condition = isl::manage(isl_ast_node_if_get_cond(node.get()));

  ir::Expr ir_then_body;
  IslAstNodeToCinnExpr(then_body, &ir_then_body);

  ir::Expr ir_else_body;
  if (isl_bool_true == isl_ast_node_if_has_else(node.get())) {
    isl::ast_node else_body = isl::manage(isl_ast_node_if_get_else(node.get()));
    IslAstNodeToCinnExpr(else_body, &ir_else_body);
  }

  ir::Expr ir_condition;
  IslAstExprToCinnExpr(condition, &ir_condition);

  if (ir_else_body.valid()) {
    *expr = ir::IfThenElse::make(ir_condition, ir_then_body, ir_else_body);
  } else {
    *expr = ir::IfThenElse::make(ir_condition, ir_then_body);
  }
}

void EatMark(const isl::ast_node& node, ir::Expr* expr) {
  Expr mark = ir::Mark::make(isl_id_get_name(isl_ast_node_mark_get_id(node.get())));
  Expr child;
  auto child_node = isl::manage(isl_ast_node_mark_get_node(node.get()));
  IslAstNodeToCinnExpr(child_node, &child);
  *expr = ir::Block::make({mark, child});
}

void IslAstExprToCinnExpr(const isl::ast_expr& node, ir::Expr* expr) {
  switch (isl_ast_expr_get_type(node.get())) {
    case isl_ast_expr_int: {
      isl::val val = isl::manage(isl_ast_expr_get_val(node.get()));
      *expr = ir::Expr(static_cast<int>(isl_val_get_num_si(val.get())));
    } break;
    case isl_ast_expr_id: {
      isl::id id = isl::manage(isl_ast_expr_get_id(node.get()));
      *expr = ir::Var(id.name());
    } break;
    case isl_ast_expr_op: {
      std::vector<ir::Expr> ops;
      const int n_args = isl_ast_expr_get_op_n_arg(node.get());

      for (int i = 0; i < n_args; i++) {
        ir::Expr op;
        isl::ast_expr expr0 = isl::manage(isl_ast_expr_get_op_arg(node.get(), i));
        IslAstExprToCinnExpr(expr0, &op);
        ops.push_back(op);
      }

      auto set_ops_ptype = [&](primitive_t type) {
        for (auto& op : ops) {
          op.set_ptype(type);
        }
      };

      // set ops as int32 by default.
      set_ops_ptype(primitive_t::int32);

      isl_ast_op_type op_type = isl_ast_expr_get_op_type(node.get());
      switch (op_type) {
        case isl_ast_op_and: {
          set_ops_ptype(primitive_t::boolean);
          *expr = ir::And::make(ops[0], ops[1]);
        } break;
        case isl_ast_op_or:
          *expr = ir::Or::make(ops[0], ops[1]);
          break;
        case isl_ast_op_min:
          *expr = ir::Min::make(ops[0], ops[1]);
          break;
        case isl_ast_op_max:
          *expr = ir::Max::make(ops[0], ops[1]);
          break;
        case isl_ast_op_minus:
          *expr = ir::Minus::make(ops[0]);
          break;
        case isl_ast_op_add:
          *expr = ir::Add::make(ops[0], ops[1]);
          break;
        case isl_ast_op_sub:
          *expr = ir::Sub::make(ops[0], ops[1]);
          break;
        case isl_ast_op_mul:
          *expr = ir::Mul::make(ops[0], ops[1]);
          break;
        case isl_ast_op_div:
          *expr = ir::Div::make(ops[0], ops[1]);
          break;
        case isl_ast_op_le:
          *expr = ir::LE::make(ops[0], ops[1]);
          break;
        case isl_ast_op_lt:
          *expr = ir::LT::make(ops[0], ops[1]);
          break;
        case isl_ast_op_ge:
          *expr = ir::GE::make(ops[0], ops[1]);
          break;
        case isl_ast_op_gt:
          *expr = ir::GT::make(ops[0], ops[1]);
          break;
        case isl_ast_op_eq:
          *expr = ir::EQ::make(ops[0], ops[1]);
          break;
        case isl_ast_op_call: {
          ir::Expr caller_expr = ops.front();
          // TODO(Superjomn) make it an string
          CHECK(caller_expr.type() == ir::NodeTy::Var);
          std::string caller = caller_expr.As<ir::Var>()->name();
          ops.erase(ops.begin());
          *expr = ir::Call::make(caller, ops);
        } break;
        case isl_ast_op_fdiv_q:
          *expr = ir::Div::make(ops[0], ops[1]);
          break;
        default:
          LOG(FATAL) << "unsupported op " << op_type;
      }
    } break;
    default:
      break;
  }
}

// TODO(Superjomn) to remove the access argument
isl::ast_expr CreateIslAstIndexExpression(isl_ast_build* build, const isl::map& access) {
  CHECK(build);
  LOG_INDENT(6);
  isl::map schedule = isl::manage(isl_map_from_union_map(isl_ast_build_get_schedule(build)));

  // get identity access from schedule.
  CINN_DEBUG(2) << "schedule: " << schedule;
  auto statement = isl_map_get_statement_repr(schedule.get(), isl_dim_in);
  auto statement_set = isl::manage(
      isl_set_read_from_str(isl_map_get_ctx(schedule.get()), StringFormat("{ %s : }", statement.c_str()).c_str()));
  auto identity_access = isl::manage(isl_set_identity(statement_set.release()));

  isl::map map = isl::manage(isl_map_reverse(schedule.copy()));
  CINN_DEBUG(2) << "schedule reversed: " << map;

  isl::pw_multi_aff iterator_map = isl::manage(isl_pw_multi_aff_from_map(map.copy()));
  CINN_DEBUG(2) << "iterator_map: " << iterator_map;

  isl::pw_multi_aff index_aff = isl::manage(isl_pw_multi_aff_from_map(identity_access.copy()));
  CINN_DEBUG(2) << "index_aff: " << index_aff;

  isl::space model2 = iterator_map.space();
  index_aff = isl::manage(isl_pw_multi_aff_align_params(index_aff.copy(), model2.copy()));
  CINN_DEBUG(2) << "align_params index_aff: " << index_aff;
  isl::space model = index_aff.space();
  CINN_DEBUG(2) << "model: " << model;
  iterator_map = isl::manage(isl_pw_multi_aff_align_params(iterator_map.copy(), model.copy()));
  CINN_DEBUG(2) << "iterator_map1: " << iterator_map;
  iterator_map = isl::manage(isl_pw_multi_aff_pullback_pw_multi_aff(index_aff.copy(), iterator_map.copy()));
  CINN_DEBUG(2) << "iterator_map2: " << iterator_map;

  isl::ast_expr index_expr = isl::manage(isl_ast_build_access_from_pw_multi_aff(build, iterator_map.copy()));
  return index_expr;
}

std::map<std::string, isl::ast_expr> ExtractIslTransformedIndiceMap(const isl::set& iterator_domain,
                                                                    isl_ast_build* build) {
  LOG_INDENT(6);
  std::map<std::string, isl::ast_expr> iterator_map;
  isl::map identity = isl::manage(isl_set_identity(iterator_domain.copy()));
  isl::map schedule = identity;

  CINN_DEBUG(2) << "schedule: " << schedule;

  identity = identity.apply_domain(schedule);
  CINN_DEBUG(2) << "identity: " << identity;

  isl::ast_expr idx_expr = CreateIslAstIndexExpression(build, identity);

  isl::space domain_space = iterator_domain.space();

  for (int i = 1; i < isl_ast_expr_get_op_n_arg(idx_expr.get()); i++) {
    if (isl_space_has_dim_name(domain_space.get(), isl_dim_set, i - 1)) {
      std::string original_idx_name = isl_space_get_dim_name(domain_space.get(), isl_dim_set, i - 1);
      isl::ast_expr transformed_index = isl::manage(isl_ast_expr_get_op_arg(idx_expr.get(), i));
      iterator_map.emplace(original_idx_name, transformed_index);
      CINN_DEBUG(3) << "idx: " << original_idx_name << " " << isl_ast_expr_to_C_str(transformed_index.get());
    }
  }

  CINN_DEBUG(2) << "end extraction";
  return iterator_map;
}

#define TWO_ARG_OP(op__)                                                \
  case ir::NodeTy::op__: {                                              \
    auto* x = root.As<ir::op__>();                                      \
    CINN_DEBUG(3) << "visit " << #op__;                                 \
    CINN_DEBUG(3) << "a: " << x->a;                                     \
    CINN_DEBUG(3) << "b: " << x->b;                                     \
    ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, x->a); \
    ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, x->b); \
    CINN_DEBUG(3) << "get transformed a: " << x->a;                     \
    CINN_DEBUG(3) << "get transformed b: " << x->b;                     \
    break;                                                              \
  };

#define ONE_ARG_OP(op__)                                                \
  case ir::NodeTy::op__: {                                              \
    auto* x = root.As<ir::op__>();                                      \
    ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, x->a); \
    break;                                                              \
  }

void ReplaceCinnIndiceWithIslTransformedIndicesHelper(const std::map<std::string, ir::Expr>& indice_map,
                                                      ir::Expr& root) {  // NOLINT
  LOG_INDENT(6);
  CINN_DEBUG(3) << "replacing " << root;
  switch (root.type()) {
    OP_2_ARGS_FOR_EACH(TWO_ARG_OP);
    OP_1_ARGS_FOR_EACH(ONE_ARG_OP);
    case ir::NodeTy::Var: {
      auto* var = root.As<ir::Var>();
      CINN_DEBUG(4) << "var " << var->name() << " " << var->interval().__str__();
      auto it = indice_map.find(var->name());
      if (it != indice_map.end()) {
        root = ir::CopyExpr(it->second);
        break;
      }
      break;
    }
    case ir::NodeTy::Call: {
      CINN_DEBUG(3) << "visit Call " << root;
      auto* call = root.As<ir::Call>();
      for (auto& it : call->arguments) {
        LOG_INDENT(6);
        CINN_DEBUG(4) << "replacing argument " << it;
        ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, it);
        CINN_DEBUG(4) << "get " << it;
      }
      CINN_DEBUG(3) << "get " << root;
      break;
    }

    case ir::NodeTy::Reference: {
      LOG_INDENT(0);
      auto* reference = root.As<ir::Reference>();
      std::vector<ir::Var> iterators;
      for (auto& it : reference->iterators) {
        LOG_INDENT(2);
        CINN_DEBUG(0) << "replacing " << it;
        ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, it);
        CINN_DEBUG(0) << "get " << it;
      }
      CINN_DEBUG(3) << "get " << root;
      break;
    }

    case ir::NodeTy::IfThenElse: {
      LOG_INDENT(6);
      auto* if_then_else = root.As<ir::IfThenElse>();
      ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, if_then_else->condition);
      if (if_then_else->true_block.valid()) {
        ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, if_then_else->true_block);
      }
      if (if_then_else->false_block.valid()) {
        ReplaceCinnIndiceWithIslTransformedIndicesHelper(indice_map, if_then_else->false_block);
      }
    }
    case ir::NodeTy::IntImm:
    case ir::NodeTy::Tensor:
      // skip
      break;

    default:
      LOG(ERROR) << "Unsupported op type: " << root.type();
  }
}

ir::Expr ReplaceCinnIndiceWithIslTransformedIndices(const std::map<std::string, isl::ast_expr>& indice_map,
                                                    Expr& root) {  // NOLINT
  // Transform isl expr map to CINN expr map first.
  std::map<std::string, Expr> cinn_expr_indices;
  for (auto& item : indice_map) {
    Expr expr;
    IslAstExprToCinnExpr(item.second, &expr);
    cinn_expr_indices[item.first] = expr;
  }

  // transform all stages.

  // Replace the indices recursively.
  ReplaceCinnIndiceWithIslTransformedIndicesHelper(cinn_expr_indices, root);
  return root;
}

isl_ast_node* IslAstNodeInfoCollect(isl_ast_node* node, isl_ast_build* build, void* user) {
  LOG_INDENT(6);
  Stage stage = GlobalContext().generator().GetComputationByNode(node);
  CINN_DEBUG(2) << "Stage name is " << stage.name();
  CHECK(!stage.name().empty());
  CHECK(!stage.iterator_domain().is_null());
  CHECK(build);
  auto isl_indice_map = ExtractIslTransformedIndiceMap(stage.iterator_domain(), build);

  std::map<std::string, Expr> cinn_expr_indices;
  CINN_DEBUG(2) << "collected isl_indice_map.size: " << isl_indice_map.size();
  for (auto& item : isl_indice_map) {
    Expr expr;
    IslAstExprToCinnExpr(item.second, &expr);
    cinn_expr_indices[item.first] = expr;
    CINN_DEBUG(2) << "CINN indice expr: " << item.first << " -> " << expr;
  }

  CINN_DEBUG(3) << "stage " << stage.name() << " set indice map, size: " << cinn_expr_indices.size();
  stage.SetIndiceMap(std::move(cinn_expr_indices));
  return node;
}

// Replace the original CINN expression's operand with the actual forloop iterators in ISL.
// For example:
// original statement: S0[i,j,k] = A[i] * A[j] + B[k]
// the forloop iterator levels: [c0, c3+1, c4*2]
// will get the s0[c0, s3+1, c4*2]
std::map<std::string, ir::Expr> ExprAttachIslIndices(ir::Expr expr, isl::set domain, const ir::Reference& reference) {
  LOG_INDENT(2);
  CHECK_EQ(isl_set_dim(domain.get(), isl_dim_set), reference.iterators.size()) << "dimension not match";
  // construct the map from name of cinn vars to isl iterator.
  std::map<std::string, ir::Expr> cinn2isl_exprs;

  int ndims = reference.iterators.size();

  for (int i = 0; i < ndims; i++) {
    std::string cinn_var_name = isl_set_get_dim_name(domain.get(), isl_dim_set, i);
    cinn2isl_exprs[cinn_var_name] = reference.iterators[i];
    CINN_DEBUG(0) << "cinn to isl exprs: " << cinn_var_name << " " << reference.iterators[i];
  }
  return cinn2isl_exprs;
}

std::map<std::string, ir::Expr> ExprAttachIslIndices(ir::Expr expr, isl::set domain, const ir::Call& call) {
  LOG_INDENT(6);
  CHECK_EQ(isl_set_dim(domain.get(), isl_dim_set), call.arguments.size()) << "dimension not match";
  // construct the map from name of cinn vars to isl iterator.
  std::map<std::string, ir::Expr> cinn2isl_exprs;

  int ndims = call.arguments.size();

  for (int i = 0; i < ndims; i++) {
    std::string cinn_var_name = isl_set_get_dim_name(domain.get(), isl_dim_set, i);
    cinn2isl_exprs[cinn_var_name] = call.arguments[i];
    CINN_DEBUG(0) << "cinn to isl exprs: " << cinn_var_name << " " << call.arguments[i];
  }
  return cinn2isl_exprs;
}

/**
 * Replace the variables in the expression corresponding to a map.
 * @param expr the expression where the variable to replace.
 * @param map the map of var to.
 */
void ReplaceVarInExpr(Expr* expr, const std::map<std::string, ir::Expr>& map) {
  struct Mutator : public ir::IRMutator {
    const Expr& expr;
    const std::map<std::string, ir::Expr>& map;

    Mutator(const Expr& expr, const std::map<std::string, ir::Expr>& map) : expr(expr), map(map) {}

    void Visit(const Expr* op, Expr* expr) override { IRMutator::Visit(op, expr); }

    void Visit(const ir::Var* op, Expr* expr) override {
      auto it = map.find(op->name());
      CHECK(it != map.end()) << "iterator " << op->name() << " not exists in Call";
      CINN_DEBUG(3) << "replace " << *expr << " with " << it->second;
      *expr = it->second;
    }
  };

  Mutator mutator(*expr, map);
  mutator.Visit(expr, expr);
}

void AttachCinnExprToIslIndices(Expr& root, const std::string& stage_name) {  // NOLINT
  LOG_INDENT(4);
  CINN_DEBUG(0) << "\n" << root;
  CINN_DEBUG(0) << "*** Attach " << stage_name;
  auto stage = GlobalContext().generator().GetStageByName(stage_name);

  struct Collector : public ir::IRMutator {
    std::string statement_;

    explicit Collector(const std::string& statement) : statement_(statement) {}

    void Visit(const Expr* op, Expr* expr) override { IRMutator::Visit(op, expr); }

    void Visit(const ir::Call* op, Expr* expr) override {
      LOG_INDENT(6);
      auto* m_op = expr->As<ir::Call>();
      auto stage = GlobalContext().generator().GetStageByName(statement_);

      CINN_DEBUG(0) << "current stage: " << op->caller;
      if (op->caller == statement_) {
        CINN_DEBUG(3) << "replacing " << statement_;
        // replace this.
        auto cinn2isl_exprs = ExprAttachIslIndices(*expr, stage.iterator_domain(), *op);

        CINN_DEBUG(4) << "origina call " << *expr << " " << stage.expr();
        auto copied_expr = ir::CopyExpr(stage.expr());
        ReplaceVarInExpr(&copied_expr, cinn2isl_exprs);
        *expr = copied_expr;
        CINN_DEBUG(4) << "after replaced: " << *expr;
      } else {
        for (auto& arg : m_op->arguments) {
          Visit(&arg, &arg);
        }
      }
    }
  };

  Collector collector(stage_name);
  collector.Visit(&root, &root);
}

Stage Generator::GetComputationByNode(isl_ast_node* node) {
  CHECK(node);
  LOG_INDENT(6);
  isl_ast_expr* expr = isl_ast_node_user_get_expr(node);
  isl_ast_expr* arg = isl_ast_expr_get_op_arg(expr, 0);
  std::string name = isl_id_get_name(isl_ast_expr_get_id(arg));
  CINN_DEBUG(4) << "get stage name: " << name;
  isl_ast_expr_free(expr);
  isl_ast_expr_free(arg);
  return GlobalContext().generator().GetStageByName(name);
}

}  // namespace cinn
