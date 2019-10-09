#include "cinn/core/function.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <set>
#include <utility>
#include <vector>
#include "cinn/core/isl_code_gen.h"
#include "cinn/core/stage.h"
#include "cinn/ir/ir_printer.h"
#include "cinn/utils/isl_utils.h"
#include "cinn/utils/logging.h"

namespace cinn {

std::shared_ptr<Function> cinn::Function::make(const std::string& name,
                                               std::vector<Expr> inputs,
                                               std::vector<Expr> outputs,
                                               std::vector<Stage> stages) {
  LOG_INDENT("Function::make");
  auto node = std::make_shared<Function>();
  node->InitData();
  node->data_->name = name;
  node->data_->inputs = inputs;
  node->data_->outputs = outputs;

  for (auto& stage : stages) {
    node->AddStage(stage);
  }
  CHECK(node->data_->ctx);

  node->EndDefinition();

  return node;
}

std::vector<std::string> CollectAllIteratorsFromStages(std::vector<Stage>& stages) {
  std::vector<std::string> iters;
  std::set<std::string> iters_set;

  for (auto& stage : stages) {
    const int ndims = isl_set_n_dim(stage.iterator_domain().get());
    for (int i = 0; i < ndims; i++) {
      std::string iter_name = isl_set_get_dim_name(stage.iterator_domain().get(), isl_dim_set, i);
      if (!iters_set.count(iter_name)) {
        iters.push_back(iter_name);
        iters_set.insert(iter_name);
      }
    }
  }
  return iters;
}

void Function::Accept(ir::IRVisitor* visitor) const {}

// TODO(Superjomn) to make the return type from Expr to vector<Expr> to contain multiple expressions and support Call
// and Allocate.
const Expr& Function::ComputeTransformedExpr() const {
  if (data_->transformed_expr.valid()) return data_->transformed_expr;
  data_->transformed_expr = snippets().back().GetTransformedExpr();

  // Only create a new block if there is more than one expressions, to avoid unnecessary block indents.
  if (snippets().size() > 1UL) {
    // Get a block with none or multiple expressions.
    std::vector<Expr> exprs;
    for (auto& snippet : data_->snippets) {
      exprs.push_back(snippet.GetTransformedExpr());
    }
    data_->transformed_expr = ir::Block::make(std::move(exprs));
  }

  return data_->transformed_expr;
}

// This is a naive implementation which has complexity of N^2
void Function::ComputeStageFlows() {
  isl::union_map all_deps;
  for (size_t s1_id = 0; s1_id < data_->stages.size(); s1_id++) {
    for (size_t s0_id = s1_id + 1; s0_id < data_->stages.size(); s0_id++) {
      auto& s0 = data_->stages[s0_id];
      auto& s1 = data_->stages[s1_id];

      CHECK(s0.read_access());
      CHECK(s0.write_access());
      CHECK(s1.read_access());
      CHECK(s1.write_access());

      isl_union_map* deps =
          isl_utils::isl_calculate_dependency(s0.read_access(), s0.write_access(), s1.read_access(), s1.write_access());
      all_deps = all_deps.is_null() ? isl::manage(deps) : isl::manage(isl_union_map_union(all_deps.release(), deps));
    }
  }
}

Stage Function::AddStage(const Stage& stage) {
  data_->stages.push_back(stage);
  return stage;
}

void Function::BuildSnippets() {
  LOG_INDENT("Function::BuildSnippets, function " + name());
  auto& snippets = data_->snippets;
  for (auto& stage : data_->stages) {
    CINN_DEBUG(3) << "add stage: " << stage.name() << " " << ir::Dump(stage.expr());
    CINN_DEBUG(4) << "stage.type: " << stage.type();
    CINN_DEBUG(6) << "snippets.size: " << snippets.size();
    // add to snippets
    if (snippets.empty() || snippets.back().is_unk()) {
      snippets.emplace_back();
    } else if (snippets.back().type() != stage.type()) {
      LOG(INFO) << "snippets.back().type: " << snippets.back().type();
      snippets.back().End();
      snippets.emplace_back();
    }
    snippets.back().AddStage(stage);
  }

  if (!snippets.empty()) snippets.back().End();
  CINN_DEBUG(3) << "get snippets size " << snippets.size();
}

Expr Function::operator()(const std::vector<Expr>& inputs, const std::vector<Expr>& outputs) {
  if (!data_->is_inline) {
    std::vector<Expr> args(inputs.begin(), inputs.end());
    std::transform(outputs.begin(), outputs.end(), std::back_inserter(args), [](const Expr& e) { return e; });
    return ir::Call::make(data_->name, args);
  } else {
    // expand
    LOG(FATAL) << "not supported yet";
  }
}

Function::operator Expr() {
  auto node = std::make_shared<Function>();
  node->data_ = data_;
  return Expr(node);
}

void Snippet::CollectIteratorDomain() {
  LOG_INDENT("Snippet::InitIteratorDomainFromStages ");
  CHECK(Stage::is_polyhedral(type())) << "only polyhedral snippet supports iterator domain";

  // Collect all the iterators.
  auto iter_names = CollectAllIteratorsFromStages(stages_);
  // Combine iterator domain
  CHECK(iterator_domain().is_null());

  for (auto& stage : stages_) {
    if (iterator_domain().is_null()) {
      *iterator_domain_ = isl::manage(isl_union_set_from_set(stage.iterator_domain().copy()));
    } else {
      *iterator_domain_ = isl::manage(isl_union_set_add_set(iterator_domain().copy(), stage.iterator_domain().copy()));
    }
  }

  CINN_DEBUG(3) << "collected iterator domain: " << iterator_domain();
}

void Snippet::CollectTransforms() {
  LOG_INDENT("Snippet::CollectTransforms");
  CHECK(Stage::is_polyhedral(type())) << "only polyhedral snippet supports transform collection";

  for (auto& stage : stages_) {
    if (transform_->is_null()) {
      *transform_ = isl::manage(isl_union_map_from_map(stage.schedule().copy()));
    } else {
      *transform_ = isl::manage(isl_union_map_add_map(transform_->release(), stage.schedule().copy()));
    }
  }

  CINN_DEBUG(3) << "get transform collection: " << *transform_;
}

void Snippet::AddStage(const Stage& stage) {
  LOG_INDENT("Snippet:AddStage");
  CHECK(!is_end_) << "definition of the snippet is end, should not add stages.";
  CHECK(stage.type() != Stage::Type::unk);
  CINN_DEBUG(3) << "add a " << stage.type() << " stage called " << stage.name();
  CINN_DEBUG(3) << "snippet type " << type();
  CINN_DEBUG(3) << "stage type " << stage.type();

  if (is_unk()) {
    type_ = stage.type();
  } else {
    CHECK_EQ(type_, stage.type()) << "type not match";
  }
  stages_.push_back(stage);
}

void Snippet::CollectReadAccess() {
  LOG_INDENT("Snippet::CollectReadAccess");
  CHECK(Stage::is_polyhedral(type()));
  for (auto& stage : stages_) {
    CHECK(stage.read_access());
    if (access_reads_->is_null()) {
      *access_reads_ = isl::manage(isl_union_map_copy(stage.read_access()));
    } else {
      *access_reads_ =
          isl::manage(isl_union_map_union(access_reads_->release(), isl_union_map_copy(stage.read_access())));
    }
  }
  CINN_DEBUG(3) << "collect read access: " << *access_reads_;
}

void Snippet::CollectWriteAccess() {
  LOG_INDENT("Snippet::CollectWriteAccess");
  CHECK(Stage::is_polyhedral(type()));
  for (auto& stage : stages_) {
    CHECK(stage.write_access());
    if (access_writes_->is_null()) {
      *access_writes_ = isl::manage(isl_union_map_copy(stage.write_access()));
    } else {
      *access_writes_ =
          isl::manage(isl_union_map_union(access_writes_->release(), isl_union_map_copy(stage.write_access())));
    }
  }
  CINN_DEBUG(3) << "collect read access: " << *access_writes_;
}

isl::union_map ComputeDeps(const isl::union_set& domain, const isl::union_map& reads, const isl::union_map& writes) {
  isl_ctx* ctx = domain.ctx().get();

  isl::union_map access_reads_with_domain = isl::manage(isl_union_map_intersect_domain(reads.copy(), domain.copy()));
  isl::union_map access_writes_with_domain = isl::manage(isl_union_map_intersect_domain(writes.copy(), domain.copy()));

  isl::union_map reads_writes = access_reads_with_domain;
  reads_writes = isl::manage(isl_union_map_union(reads_writes.release(), access_writes_with_domain.copy()));

  isl::union_map left = isl::manage(
      isl_union_map_apply_range(reads_writes.copy(), isl_union_map_reverse(access_writes_with_domain.copy())));
  CINN_DEBUG(3) << "read_write o write^-1: " << left;
  isl::union_map right = isl::manage(isl_union_map_apply_range(access_writes_with_domain.copy(),
                                                               isl_union_map_reverse(access_reads_with_domain.copy())));
  CINN_DEBUG(3) << "wrie o read^-1: " << right;

  isl::union_map deps = isl::manage(isl_union_map_union(left.release(), right.release()));
  deps = isl::manage(isl_union_map_detect_equalities(deps.release()));
  return deps;
}

isl::union_map ComputeScheduleValidity(const isl::union_set& domain, const isl::union_map& deps) {
  isl::union_map validity = isl::manage(isl_union_map_empty(isl_space_copy(domain.space().get())));
  // currently, we ignore the b->a dependency.
  // TODO(Superjomn) support full analysis for dependencies for any pairs.
  for (int i = 0; i < isl_union_map_n_map(deps.get()); i++) {
    isl_map_list_guard map_list(isl_union_map_get_map_list(deps.get()));
    isl::map map = isl::manage(isl_map_list_get_at(map_list.get(), i));
    if (isl_map_is_identity(map.get())) continue;

    const char* left_tuple = isl_map_get_tuple_name(map.get(), isl_dim_in);
    const char* right_tuple = isl_map_get_tuple_name(map.get(), isl_dim_out);

    if (std::strcmp(left_tuple, right_tuple) >= 0) continue;

    isl::union_map union_map = isl::manage(isl_union_map_from_map(map.copy()));
    if (validity.is_null()) {
      validity = union_map;
    } else {
      validity = isl::manage(isl_union_map_union(validity.release(), union_map.release()));
    }
  }

  return validity;
}

void Snippet::ComputeSchedule() {
  // Use a unique ctx to avoid obstruction.
  LOG_INDENT("Snippet::ComputeSchedule");
  CHECK(Stage::is_polyhedral(type()));
  CHECK(!access_reads_->is_null());
  CHECK(!access_writes_->is_null());
  CHECK(!transform_->is_null());

  auto domain = isl::union_set(ctx_, GetStreamStr(iterator_domain()));
  auto reads = isl::union_map(ctx_, GetStreamStr(access_reads()));
  auto writes = isl::union_map(ctx_, GetStreamStr(access_writes()));
  auto deps = ComputeDeps(domain, reads, writes);
  auto validity = ComputeScheduleValidity(domain, deps);
  CHECK(!validity.is_null());

  CINN_DEBUG(3) << "get memory dependencies: " << validity;

  BuildFusion();
  isl::union_map proximity;
  if (approxi_) proximity = isl::union_map(ctx_, GetStreamStr(*approxi_));

  isl::schedule_constraints sc = isl::manage(isl_schedule_constraints_on_domain(domain.release()));
  sc = isl::manage(isl_schedule_constraints_set_validity(sc.release(), validity.release()));
  if (!proximity.is_null()) sc = isl::manage(isl_schedule_constraints_set_proximity(sc.release(), proximity.release()));
  // sc = isl::manage(isl_schedule_constraints_apply(sc.release(), transform_->copy()));

  CINN_DEBUG(3) << "schedule constraints:\n" << sc;

  *schedule_ = isl::manage(isl_schedule_constraints_compute_schedule(sc.release()));
  CINN_DEBUG(3) << "schedule:\n" << isl_utils::DumpSchedule(ctx_.get(), *schedule_);

  BuildTiles();
}

void Snippet::BuildTiles() {
  LOG(INFO) << "******** build tiles";
  if (!is_polyhedral()) return;

  CHECK(schedule_) << "schedule tree should be build first before tile";

  for (auto& stage : stages_) {
    if (stage.tiles().empty()) continue;
    IslTileGenerator::Global().set_stage_name(stage.name());

    isl_schedule_node* root =
        isl_schedule_node_map_descendant_bottom_up(isl_schedule_get_root(schedule_->get()), cinn::node_tiler, nullptr);
    schedule_.reset(new isl::schedule(isl::manage(isl_schedule_node_get_schedule(root))));
  }
}

void Snippet::BuildFusion() {
  for (auto& stage : stages_) {
    std::string this_stage = stage.name();
    for (auto& target : stage.stages_fuse_with()) {
      auto it = std::find_if(stages_.begin(), stages_.end(), [&](const Stage& o) { return o.name() == target; });
      CHECK(it != stages_.end());

      auto this_statement = isl_set_get_statement_repr(stage.iterator_domain().get());
      auto target_statement = isl_set_get_statement_repr(it->iterator_domain().get());

      isl::union_map map(isl_utils::global_isl_ctx(),
                         StringFormat("{ %s -> %s }", this_statement.c_str(), target_statement.c_str()).c_str());
      if (!approxi_) {
        approxi_.reset(new isl::union_map);
        *approxi_ = isl::manage(map.release());
      } else {
        *approxi_ = isl::manage(isl_union_map_union(approxi_->release(), map.release()));
      }
    }
  }
}

isl::ast_node Snippet::GenerateIslAst() const {
  LOG_INDENT("Snippet::GenerateIslAst");
  isl::ast_node res;
  if (!is_polyhedral()) return res;

  CHECK(!iterator_domain().is_null());

  // TODO(Superjomn) pass the parameters.
  isl::set C(isl_utils::global_isl_ctx(), "{:}");
  isl::ast_build build = isl::manage(isl_ast_build_from_context(C.copy()));

  build = isl::manage(isl_ast_build_set_at_each_domain(build.release(), IslAstNodeInfoCollect, nullptr));
  isl::ast_node ast = isl::manage(isl_ast_build_node_from_schedule(build.get(), schedule_->copy()));

  CINN_DEBUG(3) << "schedule tree get C code:\n" << isl_ast_node_to_C_str(ast.get());
  return ast;
}

Expr Snippet::GetTransformedExpr() const {
  LOG_INDENT("Snippet::GetTransformedExpr");
  CHECK(is_end_);

  if (!is_polyhedral()) {
    if (stages_.size() == 1) {
      return stages_.back().expr();
    } else {
      // collect none or multiple stages.
      std::vector<Expr> exprs;
      for (auto& stage : stages_) {
        // TODO need CopyExpr here ?
        CINN_DEBUG(3) << "collect non-polyhedral expr " << ir::Dump(stage.expr());
        exprs.emplace_back(stage.expr());
      }
      return ir::Block::make(std::move(exprs));
    }
  }

  // a polyhedral snippet
  isl::ast_node ast = GenerateIslAst();
  Expr expr;
  IslAstNodeToCinnExpr(ast, &expr);
  for (int i = 0; i < stages_.size(); i++) {
    ReplaceExprWithStage(expr, stages_[i].name(), stages_[i].GetIndiceTransformedExpr());
  }
  return expr;
}

void Snippet::TryFuse(const std::string& stage0, const std::string& stage1) {
  // colect a map from name to stage pointer.
  std::map<std::string, Stage*> map;
  for (auto& stage : stages_) {
    map[stage.name()] = &stage;
  }

  // will try to fuse if these two stage exists in the same snippet.
  if (map.count(stage0) && map.count(stage1)) {
    Stage& a = *map[stage0];
    Stage& b = *map[stage1];
    a.iterator_domain();
  }
}

}  // namespace cinn
