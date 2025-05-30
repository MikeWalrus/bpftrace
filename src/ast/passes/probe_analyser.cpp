#include "probe_analyser.h"
#include "ast/visitor.h"
#include "bpftrace.h"

namespace bpftrace::ast {

namespace {

class ProbeAnalyser : public Visitor<ProbeAnalyser> {
public:
  explicit ProbeAnalyser(ASTContext &ctx, BPFtrace &bpftrace)
      : ctx_(ctx), bpftrace_(bpftrace)
  {
  }

  using Visitor<ProbeAnalyser>::visit;
  void visit(Probe &probe);

  void analyse();

private:
  const ASTContext &ctx_;
  const BPFtrace &bpftrace_;
};

void ProbeAnalyser::visit(Probe &probe)
{
  // If the probe has a single multi-expanded kprobe attach point, check if
  // there's another probe with a single multi-expanded kretprobe attach point
  // with the same target. If so, use session expansion if available.
  // Also, we do not allow predicates in any of the probes for now.
  if (probe.attach_points.size() == 1 &&
      probetype(probe.attach_points[0]->provider) == ProbeType::kprobe &&
      probe.pred == nullptr) {
    // Session probes use the same attach mechanism as multi probes so the
    // attach point must be multi-expanded.
    auto &ap = *probe.attach_points[0];
    if (ap.expansion != ExpansionType::MULTI)
      return;

    for (Probe *other_probe : ctx_.root->probes) {
      // Other probe must also have a single multi-expanded attach point and no
      // predicate
      if (other_probe->attach_points.size() != 1 || other_probe->pred)
        continue;
      auto &other_ap = *other_probe->attach_points[0];
      if (probetype(other_ap.provider) != ProbeType::kretprobe ||
          other_ap.expansion != ExpansionType::MULTI) {
        continue;
      }

      if (ap.target == other_ap.target && ap.func == other_ap.func) {
        if (bpftrace_.feature_->has_kprobe_session()) {
          ap.expansion = ExpansionType::SESSION;
          ap.ret_probe = other_probe;
          other_ap.expansion = ExpansionType::SESSION;
        } else {
          return;
        }
      }
    }
  }
}

} // namespace

Pass CreateProbePass()
{
  auto fn = [](ASTContext &ast, BPFtrace &b) {
    ProbeAnalyser analyser(ast, b);
    analyser.visit(ast.root);
  };

  return Pass::create("Probe", fn);
};

} // namespace bpftrace::ast
