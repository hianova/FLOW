/-
  Flow.Sdk — Engineering Toolchain Umbrella (`FLOW/sdk`)
  ========================================================
  Combines pure mathematics (`Flow.Math`) with engineering emitters & IR builders:
    - `Flow.Emit`     : File IO emitters for `.flow` cells, arenas, and bundles
    - `Flow.Schema`   : Full-512-bit C17/WGSL ABI generator + `.flow` bundle metadata
    - `Flow.Morphism` : QTT resource-typed (`linear` | `cartesian`) SSA `.fir` builder
-/
import Flow.Math
import Flow.Emit
import Flow.Schema
import Flow.Morphism
