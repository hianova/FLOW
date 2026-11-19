/-
  FLOW v8.0.0 — Backward-Compatible Umbrella (`import Flow`)
  ===========================================================
  Re-exports `Flow.Sdk` (which itself imports `Flow.Math` + `Flow.Emit` + `Flow.Schema` +
  `Flow.Morphism`). Consumers requiring only pure mathematics without IO or codegen
  should `import Flow.Math` or specific `Flow.Core` / `Flow.Bundle` modules directly.
-/
import Flow.Sdk
